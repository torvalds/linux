// SPDX-License-Identifier: GPL-2.0
/*
 * cxd2880_tnrdmd_dvbt.c
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver
 * control functions for DVB-T
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#include <media/dvb_frontend.h>

#include "cxd2880_tnrdmd_dvbt.h"
#include "cxd2880_tnrdmd_dvbt_mon.h"

static const struct cxd2880_reg_value tune_dmd_setting_seq1[] = {
	{0x00, 0x00}, {0x31, 0x01},
};

static const struct cxd2880_reg_value tune_dmd_setting_seq2[] = {
	{0x00, 0x04}, {0x5c, 0xfb}, {0x00, 0x10}, {0xa4, 0x03},
	{0x00, 0x14}, {0xb0, 0x00}, {0x00, 0x25},
};

static const struct cxd2880_reg_value tune_dmd_setting_seq3[] = {
	{0x00, 0x12}, {0x44, 0x00},
};

static const struct cxd2880_reg_value tune_dmd_setting_seq4[] = {
	{0x00, 0x11}, {0x87, 0xd2},
};

static const struct cxd2880_reg_value tune_dmd_setting_seq5[] = {
	{0x00, 0x00}, {0xfd, 0x01},
};

static const struct cxd2880_reg_value sleep_dmd_setting_seq1[] = {
	{0x00, 0x04}, {0x5c, 0xd8}, {0x00, 0x10}, {0xa4, 0x00},
};

static const struct cxd2880_reg_value sleep_dmd_setting_seq2[] = {
	{0x00, 0x11}, {0x87, 0x04},
};

static int x_tune_dvbt_demod_setting(struct cxd2880_tnrdmd
				     *tnr_dmd,
				     enum cxd2880_dtv_bandwidth
				     bandwidth,
				     enum cxd2880_tnrdmd_clockmode
				     clk_mode)
{
	static const u8 clk_mode_ckffrq_a[2] = { 0x52, 0x49 };
	static const u8 clk_mode_ckffrq_b[2] = { 0x5d, 0x55 };
	static const u8 clk_mode_ckffrq_c[2] = { 0x60, 0x00 };
	static const u8 ratectl_margin[2] = { 0x01, 0xf0 };
	static const u8 maxclkcnt_a[3] = { 0x73, 0xca, 0x49 };
	static const u8 maxclkcnt_b[3] = { 0xc8, 0x13, 0xaa };
	static const u8 maxclkcnt_c[3] = { 0xdc, 0x6c, 0x00 };

	static const u8 bw8_nomi_ac[5] = { 0x15, 0x00, 0x00, 0x00, 0x00};
	static const u8 bw8_nomi_b[5] = { 0x14, 0x6a, 0xaa, 0xaa, 0xaa};
	static const u8 bw8_gtdofst_a[2] = { 0x01, 0x28 };
	static const u8 bw8_gtdofst_b[2] = { 0x11, 0x44 };
	static const u8 bw8_gtdofst_c[2] = { 0x15, 0x28 };
	static const u8 bw8_mrc_a[5] = { 0x30, 0x00, 0x00, 0x90, 0x00 };
	static const u8 bw8_mrc_b[5] = { 0x36, 0x71, 0x00, 0xa3, 0x55 };
	static const u8 bw8_mrc_c[5] = { 0x38, 0x00, 0x00, 0xa8, 0x00 };
	static const u8 bw8_notch[4] = { 0xb3, 0x00, 0x01, 0x02 };

	static const u8 bw7_nomi_ac[5] = { 0x18, 0x00, 0x00, 0x00, 0x00};
	static const u8 bw7_nomi_b[5] = { 0x17, 0x55, 0x55, 0x55, 0x55};
	static const u8 bw7_gtdofst_a[2] = { 0x12, 0x4c };
	static const u8 bw7_gtdofst_b[2] = { 0x1f, 0x15 };
	static const u8 bw7_gtdofst_c[2] = { 0x1f, 0xf8 };
	static const u8 bw7_mrc_a[5] = { 0x36, 0xdb, 0x00, 0xa4, 0x92 };
	static const u8 bw7_mrc_b[5] = { 0x3e, 0x38, 0x00, 0xba, 0xaa };
	static const u8 bw7_mrc_c[5] = { 0x40, 0x00, 0x00, 0xc0, 0x00 };
	static const u8 bw7_notch[4] = { 0xb8, 0x00, 0x00, 0x03 };

	static const u8 bw6_nomi_ac[5] = { 0x1c, 0x00, 0x00, 0x00, 0x00};
	static const u8 bw6_nomi_b[5] = { 0x1b, 0x38, 0xe3, 0x8e, 0x38};
	static const u8 bw6_gtdofst_a[2] = { 0x1f, 0xf8 };
	static const u8 bw6_gtdofst_b[2] = { 0x24, 0x43 };
	static const u8 bw6_gtdofst_c[2] = { 0x25, 0x4c };
	static const u8 bw6_mrc_a[5] = { 0x40, 0x00, 0x00, 0xc0, 0x00 };
	static const u8 bw6_mrc_b[5] = { 0x48, 0x97, 0x00, 0xd9, 0xc7 };
	static const u8 bw6_mrc_c[5] = { 0x4a, 0xaa, 0x00, 0xdf, 0xff };
	static const u8 bw6_notch[4] = { 0xbe, 0xab, 0x00, 0x03 };

	static const u8 bw5_nomi_ac[5] = { 0x21, 0x99, 0x99, 0x99, 0x99};
	static const u8 bw5_nomi_b[5] = { 0x20, 0xaa, 0xaa, 0xaa, 0xaa};
	static const u8 bw5_gtdofst_a[2] = { 0x26, 0x5d };
	static const u8 bw5_gtdofst_b[2] = { 0x2b, 0x84 };
	static const u8 bw5_gtdofst_c[2] = { 0x2c, 0xc2 };
	static const u8 bw5_mrc_a[5] = { 0x4c, 0xcc, 0x00, 0xe6, 0x66 };
	static const u8 bw5_mrc_b[5] = { 0x57, 0x1c, 0x01, 0x05, 0x55 };
	static const u8 bw5_mrc_c[5] = { 0x59, 0x99, 0x01, 0x0c, 0xcc };
	static const u8 bw5_notch[4] = { 0xc8, 0x01, 0x00, 0x03 };
	const u8 *data = NULL;
	u8 sst_data;
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  tune_dmd_setting_seq1,
					  ARRAY_SIZE(tune_dmd_setting_seq1));
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x04);
	if (ret)
		return ret;

	switch (clk_mode) {
	case CXD2880_TNRDMD_CLOCKMODE_A:
		data = clk_mode_ckffrq_a;
		break;
	case CXD2880_TNRDMD_CLOCKMODE_B:
		data = clk_mode_ckffrq_b;
		break;
	case CXD2880_TNRDMD_CLOCKMODE_C:
		data = clk_mode_ckffrq_c;
		break;
	default:
		return -EINVAL;
	}

	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0x65, data, 2);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x5d, 0x07);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_SUB) {
		u8 data[2] = { 0x01, 0x01 };

		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x00, 0x00);
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0xce, data, 2);
		if (ret)
			return ret;
	}

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_DMD,
					  tune_dmd_setting_seq2,
					  ARRAY_SIZE(tune_dmd_setting_seq2));
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0xf0, ratectl_margin, 2);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN ||
	    tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB) {
		ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
						  CXD2880_IO_TGT_DMD,
						  tune_dmd_setting_seq3,
						  ARRAY_SIZE(tune_dmd_setting_seq3));
		if (ret)
			return ret;
	}

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB) {
		ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
						  CXD2880_IO_TGT_DMD,
						  tune_dmd_setting_seq4,
						  ARRAY_SIZE(tune_dmd_setting_seq4));
		if (ret)
			return ret;
	}

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_SUB) {
		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x00, 0x04);
		if (ret)
			return ret;

		switch (clk_mode) {
		case CXD2880_TNRDMD_CLOCKMODE_A:
			data = maxclkcnt_a;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_B:
			data = maxclkcnt_b;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_C:
			data = maxclkcnt_c;
			break;
		default:
			return -EINVAL;
		}

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x68, data, 3);
		if (ret)
			return ret;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x04);
	if (ret)
		return ret;

	switch (bandwidth) {
	case CXD2880_DTV_BW_8_MHZ:
		switch (clk_mode) {
		case CXD2880_TNRDMD_CLOCKMODE_A:
		case CXD2880_TNRDMD_CLOCKMODE_C:
			data = bw8_nomi_ac;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_B:
			data = bw8_nomi_b;
			break;
		default:
			return -EINVAL;
		}

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x60, data, 5);
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x4a, 0x00);
		if (ret)
			return ret;

		switch (clk_mode) {
		case CXD2880_TNRDMD_CLOCKMODE_A:
			data = bw8_gtdofst_a;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_B:
			data = bw8_gtdofst_b;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_C:
			data = bw8_gtdofst_c;
			break;
		default:
			return -EINVAL;
		}

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x7d, data, 2);
		if (ret)
			return ret;

		switch (clk_mode) {
		case CXD2880_TNRDMD_CLOCKMODE_A:
		case CXD2880_TNRDMD_CLOCKMODE_B:
			sst_data = 0x35;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_C:
			sst_data = 0x34;
			break;
		default:
			return -EINVAL;
		}

		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x71, sst_data);
		if (ret)
			return ret;

		if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
			switch (clk_mode) {
			case CXD2880_TNRDMD_CLOCKMODE_A:
				data = bw8_mrc_a;
				break;
			case CXD2880_TNRDMD_CLOCKMODE_B:
				data = bw8_mrc_b;
				break;
			case CXD2880_TNRDMD_CLOCKMODE_C:
				data = bw8_mrc_c;
				break;
			default:
				return -EINVAL;
			}

			ret = tnr_dmd->io->write_regs(tnr_dmd->io,
						      CXD2880_IO_TGT_DMD,
						      0x4b, &data[0], 2);
			if (ret)
				return ret;

			ret = tnr_dmd->io->write_regs(tnr_dmd->io,
						      CXD2880_IO_TGT_DMD,
						      0x51, &data[2], 3);
			if (ret)
				return ret;
		}

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x72, &bw8_notch[0], 2);
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x6b, &bw8_notch[2], 2);
		if (ret)
			return ret;
		break;

	case CXD2880_DTV_BW_7_MHZ:
		switch (clk_mode) {
		case CXD2880_TNRDMD_CLOCKMODE_A:
		case CXD2880_TNRDMD_CLOCKMODE_C:
			data = bw7_nomi_ac;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_B:
			data = bw7_nomi_b;
			break;
		default:
			return -EINVAL;
		}

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x60, data, 5);
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x4a, 0x02);
		if (ret)
			return ret;

		switch (clk_mode) {
		case CXD2880_TNRDMD_CLOCKMODE_A:
			data = bw7_gtdofst_a;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_B:
			data = bw7_gtdofst_b;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_C:
			data = bw7_gtdofst_c;
			break;
		default:
			return -EINVAL;
		}

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x7d, data, 2);
		if (ret)
			return ret;

		switch (clk_mode) {
		case CXD2880_TNRDMD_CLOCKMODE_A:
		case CXD2880_TNRDMD_CLOCKMODE_B:
			sst_data = 0x2f;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_C:
			sst_data = 0x2e;
			break;
		default:
			return -EINVAL;
		}

		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x71, sst_data);
		if (ret)
			return ret;

		if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
			switch (clk_mode) {
			case CXD2880_TNRDMD_CLOCKMODE_A:
				data = bw7_mrc_a;
				break;
			case CXD2880_TNRDMD_CLOCKMODE_B:
				data = bw7_mrc_b;
				break;
			case CXD2880_TNRDMD_CLOCKMODE_C:
				data = bw7_mrc_c;
				break;
			default:
				return -EINVAL;
			}

			ret = tnr_dmd->io->write_regs(tnr_dmd->io,
						      CXD2880_IO_TGT_DMD,
						      0x4b, &data[0], 2);
			if (ret)
				return ret;

			ret = tnr_dmd->io->write_regs(tnr_dmd->io,
						      CXD2880_IO_TGT_DMD,
						      0x51, &data[2], 3);
			if (ret)
				return ret;
		}

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x72, &bw7_notch[0], 2);
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x6b, &bw7_notch[2], 2);
		if (ret)
			return ret;
		break;

	case CXD2880_DTV_BW_6_MHZ:
		switch (clk_mode) {
		case CXD2880_TNRDMD_CLOCKMODE_A:
		case CXD2880_TNRDMD_CLOCKMODE_C:
			data = bw6_nomi_ac;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_B:
			data = bw6_nomi_b;
			break;
		default:
			return -EINVAL;
		}

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x60, data, 5);
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x4a, 0x04);
		if (ret)
			return ret;

		switch (clk_mode) {
		case CXD2880_TNRDMD_CLOCKMODE_A:
			data = bw6_gtdofst_a;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_B:
			data = bw6_gtdofst_b;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_C:
			data = bw6_gtdofst_c;
			break;
		default:
			return -EINVAL;
		}

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x7d, data, 2);
		if (ret)
			return ret;

		switch (clk_mode) {
		case CXD2880_TNRDMD_CLOCKMODE_A:
		case CXD2880_TNRDMD_CLOCKMODE_C:
			sst_data = 0x29;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_B:
			sst_data = 0x2a;
			break;
		default:
			return -EINVAL;
		}

		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x71, sst_data);
		if (ret)
			return ret;

		if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
			switch (clk_mode) {
			case CXD2880_TNRDMD_CLOCKMODE_A:
				data = bw6_mrc_a;
				break;
			case CXD2880_TNRDMD_CLOCKMODE_B:
				data = bw6_mrc_b;
				break;
			case CXD2880_TNRDMD_CLOCKMODE_C:
				data = bw6_mrc_c;
				break;
			default:
				return -EINVAL;
			}

			ret = tnr_dmd->io->write_regs(tnr_dmd->io,
						      CXD2880_IO_TGT_DMD,
						      0x4b, &data[0], 2);
			if (ret)
				return ret;

			ret = tnr_dmd->io->write_regs(tnr_dmd->io,
						      CXD2880_IO_TGT_DMD,
						      0x51, &data[2], 3);
			if (ret)
				return ret;
		}

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x72, &bw6_notch[0], 2);
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x6b, &bw6_notch[2], 2);
		if (ret)
			return ret;
		break;

	case CXD2880_DTV_BW_5_MHZ:
		switch (clk_mode) {
		case CXD2880_TNRDMD_CLOCKMODE_A:
		case CXD2880_TNRDMD_CLOCKMODE_C:
			data = bw5_nomi_ac;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_B:
			data = bw5_nomi_b;
			break;
		default:
			return -EINVAL;
		}

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x60, data, 5);
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x4a, 0x06);
		if (ret)
			return ret;

		switch (clk_mode) {
		case CXD2880_TNRDMD_CLOCKMODE_A:
			data = bw5_gtdofst_a;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_B:
			data = bw5_gtdofst_b;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_C:
			data = bw5_gtdofst_c;
			break;
		default:
			return -EINVAL;
		}

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x7d, data, 2);
		if (ret)
			return ret;

		switch (clk_mode) {
		case CXD2880_TNRDMD_CLOCKMODE_A:
		case CXD2880_TNRDMD_CLOCKMODE_B:
			sst_data = 0x24;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_C:
			sst_data = 0x23;
			break;
		default:
			return -EINVAL;
		}

		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x71, sst_data);
		if (ret)
			return ret;

		if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
			switch (clk_mode) {
			case CXD2880_TNRDMD_CLOCKMODE_A:
				data = bw5_mrc_a;
				break;
			case CXD2880_TNRDMD_CLOCKMODE_B:
				data = bw5_mrc_b;
				break;
			case CXD2880_TNRDMD_CLOCKMODE_C:
				data = bw5_mrc_c;
				break;
			default:
				return -EINVAL;
			}

			ret = tnr_dmd->io->write_regs(tnr_dmd->io,
						      CXD2880_IO_TGT_DMD,
						      0x4b, &data[0], 2);
			if (ret)
				return ret;

			ret = tnr_dmd->io->write_regs(tnr_dmd->io,
						      CXD2880_IO_TGT_DMD,
						      0x51, &data[2], 3);
			if (ret)
				return ret;
		}

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x72, &bw5_notch[0], 2);
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x6b, &bw5_notch[2], 2);
		if (ret)
			return ret;
		break;

	default:
		return -EINVAL;
	}

	return cxd2880_io_write_multi_regs(tnr_dmd->io,
					   CXD2880_IO_TGT_DMD,
					   tune_dmd_setting_seq5,
					   ARRAY_SIZE(tune_dmd_setting_seq5));
}

static int x_sleep_dvbt_demod_setting(struct cxd2880_tnrdmd
						   *tnr_dmd)
{
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_DMD,
					  sleep_dmd_setting_seq1,
					  ARRAY_SIZE(sleep_dmd_setting_seq1));
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
						  CXD2880_IO_TGT_DMD,
						  sleep_dmd_setting_seq2,
						  ARRAY_SIZE(sleep_dmd_setting_seq2));

	return ret;
}

static int dvbt_set_profile(struct cxd2880_tnrdmd *tnr_dmd,
			    enum cxd2880_dvbt_profile profile)
{
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x10);
	if (ret)
		return ret;

	return tnr_dmd->io->write_reg(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0x67,
				      (profile == CXD2880_DVBT_PROFILE_HP)
				      ? 0x00 : 0x01);
}

int cxd2880_tnrdmd_dvbt_tune1(struct cxd2880_tnrdmd *tnr_dmd,
			      struct cxd2880_dvbt_tune_param
			      *tune_param)
{
	int ret;

	if (!tnr_dmd || !tune_param)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP &&
	    tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	ret =
	    cxd2880_tnrdmd_common_tune_setting1(tnr_dmd, CXD2880_DTV_SYS_DVBT,
						tune_param->center_freq_khz,
						tune_param->bandwidth, 0, 0);
	if (ret)
		return ret;

	ret =
	    x_tune_dvbt_demod_setting(tnr_dmd, tune_param->bandwidth,
				      tnr_dmd->clk_mode);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		ret =
		    x_tune_dvbt_demod_setting(tnr_dmd->diver_sub,
					      tune_param->bandwidth,
					      tnr_dmd->diver_sub->clk_mode);
		if (ret)
			return ret;
	}

	return dvbt_set_profile(tnr_dmd, tune_param->profile);
}

int cxd2880_tnrdmd_dvbt_tune2(struct cxd2880_tnrdmd *tnr_dmd,
			      struct cxd2880_dvbt_tune_param
			      *tune_param)
{
	int ret;

	if (!tnr_dmd || !tune_param)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP &&
	    tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	ret =
	    cxd2880_tnrdmd_common_tune_setting2(tnr_dmd, CXD2880_DTV_SYS_DVBT,
						0);
	if (ret)
		return ret;

	tnr_dmd->state = CXD2880_TNRDMD_STATE_ACTIVE;
	tnr_dmd->frequency_khz = tune_param->center_freq_khz;
	tnr_dmd->sys = CXD2880_DTV_SYS_DVBT;
	tnr_dmd->bandwidth = tune_param->bandwidth;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		tnr_dmd->diver_sub->state = CXD2880_TNRDMD_STATE_ACTIVE;
		tnr_dmd->diver_sub->frequency_khz = tune_param->center_freq_khz;
		tnr_dmd->diver_sub->sys = CXD2880_DTV_SYS_DVBT;
		tnr_dmd->diver_sub->bandwidth = tune_param->bandwidth;
	}

	return 0;
}

int cxd2880_tnrdmd_dvbt_sleep_setting(struct cxd2880_tnrdmd *tnr_dmd)
{
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP &&
	    tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	ret = x_sleep_dvbt_demod_setting(tnr_dmd);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN)
		ret = x_sleep_dvbt_demod_setting(tnr_dmd->diver_sub);

	return ret;
}

int cxd2880_tnrdmd_dvbt_check_demod_lock(struct cxd2880_tnrdmd
					 *tnr_dmd,
					 enum
					 cxd2880_tnrdmd_lock_result
					 *lock)
{
	int ret;

	u8 sync_stat = 0;
	u8 ts_lock = 0;
	u8 unlock_detected = 0;
	u8 unlock_detected_sub = 0;

	if (!tnr_dmd || !lock)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	ret =
	    cxd2880_tnrdmd_dvbt_mon_sync_stat(tnr_dmd, &sync_stat, &ts_lock,
					      &unlock_detected);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SINGLE) {
		if (sync_stat == 6)
			*lock = CXD2880_TNRDMD_LOCK_RESULT_LOCKED;
		else if (unlock_detected)
			*lock = CXD2880_TNRDMD_LOCK_RESULT_UNLOCKED;
		else
			*lock = CXD2880_TNRDMD_LOCK_RESULT_NOTDETECT;

		return 0;
	}

	if (sync_stat == 6) {
		*lock = CXD2880_TNRDMD_LOCK_RESULT_LOCKED;
		return 0;
	}

	ret =
	    cxd2880_tnrdmd_dvbt_mon_sync_stat_sub(tnr_dmd, &sync_stat,
						  &unlock_detected_sub);
	if (ret)
		return ret;

	if (sync_stat == 6)
		*lock = CXD2880_TNRDMD_LOCK_RESULT_LOCKED;
	else if (unlock_detected && unlock_detected_sub)
		*lock = CXD2880_TNRDMD_LOCK_RESULT_UNLOCKED;
	else
		*lock = CXD2880_TNRDMD_LOCK_RESULT_NOTDETECT;

	return 0;
}

int cxd2880_tnrdmd_dvbt_check_ts_lock(struct cxd2880_tnrdmd
				      *tnr_dmd,
				      enum
				      cxd2880_tnrdmd_lock_result
				      *lock)
{
	int ret;

	u8 sync_stat = 0;
	u8 ts_lock = 0;
	u8 unlock_detected = 0;
	u8 unlock_detected_sub = 0;

	if (!tnr_dmd || !lock)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	ret =
	    cxd2880_tnrdmd_dvbt_mon_sync_stat(tnr_dmd, &sync_stat, &ts_lock,
					      &unlock_detected);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SINGLE) {
		if (ts_lock)
			*lock = CXD2880_TNRDMD_LOCK_RESULT_LOCKED;
		else if (unlock_detected)
			*lock = CXD2880_TNRDMD_LOCK_RESULT_UNLOCKED;
		else
			*lock = CXD2880_TNRDMD_LOCK_RESULT_NOTDETECT;

		return 0;
	}

	if (ts_lock) {
		*lock = CXD2880_TNRDMD_LOCK_RESULT_LOCKED;
		return 0;
	} else if (!unlock_detected) {
		*lock = CXD2880_TNRDMD_LOCK_RESULT_NOTDETECT;
		return 0;
	}

	ret =
	    cxd2880_tnrdmd_dvbt_mon_sync_stat_sub(tnr_dmd, &sync_stat,
						  &unlock_detected_sub);
	if (ret)
		return ret;

	if (unlock_detected && unlock_detected_sub)
		*lock = CXD2880_TNRDMD_LOCK_RESULT_UNLOCKED;
	else
		*lock = CXD2880_TNRDMD_LOCK_RESULT_NOTDETECT;

	return 0;
}
