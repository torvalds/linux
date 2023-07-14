// SPDX-License-Identifier: GPL-2.0
/*
 * cxd2880_tnrdmd_dvbt2.c
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver
 * control functions for DVB-T2
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#include <media/dvb_frontend.h>

#include "cxd2880_tnrdmd_dvbt2.h"
#include "cxd2880_tnrdmd_dvbt2_mon.h"

static const struct cxd2880_reg_value tune_dmd_setting_seq1[] = {
	{0x00, 0x00}, {0x31, 0x02},
};

static const struct cxd2880_reg_value tune_dmd_setting_seq2[] = {
	{0x00, 0x04}, {0x5d, 0x0b},
};

static int x_tune_dvbt2_demod_setting(struct cxd2880_tnrdmd
				      *tnr_dmd,
				      enum cxd2880_dtv_bandwidth
				      bandwidth,
				      enum cxd2880_tnrdmd_clockmode
				      clk_mode)
{
	static const u8 tsif_settings[2] = { 0x01, 0x01 };
	static const u8 init_settings[14] = {
		0x07, 0x06, 0x01, 0xf0,	0x00, 0x00, 0x04, 0xb0, 0x00, 0x00,
		0x09, 0x9c, 0x0e, 0x4c
	};
	static const u8 clk_mode_settings_a1[9] = {
		0x52, 0x49, 0x2c, 0x51,	0x51, 0x3d, 0x15, 0x29, 0x0c
	};

	static const u8 clk_mode_settings_b1[9] = {
		0x5d, 0x55, 0x32, 0x5c,	0x5c, 0x45, 0x17, 0x2e, 0x0d
	};

	static const u8 clk_mode_settings_c1[9] = {
		0x60, 0x00, 0x34, 0x5e,	0x5e, 0x47, 0x18, 0x2f, 0x0e
	};

	static const u8 clk_mode_settings_a2[13] = {
		0x04, 0xe7, 0x94, 0x92,	0x09, 0xcf, 0x7e, 0xd0, 0x49,
		0xcd, 0xcd, 0x1f, 0x5b
	};

	static const u8 clk_mode_settings_b2[13] = {
		0x05, 0x90, 0x27, 0x55,	0x0b, 0x20, 0x8f, 0xd6, 0xea,
		0xc8, 0xc8, 0x23, 0x91
	};

	static const u8 clk_mode_settings_c2[13] = {
		0x05, 0xb8, 0xd8, 0x00,	0x0b, 0x72, 0x93, 0xf3, 0x00,
		0xcd, 0xcd, 0x24, 0x95
	};

	static const u8 clk_mode_settings_a3[5] = {
		0x0b, 0x6a, 0xc9, 0x03, 0x33
	};
	static const u8 clk_mode_settings_b3[5] = {
		0x01, 0x02, 0xe4, 0x03, 0x39
	};
	static const u8 clk_mode_settings_c3[5] = {
		0x01, 0x02, 0xeb, 0x03, 0x3b
	};

	static const u8 gtdofst[2] = { 0x3f, 0xff };

	static const u8 bw8_gtdofst_a[2] = { 0x19, 0xd2 };
	static const u8 bw8_nomi_ac[6] = { 0x15, 0x00, 0x00, 0x00, 0x00, 0x00 };
	static const u8 bw8_nomi_b[6] = { 0x14, 0x6a, 0xaa, 0xaa, 0xab, 0x00 };
	static const u8 bw8_sst_a[2] = { 0x06, 0x2a };
	static const u8 bw8_sst_b[2] = { 0x06, 0x29 };
	static const u8 bw8_sst_c[2] = { 0x06, 0x28 };
	static const u8 bw8_mrc_a[9] = {
		0x28, 0x00, 0x50, 0x00, 0x60, 0x00, 0x00, 0x90, 0x00
	};
	static const u8 bw8_mrc_b[9] = {
		0x2d, 0x5e, 0x5a, 0xbd, 0x6c, 0xe3, 0x00, 0xa3, 0x55
	};
	static const u8 bw8_mrc_c[9] = {
		0x2e, 0xaa, 0x5d, 0x55, 0x70, 0x00, 0x00, 0xa8, 0x00
	};

	static const u8 bw7_nomi_ac[6] = { 0x18, 0x00, 0x00, 0x00, 0x00, 0x00 };
	static const u8 bw7_nomi_b[6] = { 0x17, 0x55, 0x55, 0x55, 0x55, 0x00 };
	static const u8 bw7_sst_a[2] = { 0x06, 0x23 };
	static const u8 bw7_sst_b[2] = { 0x06, 0x22 };
	static const u8 bw7_sst_c[2] = { 0x06, 0x21 };
	static const u8 bw7_mrc_a[9] = {
		0x2d, 0xb6, 0x5b, 0x6d,	0x6d, 0xb6, 0x00, 0xa4, 0x92
	};
	static const u8 bw7_mrc_b[9] = {
		0x33, 0xda, 0x67, 0xb4,	0x7c, 0x71, 0x00, 0xba, 0xaa
	};
	static const u8 bw7_mrc_c[9] = {
		0x35, 0x55, 0x6a, 0xaa,	0x80, 0x00, 0x00, 0xc0, 0x00
	};

	static const u8 bw6_nomi_ac[6] = { 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00 };
	static const u8 bw6_nomi_b[6] = { 0x1b, 0x38, 0xe3, 0x8e, 0x39, 0x00 };
	static const u8 bw6_sst_a[2] = { 0x06, 0x1c };
	static const u8 bw6_sst_b[2] = { 0x06, 0x1b };
	static const u8 bw6_sst_c[2] = { 0x06, 0x1a };
	static const u8 bw6_mrc_a[9] = {
		0x35, 0x55, 0x6a, 0xaa, 0x80, 0x00, 0x00, 0xc0, 0x00
	};
	static const u8 bw6_mrc_b[9] = {
		0x3c, 0x7e, 0x78, 0xfc,	0x91, 0x2f, 0x00, 0xd9, 0xc7
	};
	static const u8 bw6_mrc_c[9] = {
		0x3e, 0x38, 0x7c, 0x71,	0x95, 0x55, 0x00, 0xdf, 0xff
	};

	static const u8 bw5_nomi_ac[6] = { 0x21, 0x99, 0x99, 0x99, 0x9a, 0x00 };
	static const u8 bw5_nomi_b[6] = { 0x20, 0xaa, 0xaa, 0xaa, 0xab, 0x00 };
	static const u8 bw5_sst_a[2] = { 0x06, 0x15 };
	static const u8 bw5_sst_b[2] = { 0x06, 0x15 };
	static const u8 bw5_sst_c[2] = { 0x06, 0x14 };
	static const u8 bw5_mrc_a[9] = {
		0x40, 0x00, 0x6a, 0xaa, 0x80, 0x00, 0x00, 0xe6, 0x66
	};
	static const u8 bw5_mrc_b[9] = {
		0x48, 0x97, 0x78, 0xfc, 0x91, 0x2f, 0x01, 0x05, 0x55
	};
	static const u8 bw5_mrc_c[9] = {
		0x4a, 0xaa, 0x7c, 0x71, 0x95, 0x55, 0x01, 0x0c, 0xcc
	};

	static const u8 bw1_7_nomi_a[6] = {
		0x68, 0x0f, 0xa2, 0x32, 0xcf, 0x03
	};
	static const u8 bw1_7_nomi_c[6] = {
		0x68, 0x0f, 0xa2, 0x32, 0xcf, 0x03
	};
	static const u8 bw1_7_nomi_b[6] = {
		0x65, 0x2b, 0xa4, 0xcd, 0xd8, 0x03
	};
	static const u8 bw1_7_sst_a[2] = { 0x06, 0x0c };
	static const u8 bw1_7_sst_b[2] = { 0x06, 0x0c };
	static const u8 bw1_7_sst_c[2] = { 0x06, 0x0b };
	static const u8 bw1_7_mrc_a[9] = {
		0x40, 0x00, 0x6a, 0xaa,	0x80, 0x00, 0x02, 0xc9, 0x8f
	};
	static const u8 bw1_7_mrc_b[9] = {
		0x48, 0x97, 0x78, 0xfc, 0x91, 0x2f, 0x03, 0x29, 0x5d
	};
	static const u8 bw1_7_mrc_c[9] = {
		0x4a, 0xaa, 0x7c, 0x71,	0x95, 0x55, 0x03, 0x40, 0x7d
	};

	const u8 *data = NULL;
	const u8 *data2 = NULL;
	const u8 *data3 = NULL;
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  tune_dmd_setting_seq1,
					  ARRAY_SIZE(tune_dmd_setting_seq1));
	if (ret)
		return ret;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_DMD,
					  tune_dmd_setting_seq2,
					  ARRAY_SIZE(tune_dmd_setting_seq2));
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_SUB) {
		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x00, 0x00);
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0xce, tsif_settings, 2);
		if (ret)
			return ret;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x20);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x8a, init_settings[0]);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x90, init_settings[1]);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x25);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0xf0, &init_settings[2], 2);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x2a);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0xdc, init_settings[4]);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0xde, init_settings[5]);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x2d);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0x73, &init_settings[6], 4);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0x8f, &init_settings[10], 4);
	if (ret)
		return ret;

	switch (clk_mode) {
	case CXD2880_TNRDMD_CLOCKMODE_A:
		data = clk_mode_settings_a1;
		data2 = clk_mode_settings_a2;
		data3 = clk_mode_settings_a3;
		break;
	case CXD2880_TNRDMD_CLOCKMODE_B:
		data = clk_mode_settings_b1;
		data2 = clk_mode_settings_b2;
		data3 = clk_mode_settings_b3;
		break;
	case CXD2880_TNRDMD_CLOCKMODE_C:
		data = clk_mode_settings_c1;
		data2 = clk_mode_settings_c2;
		data3 = clk_mode_settings_c3;
		break;
	default:
		return -EINVAL;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x04);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0x1d, &data[0], 3);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x22, data[3]);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x24, data[4]);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x26, data[5]);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0x29, &data[6], 2);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x2d, data[8]);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_SUB) {
		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x2e, &data2[0], 6);
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x35, &data2[6], 7);
		if (ret)
			return ret;
	}

	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0x3c, &data3[0], 2);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0x56, &data3[2], 3);
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
					      0x10, data, 6);
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
		case CXD2880_TNRDMD_CLOCKMODE_C:
			data = gtdofst;
			break;
		default:
			return -EINVAL;
		}

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x19, data, 2);
		if (ret)
			return ret;

		switch (clk_mode) {
		case CXD2880_TNRDMD_CLOCKMODE_A:
			data = bw8_sst_a;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_B:
			data = bw8_sst_b;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_C:
			data = bw8_sst_c;
			break;
		default:
			return -EINVAL;
		}

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x1b, data, 2);
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
						      0x4b, data, 9);
			if (ret)
				return ret;
		}
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
					      0x10, data, 6);
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x4a, 0x02);
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x19, gtdofst, 2);
		if (ret)
			return ret;

		switch (clk_mode) {
		case CXD2880_TNRDMD_CLOCKMODE_A:
			data = bw7_sst_a;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_B:
			data = bw7_sst_b;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_C:
			data = bw7_sst_c;
			break;
		default:
			return -EINVAL;
		}

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x1b, data, 2);
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
						      0x4b, data, 9);
			if (ret)
				return ret;
		}
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
					      0x10, data, 6);
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x4a, 0x04);
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x19, gtdofst, 2);
		if (ret)
			return ret;

		switch (clk_mode) {
		case CXD2880_TNRDMD_CLOCKMODE_A:
			data = bw6_sst_a;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_B:
			data = bw6_sst_b;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_C:
			data = bw6_sst_c;
			break;
		default:
			return -EINVAL;
		}

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x1b, data, 2);
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
						      0x4b, data, 9);
			if (ret)
				return ret;
		}
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
					      0x10, data, 6);
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x4a, 0x06);
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x19, gtdofst, 2);
		if (ret)
			return ret;

		switch (clk_mode) {
		case CXD2880_TNRDMD_CLOCKMODE_A:
			data = bw5_sst_a;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_B:
			data = bw5_sst_b;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_C:
			data = bw5_sst_c;
			break;
		default:
			return -EINVAL;
		}

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x1b, data, 2);
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
						      0x4b, data, 9);
			if (ret)
				return ret;
		}
		break;

	case CXD2880_DTV_BW_1_7_MHZ:

		switch (clk_mode) {
		case CXD2880_TNRDMD_CLOCKMODE_A:
			data = bw1_7_nomi_a;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_C:
			data = bw1_7_nomi_c;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_B:
			data = bw1_7_nomi_b;
			break;
		default:
			return -EINVAL;
		}

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x10, data, 6);
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x4a, 0x03);
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x19, gtdofst, 2);
		if (ret)
			return ret;

		switch (clk_mode) {
		case CXD2880_TNRDMD_CLOCKMODE_A:
			data = bw1_7_sst_a;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_B:
			data = bw1_7_sst_b;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_C:
			data = bw1_7_sst_c;
			break;
		default:
			return -EINVAL;
		}

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x1b, data, 2);
		if (ret)
			return ret;

		if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
			switch (clk_mode) {
			case CXD2880_TNRDMD_CLOCKMODE_A:
				data = bw1_7_mrc_a;
				break;
			case CXD2880_TNRDMD_CLOCKMODE_B:
				data = bw1_7_mrc_b;
				break;
			case CXD2880_TNRDMD_CLOCKMODE_C:
				data = bw1_7_mrc_c;
				break;
			default:
				return -EINVAL;
			}

			ret = tnr_dmd->io->write_regs(tnr_dmd->io,
						      CXD2880_IO_TGT_DMD,
						      0x4b, data, 9);
			if (ret)
				return ret;
		}
		break;

	default:
		return -EINVAL;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x00);
	if (ret)
		return ret;

	return tnr_dmd->io->write_reg(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0xfd, 0x01);
}

static int x_sleep_dvbt2_demod_setting(struct cxd2880_tnrdmd
				       *tnr_dmd)
{
	static const u8 difint_clip[] = {
		0, 1, 0, 2, 0, 4, 0, 8, 0, 16, 0, 32
	};
	int ret = 0;

	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x00, 0x1d);
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x47, difint_clip, 12);
	}

	return ret;
}

static int dvbt2_set_profile(struct cxd2880_tnrdmd *tnr_dmd,
			     enum cxd2880_dvbt2_profile profile)
{
	u8 t2_mode_tune_mode = 0;
	u8 seq_not2_dtime = 0;
	u8 dtime1 = 0;
	u8 dtime2 = 0;
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	switch (tnr_dmd->clk_mode) {
	case CXD2880_TNRDMD_CLOCKMODE_A:
		dtime1 = 0x27;
		dtime2 = 0x0c;
		break;
	case CXD2880_TNRDMD_CLOCKMODE_B:
		dtime1 = 0x2c;
		dtime2 = 0x0d;
		break;
	case CXD2880_TNRDMD_CLOCKMODE_C:
		dtime1 = 0x2e;
		dtime2 = 0x0e;
		break;
	default:
		return -EINVAL;
	}

	switch (profile) {
	case CXD2880_DVBT2_PROFILE_BASE:
		t2_mode_tune_mode = 0x01;
		seq_not2_dtime = dtime2;
		break;

	case CXD2880_DVBT2_PROFILE_LITE:
		t2_mode_tune_mode = 0x05;
		seq_not2_dtime = dtime1;
		break;

	case CXD2880_DVBT2_PROFILE_ANY:
		t2_mode_tune_mode = 0x00;
		seq_not2_dtime = dtime1;
		break;

	default:
		return -EINVAL;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x2e);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x10, t2_mode_tune_mode);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x04);
	if (ret)
		return ret;

	return tnr_dmd->io->write_reg(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0x2c, seq_not2_dtime);
}

int cxd2880_tnrdmd_dvbt2_tune1(struct cxd2880_tnrdmd *tnr_dmd,
			       struct cxd2880_dvbt2_tune_param
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

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN &&
	    tune_param->profile == CXD2880_DVBT2_PROFILE_ANY)
		return -ENOTTY;

	ret =
	    cxd2880_tnrdmd_common_tune_setting1(tnr_dmd, CXD2880_DTV_SYS_DVBT2,
						tune_param->center_freq_khz,
						tune_param->bandwidth, 0, 0);
	if (ret)
		return ret;

	ret =
	    x_tune_dvbt2_demod_setting(tnr_dmd, tune_param->bandwidth,
				       tnr_dmd->clk_mode);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		ret =
		    x_tune_dvbt2_demod_setting(tnr_dmd->diver_sub,
					       tune_param->bandwidth,
					       tnr_dmd->diver_sub->clk_mode);
		if (ret)
			return ret;
	}

	ret = dvbt2_set_profile(tnr_dmd, tune_param->profile);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		ret =
		    dvbt2_set_profile(tnr_dmd->diver_sub, tune_param->profile);
		if (ret)
			return ret;
	}

	if (tune_param->data_plp_id == CXD2880_DVBT2_TUNE_PARAM_PLPID_AUTO)
		ret = cxd2880_tnrdmd_dvbt2_set_plp_cfg(tnr_dmd, 1, 0);
	else
		ret =
		    cxd2880_tnrdmd_dvbt2_set_plp_cfg(tnr_dmd, 0,
					     (u8)(tune_param->data_plp_id));

	return ret;
}

int cxd2880_tnrdmd_dvbt2_tune2(struct cxd2880_tnrdmd *tnr_dmd,
			       struct cxd2880_dvbt2_tune_param
			       *tune_param)
{
	u8 en_fef_intmtnt_ctrl = 1;
	int ret;

	if (!tnr_dmd || !tune_param)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP &&
	    tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	switch (tune_param->profile) {
	case CXD2880_DVBT2_PROFILE_BASE:
		en_fef_intmtnt_ctrl = tnr_dmd->en_fef_intmtnt_base;
		break;
	case CXD2880_DVBT2_PROFILE_LITE:
		en_fef_intmtnt_ctrl = tnr_dmd->en_fef_intmtnt_lite;
		break;
	case CXD2880_DVBT2_PROFILE_ANY:
		if (tnr_dmd->en_fef_intmtnt_base &&
		    tnr_dmd->en_fef_intmtnt_lite)
			en_fef_intmtnt_ctrl = 1;
		else
			en_fef_intmtnt_ctrl = 0;
		break;
	default:
		return -EINVAL;
	}

	ret =
	    cxd2880_tnrdmd_common_tune_setting2(tnr_dmd,
						CXD2880_DTV_SYS_DVBT2,
						en_fef_intmtnt_ctrl);
	if (ret)
		return ret;

	tnr_dmd->state = CXD2880_TNRDMD_STATE_ACTIVE;
	tnr_dmd->frequency_khz = tune_param->center_freq_khz;
	tnr_dmd->sys = CXD2880_DTV_SYS_DVBT2;
	tnr_dmd->bandwidth = tune_param->bandwidth;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		tnr_dmd->diver_sub->state = CXD2880_TNRDMD_STATE_ACTIVE;
		tnr_dmd->diver_sub->frequency_khz = tune_param->center_freq_khz;
		tnr_dmd->diver_sub->sys = CXD2880_DTV_SYS_DVBT2;
		tnr_dmd->diver_sub->bandwidth = tune_param->bandwidth;
	}

	return 0;
}

int cxd2880_tnrdmd_dvbt2_sleep_setting(struct cxd2880_tnrdmd
				       *tnr_dmd)
{
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP &&
	    tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	ret = x_sleep_dvbt2_demod_setting(tnr_dmd);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN)
		ret = x_sleep_dvbt2_demod_setting(tnr_dmd->diver_sub);

	return ret;
}

int cxd2880_tnrdmd_dvbt2_check_demod_lock(struct cxd2880_tnrdmd
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
	    cxd2880_tnrdmd_dvbt2_mon_sync_stat(tnr_dmd, &sync_stat, &ts_lock,
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
	    cxd2880_tnrdmd_dvbt2_mon_sync_stat_sub(tnr_dmd, &sync_stat,
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

int cxd2880_tnrdmd_dvbt2_check_ts_lock(struct cxd2880_tnrdmd
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
	    cxd2880_tnrdmd_dvbt2_mon_sync_stat(tnr_dmd, &sync_stat, &ts_lock,
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
	    cxd2880_tnrdmd_dvbt2_mon_sync_stat_sub(tnr_dmd, &sync_stat,
						   &unlock_detected_sub);
	if (ret)
		return ret;

	if (unlock_detected && unlock_detected_sub)
		*lock = CXD2880_TNRDMD_LOCK_RESULT_UNLOCKED;
	else
		*lock = CXD2880_TNRDMD_LOCK_RESULT_NOTDETECT;

	return 0;
}

int cxd2880_tnrdmd_dvbt2_set_plp_cfg(struct cxd2880_tnrdmd
				     *tnr_dmd, u8 auto_plp,
				     u8 plp_id)
{
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP &&
	    tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x23);
	if (ret)
		return ret;

	if (!auto_plp) {
		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0xaf, plp_id);
		if (ret)
			return ret;
	}

	return tnr_dmd->io->write_reg(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0xad, auto_plp ? 0x00 : 0x01);
}

int cxd2880_tnrdmd_dvbt2_diver_fef_setting(struct cxd2880_tnrdmd
					   *tnr_dmd)
{
	struct cxd2880_dvbt2_ofdm ofdm;
	static const u8 data[] = { 0, 8, 0, 16, 0, 32, 0, 64, 0, 128, 1, 0};
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SINGLE)
		return 0;

	ret = cxd2880_tnrdmd_dvbt2_mon_ofdm(tnr_dmd, &ofdm);
	if (ret)
		return ret;

	if (!ofdm.mixed)
		return 0;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x1d);
	if (ret)
		return ret;

	return tnr_dmd->io->write_regs(tnr_dmd->io,
				       CXD2880_IO_TGT_DMD,
				       0x47, data, 12);
}

int cxd2880_tnrdmd_dvbt2_check_l1post_valid(struct cxd2880_tnrdmd
					    *tnr_dmd,
					    u8 *l1_post_valid)
{
	int ret;

	u8 data;

	if (!tnr_dmd || !l1_post_valid)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP &&
	    tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x0b);
	if (ret)
		return ret;

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x86, &data, 1);
	if (ret)
		return ret;

	*l1_post_valid = data & 0x01;

	return ret;
}
