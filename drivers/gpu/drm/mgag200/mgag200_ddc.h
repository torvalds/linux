/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __MGAG200_DDC_H__
#define __MGAG200_DDC_H__

struct i2c_adapter;
struct mga_device;

struct i2c_adapter *mgag200_ddc_create(struct mga_device *mdev);

#endif
