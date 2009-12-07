/*
 * Copyright (C) 2008-2009 QUALCOMM Incorporated.
 */

#ifndef MT9D112_H
#define MT9D112_H

#include <linux/types.h>
#include <mach/camera.h>

enum mt9d112_width {
	WORD_LEN,
	BYTE_LEN
};

struct mt9d112_i2c_reg_conf {
	unsigned short waddr;
	unsigned short wdata;
	enum mt9d112_width width;
	unsigned short mdelay_time;
};

struct mt9d112_reg {
	const struct register_address_value_pair *prev_snap_reg_settings;
	uint16_t prev_snap_reg_settings_size;
	const struct register_address_value_pair *noise_reduction_reg_settings;
	uint16_t noise_reduction_reg_settings_size;
	const struct mt9d112_i2c_reg_conf *plltbl;
	uint16_t plltbl_size;
	const struct mt9d112_i2c_reg_conf *stbl;
	uint16_t stbl_size;
	const struct mt9d112_i2c_reg_conf *rftbl;
	uint16_t rftbl_size;
};

#endif /* MT9D112_H */
