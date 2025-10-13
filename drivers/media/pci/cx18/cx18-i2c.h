/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  cx18 I2C functions
 *
 *  Derived from ivtv-i2c.h
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@kernel.org>
 */

int cx18_i2c_register(struct cx18 *cx, unsigned idx);
struct v4l2_subdev *cx18_find_hw(struct cx18 *cx, u32 hw);

/* init + register i2c adapter */
int init_cx18_i2c(struct cx18 *cx);
void exit_cx18_i2c(struct cx18 *cx);
