/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISPP_VERSION_H
#define _RKISPP_VERSION_H
#include <linux/version.h>
#include <linux/rkispp-config.h>

/*
 * RKISPP DRIVER VERSION NOTE
 *
 * v0.1.0:
 * 1. First version;
 *
 * v0.1.1:
 * 1. support fbc output format
 * 2. support fec function
 * 3. support oneframe mode
 *
 * v0.1.2:
 * 1. support multi virtual device;
 *
 * v0.1.3:
 * 1. fix reg write err for fec
 *
 * v0.1.4:
 * 1. add clk rate set for rv1126
 * 2. safe to enable shp/fec output
 * 3. tnr skip input buf if no output buf
 *
 * v0.1.5:
 * 1. add proc fs
 * 2. add iq part information to procfs
 * 3. fix config err for stream switch
 *
 * v0.1.6:
 * 1. tnr support dynamic switch
 */

#define RKISPP_DRIVER_VERSION ISPP_API_VERSION

#endif
