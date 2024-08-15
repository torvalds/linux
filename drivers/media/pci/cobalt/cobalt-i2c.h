/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  cobalt I2C functions
 *
 *  Derived from cx18-i2c.h
 *
 *  Copyright 2012-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 */

/* init + register i2c algo-bit adapter */
int cobalt_i2c_init(struct cobalt *cobalt);
void cobalt_i2c_exit(struct cobalt *cobalt);
