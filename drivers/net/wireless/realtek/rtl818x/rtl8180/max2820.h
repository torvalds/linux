/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef RTL8180_MAX2820_H
#define RTL8180_MAX2820_H

/*
 * Radio tuning for Maxim max2820 on RTL8180
 *
 * Copyright 2007 Andrea Merello <andrea.merello@gmail.com>
 *
 * Code from the BSD driver and the rtl8181 project have been
 * very useful to understand certain things
 *
 * I want to thanks the Authors of such projects and the Ndiswrapper
 * project Authors.
 *
 * A special Big Thanks also is for all people who donated me cards,
 * making possible the creation of the original rtl8180 driver
 * from which this code is derived!
 */

#define MAXIM_ANTENNA 0xb3

extern const struct rtl818x_rf_ops max2820_rf_ops;

#endif /* RTL8180_MAX2820_H */
