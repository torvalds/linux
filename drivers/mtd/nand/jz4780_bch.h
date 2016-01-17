/*
 * JZ4780 BCH controller
 *
 * Copyright (c) 2015 Imagination Technologies
 * Author: Alex Smith <alex.smith@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#ifndef __DRIVERS_MTD_NAND_JZ4780_BCH_H__
#define __DRIVERS_MTD_NAND_JZ4780_BCH_H__

#include <linux/types.h>

struct device;
struct device_node;
struct jz4780_bch;

/**
 * struct jz4780_bch_params - BCH parameters
 * @size: data bytes per ECC step.
 * @bytes: ECC bytes per step.
 * @strength: number of correctable bits per ECC step.
 */
struct jz4780_bch_params {
	int size;
	int bytes;
	int strength;
};

int jz4780_bch_calculate(struct jz4780_bch *bch,
				struct jz4780_bch_params *params,
				const u8 *buf, u8 *ecc_code);
int jz4780_bch_correct(struct jz4780_bch *bch,
			      struct jz4780_bch_params *params, u8 *buf,
			      u8 *ecc_code);

void jz4780_bch_release(struct jz4780_bch *bch);
struct jz4780_bch *of_jz4780_bch_get(struct device_node *np);

#endif /* __DRIVERS_MTD_NAND_JZ4780_BCH_H__ */
