// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019-2020 Pengutronix, Michael Tretter <kernel@pengutronix.de>
 *
 * Helper functions to generate a raw byte sequence payload from values.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/v4l2-controls.h>

#include <linux/device.h>
#include <linux/export.h>
#include <linux/log2.h>

#include "nal-rbsp.h"

void rbsp_init(struct rbsp *rbsp, void *addr, size_t size,
	       struct nal_rbsp_ops *ops)
{
	if (!rbsp)
		return;

	rbsp->data = addr;
	rbsp->size = size;
	rbsp->pos = 0;
	rbsp->ops = ops;
	rbsp->error = 0;
}

void rbsp_unsupported(struct rbsp *rbsp)
{
	rbsp->error = -EINVAL;
}

static int rbsp_read_bits(struct rbsp *rbsp, int n, unsigned int *value);
static int rbsp_write_bits(struct rbsp *rbsp, int n, unsigned int value);

/*
 * When reading or writing, the emulation_prevention_three_byte is detected
 * only when the 2 one bits need to be inserted. Therefore, we are not
 * actually adding the 0x3 byte, but the 2 one bits and the six 0 bits of the
 * next byte.
 */
#define EMULATION_PREVENTION_THREE_BYTE (0x3 << 6)

static int add_emulation_prevention_three_byte(struct rbsp *rbsp)
{
	rbsp->num_consecutive_zeros = 0;
	rbsp_write_bits(rbsp, 8, EMULATION_PREVENTION_THREE_BYTE);

	return 0;
}

static int discard_emulation_prevention_three_byte(struct rbsp *rbsp)
{
	unsigned int tmp = 0;

	rbsp->num_consecutive_zeros = 0;
	rbsp_read_bits(rbsp, 8, &tmp);
	if (tmp != EMULATION_PREVENTION_THREE_BYTE)
		return -EINVAL;

	return 0;
}

static inline int rbsp_read_bit(struct rbsp *rbsp)
{
	int shift;
	int ofs;
	int bit;
	int err;

	if (rbsp->num_consecutive_zeros == 22) {
		err = discard_emulation_prevention_three_byte(rbsp);
		if (err)
			return err;
	}

	shift = 7 - (rbsp->pos % 8);
	ofs = rbsp->pos / 8;
	if (ofs >= rbsp->size)
		return -EINVAL;

	bit = (rbsp->data[ofs] >> shift) & 1;

	rbsp->pos++;

	if (bit == 1 ||
	    (rbsp->num_consecutive_zeros < 7 && (rbsp->pos % 8 == 0)))
		rbsp->num_consecutive_zeros = 0;
	else
		rbsp->num_consecutive_zeros++;

	return bit;
}

static inline int rbsp_write_bit(struct rbsp *rbsp, bool value)
{
	int shift;
	int ofs;

	if (rbsp->num_consecutive_zeros == 22)
		add_emulation_prevention_three_byte(rbsp);

	shift = 7 - (rbsp->pos % 8);
	ofs = rbsp->pos / 8;
	if (ofs >= rbsp->size)
		return -EINVAL;

	rbsp->data[ofs] &= ~(1 << shift);
	rbsp->data[ofs] |= value << shift;

	rbsp->pos++;

	if (value ||
	    (rbsp->num_consecutive_zeros < 7 && (rbsp->pos % 8 == 0))) {
		rbsp->num_consecutive_zeros = 0;
	} else {
		rbsp->num_consecutive_zeros++;
	}

	return 0;
}

static inline int rbsp_read_bits(struct rbsp *rbsp, int n, unsigned int *value)
{
	int i;
	int bit;
	unsigned int tmp = 0;

	if (n > 8 * sizeof(*value))
		return -EINVAL;

	for (i = n; i > 0; i--) {
		bit = rbsp_read_bit(rbsp);
		if (bit < 0)
			return bit;
		tmp |= bit << (i - 1);
	}

	if (value)
		*value = tmp;

	return 0;
}

static int rbsp_write_bits(struct rbsp *rbsp, int n, unsigned int value)
{
	int ret;

	if (n > 8 * sizeof(value))
		return -EINVAL;

	while (n--) {
		ret = rbsp_write_bit(rbsp, (value >> n) & 1);
		if (ret)
			return ret;
	}

	return 0;
}

