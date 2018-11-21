// SPDX-License-Identifier: GPL-2.0
/*
 * cxd2880_io.c
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver
 * register I/O interface functions
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#include "cxd2880_io.h"

int cxd2880_io_common_write_one_reg(struct cxd2880_io *io,
				    enum cxd2880_io_tgt tgt,
				    u8 sub_address, u8 data)
{
	if (!io)
		return -EINVAL;

	return io->write_regs(io, tgt, sub_address, &data, 1);
}

int cxd2880_io_set_reg_bits(struct cxd2880_io *io,
			    enum cxd2880_io_tgt tgt,
			    u8 sub_address, u8 data, u8 mask)
{
	int ret;

	if (!io)
		return -EINVAL;

	if (mask == 0x00)
		return 0;

	if (mask != 0xff) {
		u8 rdata = 0x00;

		ret = io->read_regs(io, tgt, sub_address, &rdata, 1);
		if (ret)
			return ret;

		data = (data & mask) | (rdata & (mask ^ 0xff));
	}

	return io->write_reg(io, tgt, sub_address, data);
}

int cxd2880_io_write_multi_regs(struct cxd2880_io *io,
			     enum cxd2880_io_tgt tgt,
			     const struct cxd2880_reg_value reg_value[],
			     u8 size)
{
	int ret;
	int i;

	if (!io)
		return -EINVAL;

	for (i = 0; i < size ; i++) {
		ret = io->write_reg(io, tgt, reg_value[i].addr,
				    reg_value[i].value);
		if (ret)
			return ret;
	}

	return 0;
}
