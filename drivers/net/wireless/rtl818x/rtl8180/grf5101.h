#ifndef RTL8180_GRF5101_H
#define RTL8180_GRF5101_H

/*
 * Radio tuning for GCT GRF5101 on RTL8180
 *
 * Copyright 2007 Andrea Merello <andreamrl@tiscali.it>
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
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define GRF5101_ANTENNA 0xA3

extern const struct rtl818x_rf_ops grf5101_rf_ops;

#endif /* RTL8180_GRF5101_H */
