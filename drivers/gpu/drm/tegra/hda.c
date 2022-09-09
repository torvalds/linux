// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2019 NVIDIA Corporation
 */

#include <linux/bug.h>

#include <sound/hda_verbs.h>

#include "hda.h"

void tegra_hda_parse_format(unsigned int format, struct tegra_hda_format *fmt)
{
	unsigned int mul, div, bits, channels;

	if (format & AC_FMT_TYPE_NON_PCM)
		fmt->pcm = false;
	else
		fmt->pcm = true;

	if (format & AC_FMT_BASE_44K)
		fmt->sample_rate = 44100;
	else
		fmt->sample_rate = 48000;

	mul = (format & AC_FMT_MULT_MASK) >> AC_FMT_MULT_SHIFT;
	div = (format & AC_FMT_DIV_MASK) >> AC_FMT_DIV_SHIFT;

	fmt->sample_rate *= (mul + 1) / (div + 1);

	switch (format & AC_FMT_BITS_MASK) {
	case AC_FMT_BITS_8:
		fmt->bits = 8;
		break;

	case AC_FMT_BITS_16:
		fmt->bits = 16;
		break;

	case AC_FMT_BITS_20:
		fmt->bits = 20;
		break;

	case AC_FMT_BITS_24:
		fmt->bits = 24;
		break;

	case AC_FMT_BITS_32:
		fmt->bits = 32;
		break;

	default:
		bits = (format & AC_FMT_BITS_MASK) >> AC_FMT_BITS_SHIFT;
		WARN(1, "invalid number of bits: %#x\n", bits);
		fmt->bits = 8;
		break;
	}

	channels = (format & AC_FMT_CHAN_MASK) >> AC_FMT_CHAN_SHIFT;

	/* channels are encoded as n - 1 */
	fmt->channels = channels + 1;
}
