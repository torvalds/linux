/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    I2C functions
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@xs4all.nl>

 */

#ifndef IVTV_I2C_H
#define IVTV_I2C_H

void ivtv_i2c_new_ir_legacy(struct ivtv *itv);
int ivtv_i2c_register(struct ivtv *itv, unsigned idx);
struct v4l2_subdev *ivtv_find_hw(struct ivtv *itv, u32 hw);

/* init + register i2c adapter */
int init_ivtv_i2c(struct ivtv *itv);
void exit_ivtv_i2c(struct ivtv *itv);

#endif
