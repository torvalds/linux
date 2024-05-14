/* SPDX-License-Identifier: GPL-2.0 */
/*
 * cxd2880_io.h
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver
 * register I/O interface definitions
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#ifndef CXD2880_IO_H
#define CXD2880_IO_H

#include "cxd2880_common.h"

enum cxd2880_io_tgt {
	CXD2880_IO_TGT_SYS,
	CXD2880_IO_TGT_DMD
};

struct cxd2880_reg_value {
	u8 addr;
	u8 value;
};

struct cxd2880_io {
	int (*read_regs)(struct cxd2880_io *io,
			 enum cxd2880_io_tgt tgt, u8 sub_address,
			 u8 *data, u32 size);
	int (*write_regs)(struct cxd2880_io *io,
			  enum cxd2880_io_tgt tgt, u8 sub_address,
			  const u8 *data, u32 size);
	int (*write_reg)(struct cxd2880_io *io,
			 enum cxd2880_io_tgt tgt, u8 sub_address,
			 u8 data);
	void *if_object;
	u8 i2c_address_sys;
	u8 i2c_address_demod;
	u8 slave_select;
	void *user;
};

int cxd2880_io_common_write_one_reg(struct cxd2880_io *io,
				    enum cxd2880_io_tgt tgt,
				    u8 sub_address, u8 data);

int cxd2880_io_set_reg_bits(struct cxd2880_io *io,
			    enum cxd2880_io_tgt tgt,
			    u8 sub_address, u8 data, u8 mask);

int cxd2880_io_write_multi_regs(struct cxd2880_io *io,
				enum cxd2880_io_tgt tgt,
				const struct cxd2880_reg_value reg_value[],
				u8 size);
#endif
