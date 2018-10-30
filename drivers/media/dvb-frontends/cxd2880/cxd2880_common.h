/* SPDX-License-Identifier: GPL-2.0 */
/*
 * cxd2880_common.h
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver common definitions
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#ifndef CXD2880_COMMON_H
#define CXD2880_COMMON_H

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/string.h>

int cxd2880_convert2s_complement(u32 value, u32 bitlen);

#endif