static int rbsp_read_uev(struct rbsp *rbsp, unsigned int *value)
{
	int leading_zero_bits = 0;
	unsigned int tmp = 0;
	int ret;

	while ((ret = rbsp_read_bit(rbsp)) == 0)
		leading_zero_bits++;
	if (ret < 0)
		return ret;

	if (leading_zero_bits > 0) {
		ret = rbsp_read_bits(rbsp, leading_zero_bits, &tmp);
		if (ret)
			return ret;
	}

	if (value)
		*value = (1 << leading_zero_bits) - 1 + tmp;

	return 0;
}

static int rbsp_write_uev(struct rbsp *rbsp, unsigned int *value)
{
	int ret;
	int leading_zero_bits;

	if (!value)
		return -EINVAL;

	leading_zero_bits = ilog2(*value + 1);

	ret = rbsp_write_bits(rbsp, leading_zero_bits, 0);
	if (ret)
		return ret;

	return rbsp_write_bits(rbsp, leading_zero_bits + 1, *value + 1);
}

static int rbsp_read_sev(struct rbsp *rbsp, int *value)
{
	int ret;
	unsigned int tmp;

	ret = rbsp_read_uev(rbsp, &tmp);
	if (ret)
		return ret;

	if (value) {
		if (tmp & 1)
			*value = (tmp + 1) / 2;
		else
			*value = -(tmp / 2);
	}

	return 0;
}

static int rbsp_write_sev(struct rbsp *rbsp, int *value)
{
	unsigned int tmp;

	if (!value)
		return -EINVAL;

	if (*value > 0)
		tmp = (2 * (*value)) | 1;
	else
		tmp = -2 * (*value);

	return rbsp_write_uev(rbsp, &tmp);
}

static int __rbsp_write_bit(struct rbsp *rbsp, int *value)
{
	return rbsp_write_bit(rbsp, *value);
}

static int __rbsp_write_bits(struct rbsp *rbsp, int n, unsigned int *value)
{
	return rbsp_write_bits(rbsp, n, *value);
}

struct nal_rbsp_ops write = {
	.rbsp_bit = __rbsp_write_bit,
	.rbsp_bits = __rbsp_write_bits,
	.rbsp_uev = rbsp_write_uev,
	.rbsp_sev = rbsp_write_sev,
};

static int __rbsp_read_bit(struct rbsp *rbsp, int *value)
{
	int tmp = rbsp_read_bit(rbsp);

	if (tmp < 0)
		return tmp;
	*value = tmp;

	return 0;
}

struct nal_rbsp_ops read = {
	.rbsp_bit = __rbsp_read_bit,
	.rbsp_bits = rbsp_read_bits,
	.rbsp_uev = rbsp_read_uev,
	.rbsp_sev = rbsp_read_sev,
};

void rbsp_bit(struct rbsp *rbsp, int *value)
{
	if (rbsp->error)
		return;
	rbsp->error = rbsp->ops->rbsp_bit(rbsp, value);
}

void rbsp_bits(struct rbsp *rbsp, int n, int *value)
{
	if (rbsp->error)
		return;
	rbsp->error = rbsp->ops->rbsp_bits(rbsp, n, value);
}

void rbsp_uev(struct rbsp *rbsp, unsigned int *value)
{
	if (rbsp->error)
		return;
	rbsp->error = rbsp->ops->rbsp_uev(rbsp, value);
}

void rbsp_sev(struct rbsp *rbsp, int *value)
{
	if (rbsp->error)
		return;
	rbsp->error = rbsp->ops->rbsp_sev(rbsp, value);
}

void rbsp_trailing_bits(struct rbsp *rbsp)
{
	unsigned int rbsp_stop_one_bit = 1;
	unsigned int rbsp_alignment_zero_bit = 0;

	rbsp_bit(rbsp, &rbsp_stop_one_bit);
	rbsp_bits(rbsp, round_up(rbsp->pos, 8) - rbsp->pos,
		  &rbsp_alignment_zero_bit);
}
