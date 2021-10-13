/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Kay Guo <kay.guo@rock-chips.com>
 */


#ifndef __DA215S_CORE_H__
#define __DA215S_CORE_H__

#define DA215S_OFFSET_TEMP_SOLUTION	0

#define MIR3DA_OFFSET_MAX		200
#define MIR3DA_OFFSET_CUS		130
#define MIR3DA_OFFSET_SEN		1024

int da215s_temp_calibrate(int *x, int *y, int *z);

#endif    /* __DA215S_CORE_H__ */




