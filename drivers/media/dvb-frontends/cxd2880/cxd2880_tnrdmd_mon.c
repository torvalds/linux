// SPDX-License-Identifier: GPL-2.0
/*
 * cxd2880_tnrdmd_mon.c
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver
 * common monitor functions
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#include "cxd2880_common.h"
#include "cxd2880_tnrdmd_mon.h"

static const u8 rf_lvl_seq[2] = {
	0x80, 0x00,
};

int cxd2880_tnrdmd_mon_rf_lvl(struct cxd2880_tnrdmd *tnr_dmd,
			      int *rf_lvl_db)
{
	u8 rdata[2];
	int ret;

	if (!tnr_dmd || !rf_lvl_db)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x00);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x10, 0x01);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x10);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0x5b, rf_lvl_seq, 2);
	if (ret)
		return ret;

	usleep_range(2000, 3000);

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x1a);
	if (ret)
		return ret;

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x15, rdata, 2);
	if (ret)
		return ret;

	if (rdata[0] || rdata[1])
		return -EINVAL;

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x11, rdata, 2);
	if (ret)
		return ret;

	*rf_lvl_db =
	    cxd2880_convert2s_complement((rdata[0] << 3) |
					 ((rdata[1] & 0xe0) >> 5), 11);

	*rf_lvl_db *= 125;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x00);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x10, 0x00);
	if (ret)
		return ret;

	if (tnr_dmd->rf_lvl_cmpstn)
		ret = tnr_dmd->rf_lvl_cmpstn(tnr_dmd, rf_lvl_db);

	return ret;
}

int cxd2880_tnrdmd_mon_rf_lvl_sub(struct cxd2880_tnrdmd *tnr_dmd,
				  int *rf_lvl_db)
{
	if (!tnr_dmd || !rf_lvl_db)
		return -EINVAL;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_MAIN)
		return -EINVAL;

	return cxd2880_tnrdmd_mon_rf_lvl(tnr_dmd->diver_sub, rf_lvl_db);
}

int cxd2880_tnrdmd_mon_internal_cpu_status(struct cxd2880_tnrdmd
					   *tnr_dmd, u16 *status)
{
	u8 data[2] = { 0 };
	int ret;

	if (!tnr_dmd || !status)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x1a);
	if (ret)
		return ret;
	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x15, data, 2);
	if (ret)
		return ret;

	*status = (data[0] << 8) | data[1];

	return 0;
}

int cxd2880_tnrdmd_mon_internal_cpu_status_sub(struct
					       cxd2880_tnrdmd
					       *tnr_dmd,
					       u16 *status)
{
	if (!tnr_dmd || !status)
		return -EINVAL;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_MAIN)
		return -EINVAL;

	return cxd2880_tnrdmd_mon_internal_cpu_status(tnr_dmd->diver_sub,
						      status);
}
