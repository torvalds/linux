/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKCIF_VERSION_H
#define _RKCIF_VERSION_H
#include <linux/version.h>

/*
 *RKCIF DRIVER VERSION NOTE
 *
 *v0.1.0:
 *1. First version;
 *v0.1.1
 *1. Support the mipi vc multi-channel input in cif driver for rk1808
 *v0.1.2
 *1. support output yuyv fmt by setting the input mode to raw8
 *2. Compatible with cif only have single dma mode in driver
 *3. Support cif works with mipi channel for rk3288
 *4. Support switching between oneframe and pingpong for cif
 *5. Support sampling raw data for cif
 *6. fix the bug that dummpy buffer size is error
 *7. Add framesizes and frmintervals callback
 *8. fix dvp camera fails to link with cif on rk1808
 */

#define RKCIF_DRIVER_VERSION KERNEL_VERSION(0, 1, 0x2)

#endif
