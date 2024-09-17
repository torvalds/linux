// SPDX-License-Identifier: GPL-2.0
/*
 * cxd2880_common.c
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver
 * common functions
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#include "cxd2880_common.h"

int cxd2880_convert2s_complement(u32 value, u32 bitlen)
{
	if (!bitlen || bitlen >= 32)
		return (int)value;

	if (value & (u32)(1 << (bitlen - 1)))
		return (int)(GENMASK(31, bitlen) | value);
	else
		return (int)(GENMASK(bitlen - 1, 0) & value);
}
