// SPDX-License-Identifier: GPL-2.0
/*
 * cxd2880_tnrdmd.c
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver
 * common control functions
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#include <media/dvb_frontend.h>
#include "cxd2880_common.h"
#include "cxd2880_tnrdmd.h"
#include "cxd2880_tnrdmd_mon.h"
#include "cxd2880_tnrdmd_dvbt.h"
#include "cxd2880_tnrdmd_dvbt2.h"

static const struct cxd2880_reg_value p_init1_seq[] = {
	{0x11, 0x16}, {0x00, 0x10},
};

static const struct cxd2880_reg_value rf_init1_seq1[] = {
	{0x4f, 0x18}, {0x61, 0x00}, {0x71, 0x00}, {0x9d, 0x01},
	{0x7d, 0x02}, {0x8f, 0x01}, {0x8b, 0xc6}, {0x9a, 0x03},
	{0x1c, 0x00},
};

static const struct cxd2880_reg_value rf_init1_seq2[] = {
	{0xb9, 0x07}, {0x33, 0x01}, {0xc1, 0x01}, {0xc4, 0x1e},
};

static const struct cxd2880_reg_value rf_init1_seq3[] = {
	{0x00, 0x10}, {0x51, 0x01}, {0xc5, 0x07}, {0x00, 0x11},
	{0x70, 0xe9}, {0x76, 0x0a}, {0x78, 0x32}, {0x7a, 0x46},
	{0x7c, 0x86}, {0x7e, 0xa4}, {0x00, 0x10}, {0xe1, 0x01},
};

static const struct cxd2880_reg_value rf_init1_seq4[] = {
	{0x15, 0x00}, {0x00, 0x16}
};

static const struct cxd2880_reg_value rf_init1_seq5[] = {
	{0x00, 0x00}, {0x25, 0x00}
};

static const struct cxd2880_reg_value rf_init1_seq6[] = {
	{0x02, 0x00}, {0x00, 0x00}, {0x21, 0x01}, {0x00, 0xe1},
	{0x8f, 0x16}, {0x67, 0x60}, {0x6a, 0x0f}, {0x6c, 0x17}
};

static const struct cxd2880_reg_value rf_init1_seq7[] = {
	{0x00, 0xe2}, {0x41, 0xa0}, {0x4b, 0x68}, {0x00, 0x00},
	{0x21, 0x00}, {0x10, 0x01},
};

static const struct cxd2880_reg_value rf_init1_seq8[] = {
	{0x00, 0x10}, {0x25, 0x01},
};

static const struct cxd2880_reg_value rf_init1_seq9[] = {
	{0x00, 0x10}, {0x14, 0x01}, {0x00, 0x00}, {0x26, 0x00},
};

static const struct cxd2880_reg_value rf_init2_seq1[] = {
	{0x00, 0x14}, {0x1b, 0x01},
};

static const struct cxd2880_reg_value rf_init2_seq2[] = {
	{0x00, 0x00}, {0x21, 0x01}, {0x00, 0xe1}, {0xd3, 0x00},
	{0x00, 0x00}, {0x21, 0x00},
};

static const struct cxd2880_reg_value x_tune1_seq1[] = {
	{0x00, 0x00}, {0x10, 0x01},
};

static const struct cxd2880_reg_value x_tune1_seq2[] = {
	{0x62, 0x00}, {0x00, 0x15},
};

static const struct cxd2880_reg_value x_tune2_seq1[] = {
	{0x00, 0x1a}, {0x29, 0x01},
};

static const struct cxd2880_reg_value x_tune2_seq2[] = {
	{0x62, 0x01}, {0x00, 0x11}, {0x2d, 0x00}, {0x2f, 0x00},
};

static const struct cxd2880_reg_value x_tune2_seq3[] = {
	{0x00, 0x00}, {0x10, 0x00}, {0x21, 0x01},
};

static const struct cxd2880_reg_value x_tune2_seq4[] = {
	{0x00, 0xe1}, {0x8a, 0x87},
};

static const struct cxd2880_reg_value x_tune2_seq5[] = {
	{0x00, 0x00}, {0x21, 0x00},
};

static const struct cxd2880_reg_value x_tune3_seq[] = {
	{0x00, 0x00}, {0x21, 0x01}, {0x00, 0xe2}, {0x41, 0xa0},
	{0x00, 0x00}, {0x21, 0x00}, {0xfe, 0x01},
};

static const struct cxd2880_reg_value x_tune4_seq[] = {
	{0x00, 0x00}, {0xfe, 0x01},
};

static const struct cxd2880_reg_value x_sleep1_seq[] = {
	{0x00, 0x00}, {0x57, 0x03},
};

static const struct cxd2880_reg_value x_sleep2_seq1[] = {
	{0x00, 0x2d}, {0xb1, 0x01},
};

static const struct cxd2880_reg_value x_sleep2_seq2[] = {
	{0x00, 0x10}, {0xf4, 0x00}, {0xf3, 0x00}, {0xf2, 0x00},
	{0xf1, 0x00}, {0xf0, 0x00}, {0xef, 0x00},
};

static const struct cxd2880_reg_value x_sleep3_seq[] = {
	{0x00, 0x00}, {0xfd, 0x00},
};

static const struct cxd2880_reg_value x_sleep4_seq[] = {
	{0x00, 0x00}, {0x21, 0x01}, {0x00, 0xe2}, {0x41, 0x00},
	{0x00, 0x00}, {0x21, 0x00},
};

static const struct cxd2880_reg_value spll_reset_seq1[] = {
	{0x00, 0x10}, {0x29, 0x01}, {0x28, 0x01}, {0x27, 0x01},
	{0x26, 0x01},
};

static const struct cxd2880_reg_value spll_reset_seq2[] = {
	{0x00, 0x00}, {0x10, 0x00},
};

static const struct cxd2880_reg_value spll_reset_seq3[] = {
	{0x00, 0x00}, {0x27, 0x00}, {0x22, 0x01},
};

static const struct cxd2880_reg_value spll_reset_seq4[] = {
	{0x00, 0x00}, {0x27, 0x01},
};

static const struct cxd2880_reg_value spll_reset_seq5[] = {
	{0x00, 0x00}, {0x10, 0x01},
};

static const struct cxd2880_reg_value t_power_x_seq1[] = {
	{0x00, 0x10}, {0x29, 0x01}, {0x28, 0x01}, {0x27, 0x01},
};

static const struct cxd2880_reg_value t_power_x_seq2[] = {
	{0x00, 0x00}, {0x10, 0x00},
};

static const struct cxd2880_reg_value t_power_x_seq3[] = {
	{0x00, 0x00}, {0x27, 0x00}, {0x25, 0x01},
};

static const struct cxd2880_reg_value t_power_x_seq4[] = {
	{0x00, 0x00}, {0x2a, 0x00},
};

static const struct cxd2880_reg_value t_power_x_seq5[] = {
	{0x00, 0x00}, {0x25, 0x00},
};

static const struct cxd2880_reg_value t_power_x_seq6[] = {
	{0x00, 0x00}, {0x27, 0x01},
};

static const struct cxd2880_reg_value t_power_x_seq7[] = {
	{0x00, 0x00}, {0x10, 0x01},
};

static const struct cxd2880_reg_value set_ts_pin_seq[] = {
	{0x50, 0x3f}, {0x52, 0x1f},

};

static const struct cxd2880_reg_value set_ts_output_seq1[] = {
	{0x00, 0x00}, {0x52, 0x00},
};

static const struct cxd2880_reg_value set_ts_output_seq2[] = {
	{0x00, 0x00}, {0xc3, 0x00},

};

static const struct cxd2880_reg_value set_ts_output_seq3[] = {
	{0x00, 0x00}, {0xc3, 0x01},

};

static const struct cxd2880_reg_value set_ts_output_seq4[] = {
	{0x00, 0x00}, {0x52, 0x1f},

};

static int p_init1(struct cxd2880_tnrdmd *tnr_dmd)
{
	u8 data = 0;
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x00);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SINGLE ||
	    tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		switch (tnr_dmd->create_param.ts_output_if) {
		case CXD2880_TNRDMD_TSOUT_IF_TS:
			data = 0x00;
			break;
		case CXD2880_TNRDMD_TSOUT_IF_SPI:
			data = 0x01;
			break;
		case CXD2880_TNRDMD_TSOUT_IF_SDIO:
			data = 0x02;
			break;
		default:
			return -EINVAL;
		}
		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_SYS,
					     0x10, data);
		if (ret)
			return ret;
	}

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  p_init1_seq,
					  ARRAY_SIZE(p_init1_seq));
	if (ret)
		return ret;

	switch (tnr_dmd->chip_id) {
	case CXD2880_TNRDMD_CHIP_ID_CXD2880_ES1_0X:
		data = 0x1a;
		break;
	case CXD2880_TNRDMD_CHIP_ID_CXD2880_ES1_11:
		data = 0x16;
		break;
	default:
		return -ENOTTY;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x10, data);
	if (ret)
		return ret;

	if (tnr_dmd->create_param.en_internal_ldo)
		data = 0x01;
	else
		data = 0x00;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x11, data);
	if (ret)
		return ret;
	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x13, data);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x00);
	if (ret)
		return ret;
	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x12, data);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x10);
	if (ret)
		return ret;

	switch (tnr_dmd->chip_id) {
	case CXD2880_TNRDMD_CHIP_ID_CXD2880_ES1_0X:
		data = 0x01;
		break;
	case CXD2880_TNRDMD_CHIP_ID_CXD2880_ES1_11:
		data = 0x00;
		break;
	default:
		return -ENOTTY;
	}

	return tnr_dmd->io->write_reg(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0x69, data);
}

static int p_init2(struct cxd2880_tnrdmd *tnr_dmd)
{
	u8 data[6] = { 0 };
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x00);
	if (ret)
		return ret;
	data[0] = tnr_dmd->create_param.xosc_cap;
	data[1] = tnr_dmd->create_param.xosc_i;
	switch (tnr_dmd->create_param.xtal_share_type) {
	case CXD2880_TNRDMD_XTAL_SHARE_NONE:
		data[2] = 0x01;
		data[3] = 0x00;
		break;
	case CXD2880_TNRDMD_XTAL_SHARE_EXTREF:
		data[2] = 0x00;
		data[3] = 0x00;
		break;
	case CXD2880_TNRDMD_XTAL_SHARE_MASTER:
		data[2] = 0x01;
		data[3] = 0x01;
		break;
	case CXD2880_TNRDMD_XTAL_SHARE_SLAVE:
		data[2] = 0x00;
		data[3] = 0x01;
		break;
	default:
		return -EINVAL;
	}
	data[4] = 0x06;
	data[5] = 0x00;

	return tnr_dmd->io->write_regs(tnr_dmd->io,
				       CXD2880_IO_TGT_SYS,
				       0x13, data, 6);
}

static int p_init3(struct cxd2880_tnrdmd *tnr_dmd)
{
	u8 data[2] = { 0 };
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x00);
	if (ret)
		return ret;

	switch (tnr_dmd->diver_mode) {
	case CXD2880_TNRDMD_DIVERMODE_SINGLE:
		data[0] = 0x00;
		break;
	case CXD2880_TNRDMD_DIVERMODE_MAIN:
		data[0] = 0x03;
		break;
	case CXD2880_TNRDMD_DIVERMODE_SUB:
		data[0] = 0x02;
		break;
	default:
		return -EINVAL;
	}

	data[1] = 0x01;

	return tnr_dmd->io->write_regs(tnr_dmd->io,
				       CXD2880_IO_TGT_SYS,
				       0x1f, data, 2);
}

static int rf_init1(struct cxd2880_tnrdmd *tnr_dmd)
{
	u8 data[8] = { 0 };
	static const u8 rf_init1_cdata1[40] = {
		0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
		0x05, 0x05, 0x04, 0x04, 0x04, 0x03, 0x03,
		0x03, 0x04, 0x04, 0x05, 0x05, 0x05, 0x02,
		0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
		0x02, 0x03, 0x02, 0x01, 0x01, 0x01, 0x02,
		0x02, 0x03, 0x04, 0x04, 0x04
	};

	static const u8 rf_init1_cdata2[5] = {0xff, 0x00, 0x00, 0x00, 0x00};
	static const u8 rf_init1_cdata3[80] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
		0x01, 0x00, 0x02, 0x00, 0x63, 0x00, 0x00,
		0x00, 0x03, 0x00, 0x04, 0x00, 0x04, 0x00,
		0x06, 0x00, 0x06, 0x00, 0x08, 0x00, 0x09,
		0x00, 0x0b, 0x00, 0x0b, 0x00, 0x0d, 0x00,
		0x0d, 0x00, 0x0f, 0x00, 0x0f, 0x00, 0x0f,
		0x00, 0x10, 0x00, 0x79, 0x00, 0x00, 0x00,
		0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01,
		0x00, 0x03, 0x00, 0x03, 0x00, 0x03, 0x00,
		0x04, 0x00, 0x04, 0x00, 0x06, 0x00, 0x05,
		0x00, 0x07, 0x00, 0x07, 0x00, 0x08, 0x00,
		0x0a, 0x03, 0xe0
	};

	static const u8 rf_init1_cdata4[8] = {
		0x20, 0x20, 0x30, 0x41, 0x50, 0x5f, 0x6f, 0x80
	};

	static const u8 rf_init1_cdata5[50] = {
		0x00, 0x09, 0x00, 0x08, 0x00, 0x07, 0x00,
		0x06, 0x00, 0x05, 0x00, 0x03, 0x00, 0x02,
		0x00, 0x00, 0x00, 0x78, 0x00, 0x00, 0x00,
		0x06, 0x00, 0x08, 0x00, 0x08, 0x00, 0x0c,
		0x00, 0x0c, 0x00, 0x0d, 0x00, 0x0f, 0x00,
		0x0e, 0x00, 0x0e, 0x00, 0x10, 0x00, 0x0f,
		0x00, 0x0e, 0x00, 0x10, 0x00, 0x0f, 0x00,
		0x0e
	};

	u8 addr = 0;
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x00);
	if (ret)
		return ret;
	data[0] = 0x01;
	data[1] = 0x00;
	data[2] = 0x01;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0x21, data, 3);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x10);
	if (ret)
		return ret;
	data[0] = 0x01;
	data[1] = 0x01;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0x17, data, 2);
	if (ret)
		return ret;

	if (tnr_dmd->create_param.stationary_use) {
		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_SYS,
					     0x1a, 0x06);
		if (ret)
			return ret;
	}

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  rf_init1_seq1,
					  ARRAY_SIZE(rf_init1_seq1));
	if (ret)
		return ret;

	data[0] = 0x00;
	if (tnr_dmd->create_param.is_cxd2881gg &&
	    tnr_dmd->create_param.xtal_share_type ==
		CXD2880_TNRDMD_XTAL_SHARE_SLAVE)
		data[1] = 0x00;
	else
		data[1] = 0x1f;
	data[2] = 0x0a;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0xb5, data, 3);
	if (ret)
		return ret;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  rf_init1_seq2,
					  ARRAY_SIZE(rf_init1_seq2));
	if (ret)
		return ret;

	if (tnr_dmd->chip_id == CXD2880_TNRDMD_CHIP_ID_CXD2880_ES1_0X) {
		data[0] = 0x34;
		data[1] = 0x2c;
	} else {
		data[0] = 0x2f;
		data[1] = 0x25;
	}
	data[2] = 0x15;
	data[3] = 0x19;
	data[4] = 0x1b;
	data[5] = 0x15;
	data[6] = 0x19;
	data[7] = 0x1b;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0xd9, data, 8);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x11);
	if (ret)
		return ret;
	data[0] = 0x6c;
	data[1] = 0x10;
	data[2] = 0xa6;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0x44, data, 3);
	if (ret)
		return ret;
	data[0] = 0x16;
	data[1] = 0xa8;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0x50, data, 2);
	if (ret)
		return ret;
	data[0] = 0x00;
	data[1] = 0x22;
	data[2] = 0x00;
	data[3] = 0x88;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0x62, data, 4);
	if (ret)
		return ret;
	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x74, 0x75);
	if (ret)
		return ret;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0x7f, rf_init1_cdata1, 40);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x16);
	if (ret)
		return ret;
	data[0] = 0x00;
	data[1] = 0x71;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0x10, data, 2);
	if (ret)
		return ret;
	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x23, 0x89);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0x27, rf_init1_cdata2, 5);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0x3a, rf_init1_cdata3, 80);
	if (ret)
		return ret;

	data[0] = 0x03;
	data[1] = 0xe0;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0xbc, data, 2);
	if (ret)
		return ret;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  rf_init1_seq3,
					  ARRAY_SIZE(rf_init1_seq3));
	if (ret)
		return ret;

	if (tnr_dmd->create_param.stationary_use) {
		data[0] = 0x06;
		data[1] = 0x07;
		data[2] = 0x1a;
	} else {
		data[0] = 0x00;
		data[1] = 0x08;
		data[2] = 0x19;
	}
	data[3] = 0x0e;
	data[4] = 0x09;
	data[5] = 0x0e;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x12);
	if (ret)
		return ret;
	for (addr = 0x10; addr < 0x9f; addr += 6) {
		if (tnr_dmd->lna_thrs_tbl_air) {
			u8 idx = 0;

			idx = (addr - 0x10) / 6;
			data[0] =
			    tnr_dmd->lna_thrs_tbl_air->thrs[idx].off_on;
			data[1] =
			    tnr_dmd->lna_thrs_tbl_air->thrs[idx].on_off;
		}
		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_SYS,
					      addr, data, 6);
		if (ret)
			return ret;
	}

	data[0] = 0x00;
	data[1] = 0x08;
	if (tnr_dmd->create_param.stationary_use)
		data[2] = 0x1a;
	else
		data[2] = 0x19;
	data[3] = 0x0e;
	data[4] = 0x09;
	data[5] = 0x0e;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x13);
	if (ret)
		return ret;
	for (addr = 0x10; addr < 0xcf; addr += 6) {
		if (tnr_dmd->lna_thrs_tbl_cable) {
			u8 idx = 0;

			idx = (addr - 0x10) / 6;
			data[0] =
			    tnr_dmd->lna_thrs_tbl_cable->thrs[idx].off_on;
			data[1] =
			    tnr_dmd->lna_thrs_tbl_cable->thrs[idx].on_off;
		}
		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_SYS,
					      addr, data, 6);
		if (ret)
			return ret;
	}

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x11);
	if (ret)
		return ret;
	data[0] = 0x08;
	data[1] = 0x09;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0xbd, data, 2);
	if (ret)
		return ret;
	data[0] = 0x08;
	data[1] = 0x09;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0xc4, data, 2);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0xc9, rf_init1_cdata4, 8);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x14);
	if (ret)
		return ret;
	data[0] = 0x15;
	data[1] = 0x18;
	data[2] = 0x00;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0x10, data, 3);
	if (ret)
		return ret;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  rf_init1_seq4,
					  ARRAY_SIZE(rf_init1_seq4));
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0x12, rf_init1_cdata5, 50);
	if (ret)
		return ret;

	usleep_range(1000, 2000);

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x0a);
	if (ret)
		return ret;
	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x10, data, 1);
	if (ret)
		return ret;
	if ((data[0] & 0x01) == 0x00)
		return -EINVAL;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  rf_init1_seq5,
					  ARRAY_SIZE(rf_init1_seq5));
	if (ret)
		return ret;

	usleep_range(1000, 2000);

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x0a);
	if (ret)
		return ret;
	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x11, data, 1);
	if (ret)
		return ret;
	if ((data[0] & 0x01) == 0x00)
		return -EINVAL;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_DMD,
					  rf_init1_seq6,
					  ARRAY_SIZE(rf_init1_seq6));
	if (ret)
		return ret;

	data[0] = 0x00;
	data[1] = 0xfe;
	data[2] = 0xee;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0x6e, data, 3);
	if (ret)
		return ret;
	data[0] = 0xa1;
	data[1] = 0x8b;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0x8d, data, 2);
	if (ret)
		return ret;
	data[0] = 0x08;
	data[1] = 0x09;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0x77, data, 2);
	if (ret)
		return ret;

	if (tnr_dmd->create_param.stationary_use) {
		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x80, 0xaa);
		if (ret)
			return ret;
	}

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_DMD,
					  rf_init1_seq7,
					  ARRAY_SIZE(rf_init1_seq7));
	if (ret)
		return ret;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  rf_init1_seq8,
					  ARRAY_SIZE(rf_init1_seq8));
	if (ret)
		return ret;

	usleep_range(1000, 2000);

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x1a);
	if (ret)
		return ret;
	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x10, data, 1);
	if (ret)
		return ret;
	if ((data[0] & 0x01) == 0x00)
		return -EINVAL;

	return cxd2880_io_write_multi_regs(tnr_dmd->io,
					   CXD2880_IO_TGT_SYS,
					   rf_init1_seq9,
					   ARRAY_SIZE(rf_init1_seq9));
}

static int rf_init2(struct cxd2880_tnrdmd *tnr_dmd)
{
	u8 data[5] = { 0 };
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x10);
	if (ret)
		return ret;
	data[0] = 0x40;
	data[1] = 0x40;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0xea, data, 2);
	if (ret)
		return ret;

	usleep_range(1000, 2000);

	data[0] = 0x00;
	if (tnr_dmd->chip_id == CXD2880_TNRDMD_CHIP_ID_CXD2880_ES1_0X)
		data[1] = 0x00;
	else
		data[1] = 0x01;
	data[2] = 0x01;
	data[3] = 0x03;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0x30, data, 4);
	if (ret)
		return ret;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  rf_init2_seq1,
					  ARRAY_SIZE(rf_init2_seq1));
	if (ret)
		return ret;

	return cxd2880_io_write_multi_regs(tnr_dmd->io,
					   CXD2880_IO_TGT_DMD,
					   rf_init2_seq2,
					   ARRAY_SIZE(rf_init2_seq2));
}

static int x_tune1(struct cxd2880_tnrdmd *tnr_dmd,
		   enum cxd2880_dtv_sys sys, u32 freq_khz,
		   enum cxd2880_dtv_bandwidth bandwidth,
		   u8 is_cable, int shift_frequency_khz)
{
	u8 data[11] = { 0 };
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_DMD,
					  x_tune1_seq1,
					  ARRAY_SIZE(x_tune1_seq1));
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x10);
	if (ret)
		return ret;

	data[2] = 0x0e;
	data[4] = 0x03;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0xe7, data, 5);
	if (ret)
		return ret;

	data[0] = 0x1f;
	data[1] = 0x80;
	data[2] = 0x18;
	data[3] = 0x00;
	data[4] = 0x07;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0xe7, data, 5);
	if (ret)
		return ret;

	usleep_range(1000, 2000);

	data[0] = 0x72;
	data[1] = 0x81;
	data[3] = 0x1d;
	data[4] = 0x6f;
	data[5] = 0x7e;
	data[7] = 0x1c;
	switch (sys) {
	case CXD2880_DTV_SYS_DVBT:
		data[2] = 0x94;
		data[6] = 0x91;
		break;
	case CXD2880_DTV_SYS_DVBT2:
		data[2] = 0x96;
		data[6] = 0x93;
		break;
	default:
		return -EINVAL;
	}
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0x44, data, 8);
	if (ret)
		return ret;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  x_tune1_seq2,
					  ARRAY_SIZE(x_tune1_seq2));
	if (ret)
		return ret;

	data[0] = 0x03;
	data[1] = 0xe2;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0x1e, data, 2);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x10);
	if (ret)
		return ret;

	data[0] = is_cable ? 0x01 : 0x00;
	data[1] = 0x00;
	data[2] = 0x6b;
	data[3] = 0x4d;

	switch (bandwidth) {
	case CXD2880_DTV_BW_1_7_MHZ:
		data[4] = 0x03;
		break;
	case CXD2880_DTV_BW_5_MHZ:
	case CXD2880_DTV_BW_6_MHZ:
		data[4] = 0x00;
		break;
	case CXD2880_DTV_BW_7_MHZ:
		data[4] = 0x01;
		break;
	case CXD2880_DTV_BW_8_MHZ:
		data[4] = 0x02;
		break;
	default:
		return -EINVAL;
	}

	data[5] = 0x00;

	freq_khz += shift_frequency_khz;

	data[6] = (freq_khz >> 16) & 0x0f;
	data[7] = (freq_khz >> 8) & 0xff;
	data[8] = freq_khz & 0xff;
	data[9] = 0xff;
	data[10] = 0xfe;

	return tnr_dmd->io->write_regs(tnr_dmd->io,
				       CXD2880_IO_TGT_SYS,
				       0x52, data, 11);
}

static int x_tune2(struct cxd2880_tnrdmd *tnr_dmd,
		   enum cxd2880_dtv_bandwidth bandwidth,
		   enum cxd2880_tnrdmd_clockmode clk_mode,
		   int shift_frequency_khz)
{
	u8 data[3] = { 0 };
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x11);
	if (ret)
		return ret;

	data[0] = 0x01;
	data[1] = 0x0e;
	data[2] = 0x01;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0x2d, data, 3);
	if (ret)
		return ret;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  x_tune2_seq1,
					  ARRAY_SIZE(x_tune2_seq1));
	if (ret)
		return ret;

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x2c, data, 1);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x10);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x60, data[0]);
	if (ret)
		return ret;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  x_tune2_seq2,
					  ARRAY_SIZE(x_tune2_seq2));
	if (ret)
		return ret;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_DMD,
					  x_tune2_seq3,
					  ARRAY_SIZE(x_tune2_seq3));
	if (ret)
		return ret;

	if (shift_frequency_khz != 0) {
		int shift_freq = 0;

		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x00, 0xe1);
		if (ret)
			return ret;

		ret = tnr_dmd->io->read_regs(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x60, data, 2);
		if (ret)
			return ret;

		shift_freq = shift_frequency_khz * 1000;

		switch (clk_mode) {
		case CXD2880_TNRDMD_CLOCKMODE_A:
		case CXD2880_TNRDMD_CLOCKMODE_C:
		default:
			if (shift_freq >= 0)
				shift_freq = (shift_freq + 183 / 2) / 183;
			else
				shift_freq = (shift_freq - 183 / 2) / 183;
			break;
		case CXD2880_TNRDMD_CLOCKMODE_B:
			if (shift_freq >= 0)
				shift_freq = (shift_freq + 178 / 2) / 178;
			else
				shift_freq = (shift_freq - 178 / 2) / 178;
			break;
		}

		shift_freq +=
		    cxd2880_convert2s_complement((data[0] << 8) | data[1], 16);

		if (shift_freq > 32767)
			shift_freq = 32767;
		else if (shift_freq < -32768)
			shift_freq = -32768;

		data[0] = (shift_freq >> 8) & 0xff;
		data[1] = shift_freq & 0xff;

		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x60, data, 2);
		if (ret)
			return ret;

		ret = tnr_dmd->io->read_regs(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x69, data, 1);
		if (ret)
			return ret;

		shift_freq = -shift_frequency_khz;

		if (bandwidth == CXD2880_DTV_BW_1_7_MHZ) {
			switch (clk_mode) {
			case CXD2880_TNRDMD_CLOCKMODE_A:
			case CXD2880_TNRDMD_CLOCKMODE_C:
			default:
				if (shift_freq >= 0)
					shift_freq =
					    (shift_freq * 1000 +
					     17578 / 2) / 17578;
				else
					shift_freq =
					    (shift_freq * 1000 -
					     17578 / 2) / 17578;
				break;
			case CXD2880_TNRDMD_CLOCKMODE_B:
				if (shift_freq >= 0)
					shift_freq =
					    (shift_freq * 1000 +
					     17090 / 2) / 17090;
				else
					shift_freq =
					    (shift_freq * 1000 -
					     17090 / 2) / 17090;
				break;
			}
		} else {
			switch (clk_mode) {
			case CXD2880_TNRDMD_CLOCKMODE_A:
			case CXD2880_TNRDMD_CLOCKMODE_C:
			default:
				if (shift_freq >= 0)
					shift_freq =
					    (shift_freq * 1000 +
					     35156 / 2) / 35156;
				else
					shift_freq =
					    (shift_freq * 1000 -
					     35156 / 2) / 35156;
				break;
			case CXD2880_TNRDMD_CLOCKMODE_B:
				if (shift_freq >= 0)
					shift_freq =
					    (shift_freq * 1000 +
					     34180 / 2) / 34180;
				else
					shift_freq =
					    (shift_freq * 1000 -
					     34180 / 2) / 34180;
				break;
			}
		}

		shift_freq += cxd2880_convert2s_complement(data[0], 8);

		if (shift_freq > 127)
			shift_freq = 127;
		else if (shift_freq < -128)
			shift_freq = -128;

		data[0] = shift_freq & 0xff;

		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x69, data[0]);
		if (ret)
			return ret;
	}

	if (tnr_dmd->create_param.stationary_use) {
		ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
						  CXD2880_IO_TGT_DMD,
						  x_tune2_seq4,
						  ARRAY_SIZE(x_tune2_seq4));
		if (ret)
			return ret;
	}

	return cxd2880_io_write_multi_regs(tnr_dmd->io,
					   CXD2880_IO_TGT_DMD,
					   x_tune2_seq5,
					   ARRAY_SIZE(x_tune2_seq5));
}

static int x_tune3(struct cxd2880_tnrdmd *tnr_dmd,
		   enum cxd2880_dtv_sys sys,
		   u8 en_fef_intmtnt_ctrl)
{
	u8 data[6] = { 0 };
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_DMD,
					  x_tune3_seq,
					  ARRAY_SIZE(x_tune3_seq));
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x10);
	if (ret)
		return ret;

	if (sys == CXD2880_DTV_SYS_DVBT2 && en_fef_intmtnt_ctrl)
		memset(data, 0x01, sizeof(data));
	else
		memset(data, 0x00, sizeof(data));

	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0xef, data, 6);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x2d);
	if (ret)
		return ret;
	if (sys == CXD2880_DTV_SYS_DVBT2 && en_fef_intmtnt_ctrl)
		data[0] = 0x00;
	else
		data[0] = 0x01;

	return tnr_dmd->io->write_reg(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0xb1, data[0]);
}

static int x_tune4(struct cxd2880_tnrdmd *tnr_dmd)
{
	u8 data[2] = { 0 };
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_MAIN)
		return -EINVAL;

	ret = tnr_dmd->diver_sub->io->write_reg(tnr_dmd->diver_sub->io,
						CXD2880_IO_TGT_SYS,
						0x00, 0x00);
	if (ret)
		return ret;
	data[0] = 0x14;
	data[1] = 0x00;
	ret = tnr_dmd->diver_sub->io->write_regs(tnr_dmd->diver_sub->io,
						CXD2880_IO_TGT_SYS,
						0x55, data, 2);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x00);
	if (ret)
		return ret;
	data[0] = 0x0b;
	data[1] = 0xff;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0x53, data, 2);
	if (ret)
		return ret;
	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x57, 0x01);
	if (ret)
		return ret;
	data[0] = 0x0b;
	data[1] = 0xff;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0x55, data, 2);
	if (ret)
		return ret;

	ret = tnr_dmd->diver_sub->io->write_reg(tnr_dmd->diver_sub->io,
						CXD2880_IO_TGT_SYS,
						0x00, 0x00);
	if (ret)
		return ret;
	data[0] = 0x14;
	data[1] = 0x00;
	ret = tnr_dmd->diver_sub->io->write_regs(tnr_dmd->diver_sub->io,
						 CXD2880_IO_TGT_SYS,
						 0x53, data, 2);
	if (ret)
		return ret;
	ret = tnr_dmd->diver_sub->io->write_reg(tnr_dmd->diver_sub->io,
						CXD2880_IO_TGT_SYS,
						0x57, 0x02);
	if (ret)
		return ret;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_DMD,
					  x_tune4_seq,
					  ARRAY_SIZE(x_tune4_seq));
	if (ret)
		return ret;

	return cxd2880_io_write_multi_regs(tnr_dmd->diver_sub->io,
					   CXD2880_IO_TGT_DMD,
					   x_tune4_seq,
					   ARRAY_SIZE(x_tune4_seq));
}

static int x_sleep1(struct cxd2880_tnrdmd *tnr_dmd)
{
	u8 data[3] = { 0 };
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_MAIN)
		return -EINVAL;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  x_sleep1_seq,
					  ARRAY_SIZE(x_sleep1_seq));
	if (ret)
		return ret;

	data[0] = 0x00;
	data[1] = 0x00;
	ret = tnr_dmd->io->write_regs(tnr_dmd->io,
				      CXD2880_IO_TGT_SYS,
				      0x53, data, 2);
	if (ret)
		return ret;

	ret = tnr_dmd->diver_sub->io->write_reg(tnr_dmd->diver_sub->io,
						CXD2880_IO_TGT_SYS,
						0x00, 0x00);
	if (ret)
		return ret;
	data[0] = 0x1f;
	data[1] = 0xff;
	data[2] = 0x03;
	ret = tnr_dmd->diver_sub->io->write_regs(tnr_dmd->diver_sub->io,
						 CXD2880_IO_TGT_SYS,
						 0x55, data, 3);
	if (ret)
		return ret;
	data[0] = 0x00;
	data[1] = 0x00;
	ret = tnr_dmd->diver_sub->io->write_regs(tnr_dmd->diver_sub->io,
						 CXD2880_IO_TGT_SYS,
						 0x53, data, 2);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x00);
	if (ret)
		return ret;
	data[0] = 0x1f;
	data[1] = 0xff;

	return tnr_dmd->io->write_regs(tnr_dmd->io,
				       CXD2880_IO_TGT_SYS,
				       0x55, data, 2);
}

static int x_sleep2(struct cxd2880_tnrdmd *tnr_dmd)
{
	u8 data = 0;
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_DMD,
					  x_sleep2_seq1,
					  ARRAY_SIZE(x_sleep2_seq1));
	if (ret)
		return ret;

	usleep_range(1000, 2000);

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0xb2, &data, 1);
	if (ret)
		return ret;

	if ((data & 0x01) == 0x00)
		return -EINVAL;

	return cxd2880_io_write_multi_regs(tnr_dmd->io,
					   CXD2880_IO_TGT_SYS,
					   x_sleep2_seq2,
					   ARRAY_SIZE(x_sleep2_seq2));
}

static int x_sleep3(struct cxd2880_tnrdmd *tnr_dmd)
{
	if (!tnr_dmd)
		return -EINVAL;

	return cxd2880_io_write_multi_regs(tnr_dmd->io,
					   CXD2880_IO_TGT_DMD,
					   x_sleep3_seq,
					   ARRAY_SIZE(x_sleep3_seq));
}

static int x_sleep4(struct cxd2880_tnrdmd *tnr_dmd)
{
	if (!tnr_dmd)
		return -EINVAL;

	return cxd2880_io_write_multi_regs(tnr_dmd->io,
					   CXD2880_IO_TGT_DMD,
					   x_sleep4_seq,
					   ARRAY_SIZE(x_sleep4_seq));
}

static int spll_reset(struct cxd2880_tnrdmd *tnr_dmd,
		      enum cxd2880_tnrdmd_clockmode clockmode)
{
	u8 data[4] = { 0 };
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  spll_reset_seq1,
					  ARRAY_SIZE(spll_reset_seq1));
	if (ret)
		return ret;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_DMD,
					  spll_reset_seq2,
					  ARRAY_SIZE(spll_reset_seq2));
	if (ret)
		return ret;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  spll_reset_seq3,
					  ARRAY_SIZE(spll_reset_seq3));
	if (ret)
		return ret;

	switch (clockmode) {
	case CXD2880_TNRDMD_CLOCKMODE_A:
		data[0] = 0x00;
		break;

	case CXD2880_TNRDMD_CLOCKMODE_B:
		data[0] = 0x01;
		break;

	case CXD2880_TNRDMD_CLOCKMODE_C:
		data[0] = 0x02;
		break;

	default:
		return -EINVAL;
	}
	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x30, data[0]);
	if (ret)
		return ret;
	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x22, 0x00);
	if (ret)
		return ret;

	usleep_range(2000, 3000);

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x0a);
	if (ret)
		return ret;
	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x10, data, 1);
	if (ret)
		return ret;
	if ((data[0] & 0x01) == 0x00)
		return -EINVAL;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  spll_reset_seq4,
					  ARRAY_SIZE(spll_reset_seq4));
	if (ret)
		return ret;

	usleep_range(1000, 2000);

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_DMD,
					  spll_reset_seq5,
					  ARRAY_SIZE(spll_reset_seq5));
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x10);
	if (ret)
		return ret;

	memset(data, 0x00, sizeof(data));

	return tnr_dmd->io->write_regs(tnr_dmd->io,
				       CXD2880_IO_TGT_SYS,
				       0x26, data, 4);
}

static int t_power_x(struct cxd2880_tnrdmd *tnr_dmd, u8 on)
{
	u8 data[3] = { 0 };
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  t_power_x_seq1,
					  ARRAY_SIZE(t_power_x_seq1));
	if (ret)
		return ret;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_DMD,
					  t_power_x_seq2,
					  ARRAY_SIZE(t_power_x_seq2));
	if (ret)
		return ret;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  t_power_x_seq3,
					  ARRAY_SIZE(t_power_x_seq3));
	if (ret)
		return ret;

	if (on) {
		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_SYS,
					     0x2b, 0x01);
		if (ret)
			return ret;

		usleep_range(1000, 2000);

		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_SYS,
					     0x00, 0x0a);
		if (ret)
			return ret;
		ret = tnr_dmd->io->read_regs(tnr_dmd->io,
					     CXD2880_IO_TGT_SYS,
					     0x12, data, 1);
		if (ret)
			return ret;
		if ((data[0] & 0x01) == 0)
			return -EINVAL;

		ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
						  CXD2880_IO_TGT_SYS,
						  t_power_x_seq4,
						  ARRAY_SIZE(t_power_x_seq4));
		if (ret)
			return ret;
	} else {
		data[0] = 0x03;
		data[1] = 0x00;
		ret = tnr_dmd->io->write_regs(tnr_dmd->io,
					      CXD2880_IO_TGT_SYS,
					      0x2a, data, 2);
		if (ret)
			return ret;

		usleep_range(1000, 2000);

		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_SYS,
					     0x00, 0x0a);
		if (ret)
			return ret;
		ret = tnr_dmd->io->read_regs(tnr_dmd->io,
					     CXD2880_IO_TGT_SYS,
					     0x13, data, 1);
		if (ret)
			return ret;
		if ((data[0] & 0x01) == 0)
			return -EINVAL;
	}

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  t_power_x_seq5,
					  ARRAY_SIZE(t_power_x_seq5));
	if (ret)
		return ret;

	usleep_range(1000, 2000);

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x0a);
	if (ret)
		return ret;
	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x11, data, 1);
	if (ret)
		return ret;
	if ((data[0] & 0x01) == 0)
		return -EINVAL;

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_SYS,
					  t_power_x_seq6,
					  ARRAY_SIZE(t_power_x_seq6));
	if (ret)
		return ret;

	usleep_range(1000, 2000);

	ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
					  CXD2880_IO_TGT_DMD,
					  t_power_x_seq7,
					  ARRAY_SIZE(t_power_x_seq7));
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x10);
	if (ret)
		return ret;

	memset(data, 0x00, sizeof(data));

	return tnr_dmd->io->write_regs(tnr_dmd->io,
				       CXD2880_IO_TGT_SYS,
				       0x27, data, 3);
}

struct cxd2880_tnrdmd_ts_clk_cfg {
	u8 srl_clk_mode;
	u8 srl_duty_mode;
	u8 ts_clk_period;
};

static int set_ts_clk_mode_and_freq(struct cxd2880_tnrdmd *tnr_dmd,
				    enum cxd2880_dtv_sys sys)
{
	int ret;
	u8 backwards_compatible = 0;
	struct cxd2880_tnrdmd_ts_clk_cfg ts_clk_cfg;
	u8 ts_rate_ctrl_off = 0;
	u8 ts_in_off = 0;
	u8 ts_clk_manaul_on = 0;
	u8 data = 0;

	static const struct cxd2880_tnrdmd_ts_clk_cfg srl_ts_clk_stgs[2][2] = {
		{
			{3, 1, 8,},
			{0, 2, 16,}
		}, {
			{1, 1, 8,},
			{2, 2, 16,}
		}
	};

	if (!tnr_dmd)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x00);
	if (ret)
		return ret;

	if (tnr_dmd->is_ts_backwards_compatible_mode) {
		backwards_compatible = 1;
		ts_rate_ctrl_off = 1;
		ts_in_off = 1;
	} else {
		backwards_compatible = 0;
		ts_rate_ctrl_off = 0;
		ts_in_off = 0;
	}

	if (tnr_dmd->ts_byte_clk_manual_setting) {
		ts_clk_manaul_on = 1;
		ts_rate_ctrl_off = 0;
	}

	ret = cxd2880_io_set_reg_bits(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0xd3, ts_rate_ctrl_off, 0x01);
	if (ret)
		return ret;

	ret = cxd2880_io_set_reg_bits(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0xde, ts_in_off, 0x01);
	if (ret)
		return ret;

	ret = cxd2880_io_set_reg_bits(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0xda, ts_clk_manaul_on, 0x01);
	if (ret)
		return ret;

	ts_clk_cfg = srl_ts_clk_stgs[tnr_dmd->srl_ts_clk_mod_cnts]
				    [tnr_dmd->srl_ts_clk_frq];

	if (tnr_dmd->ts_byte_clk_manual_setting)
		ts_clk_cfg.ts_clk_period = tnr_dmd->ts_byte_clk_manual_setting;

	ret = cxd2880_io_set_reg_bits(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0xc4, ts_clk_cfg.srl_clk_mode, 0x03);
	if (ret)
		return ret;

	ret = cxd2880_io_set_reg_bits(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0xd1, ts_clk_cfg.srl_duty_mode, 0x03);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD, 0xd9,
				     ts_clk_cfg.ts_clk_period);
	if (ret)
		return ret;

	data = backwards_compatible ? 0x00 : 0x01;

	if (sys == CXD2880_DTV_SYS_DVBT) {
		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x00, 0x10);
		if (ret)
			return ret;

		ret =
		    cxd2880_io_set_reg_bits(tnr_dmd->io,
					    CXD2880_IO_TGT_DMD,
					    0x66, data, 0x01);
	}

	return ret;
}

static int pid_ftr_setting(struct cxd2880_tnrdmd *tnr_dmd,
			   struct cxd2880_tnrdmd_pid_ftr_cfg
			   *pid_ftr_cfg)
{
	int i;
	int ret;
	u8 data[65];

	if (!tnr_dmd)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x00);
	if (ret)
		return ret;

	if (!pid_ftr_cfg)
		return tnr_dmd->io->write_reg(tnr_dmd->io,
					      CXD2880_IO_TGT_DMD,
					      0x50, 0x02);

	data[0] = pid_ftr_cfg->is_negative ? 0x01 : 0x00;

	for (i = 0; i < 32; i++) {
		if (pid_ftr_cfg->pid_cfg[i].is_en) {
			data[1 + (i * 2)] = (pid_ftr_cfg->pid_cfg[i].pid >> 8) | 0x20;
			data[2 + (i * 2)] =  pid_ftr_cfg->pid_cfg[i].pid & 0xff;
		} else {
			data[1 + (i * 2)] = 0x00;
			data[2 + (i * 2)] = 0x00;
		}
	}

	return tnr_dmd->io->write_regs(tnr_dmd->io,
				       CXD2880_IO_TGT_DMD,
				       0x50, data, 65);
}

static int load_cfg_mem(struct cxd2880_tnrdmd *tnr_dmd)
{
	int ret;
	u8 i;

	if (!tnr_dmd)
		return -EINVAL;

	for (i = 0; i < tnr_dmd->cfg_mem_last_entry; i++) {
		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     tnr_dmd->cfg_mem[i].tgt,
					     0x00, tnr_dmd->cfg_mem[i].bank);
		if (ret)
			return ret;

		ret = cxd2880_io_set_reg_bits(tnr_dmd->io,
					      tnr_dmd->cfg_mem[i].tgt,
					      tnr_dmd->cfg_mem[i].address,
					      tnr_dmd->cfg_mem[i].value,
					      tnr_dmd->cfg_mem[i].bit_mask);
		if (ret)
			return ret;
	}

	return 0;
}

static int set_cfg_mem(struct cxd2880_tnrdmd *tnr_dmd,
		       enum cxd2880_io_tgt tgt,
		       u8 bank, u8 address, u8 value, u8 bit_mask)
{
	u8 i;
	u8 value_stored = 0;

	if (!tnr_dmd)
		return -EINVAL;

	for (i = 0; i < tnr_dmd->cfg_mem_last_entry; i++) {
		if (value_stored == 0 &&
		    tnr_dmd->cfg_mem[i].tgt == tgt &&
		    tnr_dmd->cfg_mem[i].bank == bank &&
		    tnr_dmd->cfg_mem[i].address == address) {
			tnr_dmd->cfg_mem[i].value &= ~bit_mask;
			tnr_dmd->cfg_mem[i].value |= (value & bit_mask);

			tnr_dmd->cfg_mem[i].bit_mask |= bit_mask;

			value_stored = 1;
		}
	}

	if (value_stored)
		return 0;

	if (tnr_dmd->cfg_mem_last_entry < CXD2880_TNRDMD_MAX_CFG_MEM_COUNT) {
		tnr_dmd->cfg_mem[tnr_dmd->cfg_mem_last_entry].tgt = tgt;
		tnr_dmd->cfg_mem[tnr_dmd->cfg_mem_last_entry].bank = bank;
		tnr_dmd->cfg_mem[tnr_dmd->cfg_mem_last_entry].address = address;
		tnr_dmd->cfg_mem[tnr_dmd->cfg_mem_last_entry].value = (value & bit_mask);
		tnr_dmd->cfg_mem[tnr_dmd->cfg_mem_last_entry].bit_mask = bit_mask;
		tnr_dmd->cfg_mem_last_entry++;
	} else {
		return -ENOMEM;
	}

	return 0;
}

int cxd2880_tnrdmd_create(struct cxd2880_tnrdmd *tnr_dmd,
			  struct cxd2880_io *io,
			  struct cxd2880_tnrdmd_create_param
			  *create_param)
{
	if (!tnr_dmd || !io || !create_param)
		return -EINVAL;

	memset(tnr_dmd, 0, sizeof(struct cxd2880_tnrdmd));

	tnr_dmd->io = io;
	tnr_dmd->create_param = *create_param;

	tnr_dmd->diver_mode = CXD2880_TNRDMD_DIVERMODE_SINGLE;
	tnr_dmd->diver_sub = NULL;

	tnr_dmd->srl_ts_clk_mod_cnts = 1;
	tnr_dmd->en_fef_intmtnt_base = 1;
	tnr_dmd->en_fef_intmtnt_lite = 1;
	tnr_dmd->rf_lvl_cmpstn = NULL;
	tnr_dmd->lna_thrs_tbl_air = NULL;
	tnr_dmd->lna_thrs_tbl_cable = NULL;
	atomic_set(&tnr_dmd->cancel, 0);

	return 0;
}

int cxd2880_tnrdmd_diver_create(struct cxd2880_tnrdmd
				*tnr_dmd_main,
				struct cxd2880_io *io_main,
				struct cxd2880_tnrdmd *tnr_dmd_sub,
				struct cxd2880_io *io_sub,
				struct
				cxd2880_tnrdmd_diver_create_param
				*create_param)
{
	struct cxd2880_tnrdmd_create_param *main_param, *sub_param;

	if (!tnr_dmd_main || !io_main || !tnr_dmd_sub || !io_sub ||
	    !create_param)
		return -EINVAL;

	memset(tnr_dmd_main, 0, sizeof(struct cxd2880_tnrdmd));
	memset(tnr_dmd_sub, 0, sizeof(struct cxd2880_tnrdmd));

	main_param = &tnr_dmd_main->create_param;
	sub_param = &tnr_dmd_sub->create_param;

	tnr_dmd_main->io = io_main;
	tnr_dmd_main->diver_mode = CXD2880_TNRDMD_DIVERMODE_MAIN;
	tnr_dmd_main->diver_sub = tnr_dmd_sub;
	tnr_dmd_main->create_param.en_internal_ldo =
	    create_param->en_internal_ldo;

	main_param->ts_output_if = create_param->ts_output_if;
	main_param->xtal_share_type = CXD2880_TNRDMD_XTAL_SHARE_MASTER;
	main_param->xosc_cap = create_param->xosc_cap_main;
	main_param->xosc_i = create_param->xosc_i_main;
	main_param->is_cxd2881gg = create_param->is_cxd2881gg;
	main_param->stationary_use = create_param->stationary_use;

	tnr_dmd_sub->io = io_sub;
	tnr_dmd_sub->diver_mode = CXD2880_TNRDMD_DIVERMODE_SUB;
	tnr_dmd_sub->diver_sub = NULL;

	sub_param->en_internal_ldo = create_param->en_internal_ldo;
	sub_param->ts_output_if = create_param->ts_output_if;
	sub_param->xtal_share_type = CXD2880_TNRDMD_XTAL_SHARE_SLAVE;
	sub_param->xosc_cap = 0;
	sub_param->xosc_i = create_param->xosc_i_sub;
	sub_param->is_cxd2881gg = create_param->is_cxd2881gg;
	sub_param->stationary_use = create_param->stationary_use;

	tnr_dmd_main->srl_ts_clk_mod_cnts = 1;
	tnr_dmd_main->en_fef_intmtnt_base = 1;
	tnr_dmd_main->en_fef_intmtnt_lite = 1;
	tnr_dmd_main->rf_lvl_cmpstn = NULL;
	tnr_dmd_main->lna_thrs_tbl_air = NULL;
	tnr_dmd_main->lna_thrs_tbl_cable = NULL;

	tnr_dmd_sub->srl_ts_clk_mod_cnts = 1;
	tnr_dmd_sub->en_fef_intmtnt_base = 1;
	tnr_dmd_sub->en_fef_intmtnt_lite = 1;
	tnr_dmd_sub->rf_lvl_cmpstn = NULL;
	tnr_dmd_sub->lna_thrs_tbl_air = NULL;
	tnr_dmd_sub->lna_thrs_tbl_cable = NULL;

	return 0;
}

int cxd2880_tnrdmd_init1(struct cxd2880_tnrdmd *tnr_dmd)
{
	int ret;

	if (!tnr_dmd || tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	tnr_dmd->chip_id = CXD2880_TNRDMD_CHIP_ID_UNKNOWN;
	tnr_dmd->state = CXD2880_TNRDMD_STATE_UNKNOWN;
	tnr_dmd->clk_mode = CXD2880_TNRDMD_CLOCKMODE_UNKNOWN;
	tnr_dmd->frequency_khz = 0;
	tnr_dmd->sys = CXD2880_DTV_SYS_UNKNOWN;
	tnr_dmd->bandwidth = CXD2880_DTV_BW_UNKNOWN;
	tnr_dmd->scan_mode = 0;
	atomic_set(&tnr_dmd->cancel, 0);

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		tnr_dmd->diver_sub->chip_id = CXD2880_TNRDMD_CHIP_ID_UNKNOWN;
		tnr_dmd->diver_sub->state = CXD2880_TNRDMD_STATE_UNKNOWN;
		tnr_dmd->diver_sub->clk_mode = CXD2880_TNRDMD_CLOCKMODE_UNKNOWN;
		tnr_dmd->diver_sub->frequency_khz = 0;
		tnr_dmd->diver_sub->sys = CXD2880_DTV_SYS_UNKNOWN;
		tnr_dmd->diver_sub->bandwidth = CXD2880_DTV_BW_UNKNOWN;
		tnr_dmd->diver_sub->scan_mode = 0;
		atomic_set(&tnr_dmd->diver_sub->cancel, 0);
	}

	ret = cxd2880_tnrdmd_chip_id(tnr_dmd, &tnr_dmd->chip_id);
	if (ret)
		return ret;

	if (!CXD2880_TNRDMD_CHIP_ID_VALID(tnr_dmd->chip_id))
		return -ENOTTY;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		ret =
		    cxd2880_tnrdmd_chip_id(tnr_dmd->diver_sub,
					   &tnr_dmd->diver_sub->chip_id);
		if (ret)
			return ret;

		if (!CXD2880_TNRDMD_CHIP_ID_VALID(tnr_dmd->diver_sub->chip_id))
			return -ENOTTY;
	}

	ret = p_init1(tnr_dmd);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		ret = p_init1(tnr_dmd->diver_sub);
		if (ret)
			return ret;
	}

	usleep_range(1000, 2000);

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		ret = p_init2(tnr_dmd->diver_sub);
		if (ret)
			return ret;
	}

	ret = p_init2(tnr_dmd);
	if (ret)
		return ret;

	usleep_range(5000, 6000);

	ret = p_init3(tnr_dmd);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		ret = p_init3(tnr_dmd->diver_sub);
		if (ret)
			return ret;
	}

	ret = rf_init1(tnr_dmd);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN)
		ret = rf_init1(tnr_dmd->diver_sub);

	return ret;
}

int cxd2880_tnrdmd_init2(struct cxd2880_tnrdmd *tnr_dmd)
{
	u8 cpu_task_completed;
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	ret = cxd2880_tnrdmd_check_internal_cpu_status(tnr_dmd,
						     &cpu_task_completed);
	if (ret)
		return ret;

	if (!cpu_task_completed)
		return -EINVAL;

	ret = rf_init2(tnr_dmd);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		ret = rf_init2(tnr_dmd->diver_sub);
		if (ret)
			return ret;
	}

	ret = load_cfg_mem(tnr_dmd);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		ret = load_cfg_mem(tnr_dmd->diver_sub);
		if (ret)
			return ret;
	}

	tnr_dmd->state = CXD2880_TNRDMD_STATE_SLEEP;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN)
		tnr_dmd->diver_sub->state = CXD2880_TNRDMD_STATE_SLEEP;

	return ret;
}

int cxd2880_tnrdmd_check_internal_cpu_status(struct cxd2880_tnrdmd
					     *tnr_dmd,
					     u8 *task_completed)
{
	u16 cpu_status = 0;
	int ret;

	if (!tnr_dmd || !task_completed)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	ret = cxd2880_tnrdmd_mon_internal_cpu_status(tnr_dmd, &cpu_status);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SINGLE) {
		if (cpu_status == 0)
			*task_completed = 1;
		else
			*task_completed = 0;

		return 0;
	}
	if (cpu_status != 0) {
		*task_completed = 0;
		return 0;
	}

	ret = cxd2880_tnrdmd_mon_internal_cpu_status_sub(tnr_dmd, &cpu_status);
	if (ret)
		return ret;

	if (cpu_status == 0)
		*task_completed = 1;
	else
		*task_completed = 0;

	return ret;
}

int cxd2880_tnrdmd_common_tune_setting1(struct cxd2880_tnrdmd *tnr_dmd,
					enum cxd2880_dtv_sys sys,
					u32 frequency_khz,
					enum cxd2880_dtv_bandwidth
					bandwidth, u8 one_seg_opt,
					u8 one_seg_opt_shft_dir)
{
	u8 data;
	enum cxd2880_tnrdmd_clockmode new_clk_mode =
				CXD2880_TNRDMD_CLOCKMODE_A;
	int shift_frequency_khz;
	u8 cpu_task_completed;
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP &&
	    tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (frequency_khz < 4000)
		return -EINVAL;

	ret = cxd2880_tnrdmd_sleep(tnr_dmd);
	if (ret)
		return ret;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00,
				     0x00);
	if (ret)
		return ret;

	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x2b,
				     &data,
				     1);
	if (ret)
		return ret;

	switch (sys) {
	case CXD2880_DTV_SYS_DVBT:
		if (data == 0x00) {
			ret = t_power_x(tnr_dmd, 1);
			if (ret)
				return ret;

			if (tnr_dmd->diver_mode ==
			    CXD2880_TNRDMD_DIVERMODE_MAIN) {
				ret = t_power_x(tnr_dmd->diver_sub, 1);
				if (ret)
					return ret;
			}
		}
		break;

	case CXD2880_DTV_SYS_DVBT2:
		if (data == 0x01) {
			ret = t_power_x(tnr_dmd, 0);
			if (ret)
				return ret;

			if (tnr_dmd->diver_mode ==
			    CXD2880_TNRDMD_DIVERMODE_MAIN) {
				ret = t_power_x(tnr_dmd->diver_sub, 0);
				if (ret)
					return ret;
			}
		}
		break;

	default:
		return -EINVAL;
	}

	ret = spll_reset(tnr_dmd, new_clk_mode);
	if (ret)
		return ret;

	tnr_dmd->clk_mode = new_clk_mode;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		ret = spll_reset(tnr_dmd->diver_sub, new_clk_mode);
		if (ret)
			return ret;

		tnr_dmd->diver_sub->clk_mode = new_clk_mode;
	}

	ret = load_cfg_mem(tnr_dmd);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		ret = load_cfg_mem(tnr_dmd->diver_sub);
		if (ret)
			return ret;
	}

	if (one_seg_opt) {
		if (tnr_dmd->diver_mode ==
		    CXD2880_TNRDMD_DIVERMODE_MAIN) {
			shift_frequency_khz = 350;
		} else {
			if (one_seg_opt_shft_dir)
				shift_frequency_khz = 350;
			else
				shift_frequency_khz = -350;

			if (tnr_dmd->create_param.xtal_share_type ==
			    CXD2880_TNRDMD_XTAL_SHARE_SLAVE)
				shift_frequency_khz *= -1;
		}
	} else {
		if (tnr_dmd->diver_mode ==
		    CXD2880_TNRDMD_DIVERMODE_MAIN) {
			shift_frequency_khz = 150;
		} else {
			switch (tnr_dmd->create_param.xtal_share_type) {
			case CXD2880_TNRDMD_XTAL_SHARE_NONE:
			case CXD2880_TNRDMD_XTAL_SHARE_EXTREF:
			default:
				shift_frequency_khz = 0;
				break;
			case CXD2880_TNRDMD_XTAL_SHARE_MASTER:
				shift_frequency_khz = 150;
				break;
			case CXD2880_TNRDMD_XTAL_SHARE_SLAVE:
				shift_frequency_khz = -150;
				break;
			}
		}
	}

	ret =
	    x_tune1(tnr_dmd, sys, frequency_khz, bandwidth,
		    tnr_dmd->is_cable_input, shift_frequency_khz);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		ret =
		    x_tune1(tnr_dmd->diver_sub, sys, frequency_khz,
			    bandwidth, tnr_dmd->is_cable_input,
			    -shift_frequency_khz);
		if (ret)
			return ret;
	}

	usleep_range(10000, 11000);

	ret =
	    cxd2880_tnrdmd_check_internal_cpu_status(tnr_dmd,
					     &cpu_task_completed);
	if (ret)
		return ret;

	if (!cpu_task_completed)
		return -EINVAL;

	ret =
	    x_tune2(tnr_dmd, bandwidth, tnr_dmd->clk_mode,
		    shift_frequency_khz);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		ret =
		    x_tune2(tnr_dmd->diver_sub, bandwidth,
			    tnr_dmd->diver_sub->clk_mode,
			    -shift_frequency_khz);
		if (ret)
			return ret;
	}

	if (tnr_dmd->create_param.ts_output_if == CXD2880_TNRDMD_TSOUT_IF_TS) {
		ret = set_ts_clk_mode_and_freq(tnr_dmd, sys);
	} else {
		struct cxd2880_tnrdmd_pid_ftr_cfg *pid_ftr_cfg;

		if (tnr_dmd->pid_ftr_cfg_en)
			pid_ftr_cfg = &tnr_dmd->pid_ftr_cfg;
		else
			pid_ftr_cfg = NULL;

		ret = pid_ftr_setting(tnr_dmd, pid_ftr_cfg);
	}

	return ret;
}

int cxd2880_tnrdmd_common_tune_setting2(struct cxd2880_tnrdmd
					*tnr_dmd,
					enum cxd2880_dtv_sys sys,
					u8 en_fef_intmtnt_ctrl)
{
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP &&
	    tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	ret = x_tune3(tnr_dmd, sys, en_fef_intmtnt_ctrl);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		ret = x_tune3(tnr_dmd->diver_sub, sys, en_fef_intmtnt_ctrl);
		if (ret)
			return ret;
		ret = x_tune4(tnr_dmd);
		if (ret)
			return ret;
	}

	return cxd2880_tnrdmd_set_ts_output(tnr_dmd, 1);
}

int cxd2880_tnrdmd_sleep(struct cxd2880_tnrdmd *tnr_dmd)
{
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state == CXD2880_TNRDMD_STATE_SLEEP)
		return 0;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	ret = cxd2880_tnrdmd_set_ts_output(tnr_dmd, 0);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		ret = x_sleep1(tnr_dmd);
		if (ret)
			return ret;
	}

	ret = x_sleep2(tnr_dmd);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		ret = x_sleep2(tnr_dmd->diver_sub);
		if (ret)
			return ret;
	}

	switch (tnr_dmd->sys) {
	case CXD2880_DTV_SYS_DVBT:
		ret = cxd2880_tnrdmd_dvbt_sleep_setting(tnr_dmd);
		if (ret)
			return ret;
		break;

	case CXD2880_DTV_SYS_DVBT2:
		ret = cxd2880_tnrdmd_dvbt2_sleep_setting(tnr_dmd);
		if (ret)
			return ret;
		break;

	default:
		return -EINVAL;
	}

	ret = x_sleep3(tnr_dmd);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		ret = x_sleep3(tnr_dmd->diver_sub);
		if (ret)
			return ret;
	}

	ret = x_sleep4(tnr_dmd);
	if (ret)
		return ret;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		ret = x_sleep4(tnr_dmd->diver_sub);
		if (ret)
			return ret;
	}

	tnr_dmd->state = CXD2880_TNRDMD_STATE_SLEEP;
	tnr_dmd->frequency_khz = 0;
	tnr_dmd->sys = CXD2880_DTV_SYS_UNKNOWN;
	tnr_dmd->bandwidth = CXD2880_DTV_BW_UNKNOWN;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN) {
		tnr_dmd->diver_sub->state = CXD2880_TNRDMD_STATE_SLEEP;
		tnr_dmd->diver_sub->frequency_khz = 0;
		tnr_dmd->diver_sub->sys = CXD2880_DTV_SYS_UNKNOWN;
		tnr_dmd->diver_sub->bandwidth = CXD2880_DTV_BW_UNKNOWN;
	}

	return 0;
}

int cxd2880_tnrdmd_set_cfg(struct cxd2880_tnrdmd *tnr_dmd,
			   enum cxd2880_tnrdmd_cfg_id id,
			   int value)
{
	int ret = 0;
	u8 data[2] = { 0 };
	u8 need_sub_setting = 0;

	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP &&
	    tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	switch (id) {
	case CXD2880_TNRDMD_CFG_OUTPUT_SEL_MSB:
		if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP)
			return -EINVAL;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x00, 0xc4,
							 value ? 0x00 : 0x10,
							 0x10);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_TSVALID_ACTIVE_HI:
		if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP)
			return -EINVAL;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x00, 0xc5,
							 value ? 0x00 : 0x02,
							 0x02);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_TSSYNC_ACTIVE_HI:
		if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP)
			return -EINVAL;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x00, 0xc5,
							 value ? 0x00 : 0x04,
							 0x04);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_TSERR_ACTIVE_HI:
		if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP)
			return -EINVAL;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x00, 0xcb,
							 value ? 0x00 : 0x01,
							 0x01);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_LATCH_ON_POSEDGE:
		if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP)
			return -EINVAL;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x00, 0xc5,
							 value ? 0x01 : 0x00,
							 0x01);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_TSCLK_CONT:
		if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP)
			return -EINVAL;

		tnr_dmd->srl_ts_clk_mod_cnts = value ? 0x01 : 0x00;
		break;

	case CXD2880_TNRDMD_CFG_TSCLK_MASK:
		if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP)
			return -EINVAL;

		if (value < 0 || value > 0x1f)
			return -EINVAL;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x00, 0xc6, value,
							 0x1f);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_TSVALID_MASK:
		if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP)
			return -EINVAL;

		if (value < 0 || value > 0x1f)
			return -EINVAL;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x00, 0xc8, value,
							 0x1f);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_TSERR_MASK:
		if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP)
			return -EINVAL;

		if (value < 0 || value > 0x1f)
			return -EINVAL;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x00, 0xc9, value,
							 0x1f);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_TSERR_VALID_DIS:
		if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP)
			return -EINVAL;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x00, 0x91,
							 value ? 0x01 : 0x00,
							 0x01);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_TSPIN_CURRENT:
		if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP)
			return -EINVAL;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_SYS,
							 0x00, 0x51, value,
							 0x3f);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_TSPIN_PULLUP_MANUAL:
		if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP)
			return -EINVAL;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_SYS,
							 0x00, 0x50,
							 value ? 0x80 : 0x00,
							 0x80);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_TSPIN_PULLUP:
		if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP)
			return -EINVAL;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_SYS,
							 0x00, 0x50, value,
							 0x3f);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_TSCLK_FREQ:
		if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP)
			return -EINVAL;

		if (value < 0 || value > 1)
			return -EINVAL;

		tnr_dmd->srl_ts_clk_frq =
		    (enum cxd2880_tnrdmd_serial_ts_clk)value;
		break;

	case CXD2880_TNRDMD_CFG_TSBYTECLK_MANUAL:
		if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP)
			return -EINVAL;

		if (value < 0 || value > 0xff)
			return -EINVAL;

		tnr_dmd->ts_byte_clk_manual_setting = value;

		break;

	case CXD2880_TNRDMD_CFG_TS_PACKET_GAP:
		if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP)
			return -EINVAL;

		if (value < 0 || value > 7)
			return -EINVAL;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x00, 0xd6, value,
							 0x07);
		if (ret)
			return ret;

		break;

	case CXD2880_TNRDMD_CFG_TS_BACKWARDS_COMPATIBLE:
		if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP)
			return -EINVAL;

		tnr_dmd->is_ts_backwards_compatible_mode = value ? 1 : 0;

		break;

	case CXD2880_TNRDMD_CFG_PWM_VALUE:
		if (value < 0 || value > 0x1000)
			return -EINVAL;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x00, 0x22,
							 value ? 0x01 : 0x00,
							 0x01);
		if (ret)
			return ret;

		data[0] = (value >> 8) & 0x1f;
		data[1] = value & 0xff;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x00, 0x23,
							 data[0], 0x1f);
		if (ret)
			return ret;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x00, 0x24,
							 data[1], 0xff);
		if (ret)
			return ret;

		break;

	case CXD2880_TNRDMD_CFG_INTERRUPT:
		data[0] = (value >> 8) & 0xff;
		data[1] = value & 0xff;
		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_SYS,
							 0x00, 0x48, data[0],
							 0xff);
		if (ret)
			return ret;
		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_SYS,
							 0x00, 0x49, data[1],
							 0xff);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_INTERRUPT_LOCK_SEL:
		data[0] = value & 0x07;
		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_SYS,
							 0x00, 0x4a, data[0],
							 0x07);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_INTERRUPT_INV_LOCK_SEL:
		data[0] = (value & 0x07) << 3;
		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_SYS,
							 0x00, 0x4a, data[0],
							 0x38);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_FIXED_CLOCKMODE:
		if (value < CXD2880_TNRDMD_CLOCKMODE_UNKNOWN ||
		    value > CXD2880_TNRDMD_CLOCKMODE_C)
			return -EINVAL;
		tnr_dmd->fixed_clk_mode = (enum cxd2880_tnrdmd_clockmode)value;
		break;

	case CXD2880_TNRDMD_CFG_CABLE_INPUT:
		tnr_dmd->is_cable_input = value ? 1 : 0;
		break;

	case CXD2880_TNRDMD_CFG_DVBT2_FEF_INTERMITTENT_BASE:
		tnr_dmd->en_fef_intmtnt_base = value ? 1 : 0;
		break;

	case CXD2880_TNRDMD_CFG_DVBT2_FEF_INTERMITTENT_LITE:
		tnr_dmd->en_fef_intmtnt_lite = value ? 1 : 0;
		break;

	case CXD2880_TNRDMD_CFG_TS_BUF_ALMOST_EMPTY_THRS:
		data[0] = (value >> 8) & 0x07;
		data[1] = value & 0xff;
		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x00, 0x99, data[0],
							 0x07);
		if (ret)
			return ret;
		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x00, 0x9a, data[1],
							 0xff);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_TS_BUF_ALMOST_FULL_THRS:
		data[0] = (value >> 8) & 0x07;
		data[1] = value & 0xff;
		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x00, 0x9b, data[0],
							 0x07);
		if (ret)
			return ret;
		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x00, 0x9c, data[1],
							 0xff);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_TS_BUF_RRDY_THRS:
		data[0] = (value >> 8) & 0x07;
		data[1] = value & 0xff;
		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x00, 0x9d, data[0],
							 0x07);
		if (ret)
			return ret;
		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x00, 0x9e, data[1],
							 0xff);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_BLINDTUNE_DVBT2_FIRST:
		tnr_dmd->blind_tune_dvbt2_first = value ? 1 : 0;
		break;

	case CXD2880_TNRDMD_CFG_DVBT_BERN_PERIOD:
		if (value < 0 || value > 31)
			return -EINVAL;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x10, 0x60,
							 value & 0x1f, 0x1f);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_DVBT_VBER_PERIOD:
		if (value < 0 || value > 7)
			return -EINVAL;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x10, 0x6f,
							 value & 0x07, 0x07);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_DVBT2_BBER_MES:
		if (value < 0 || value > 15)
			return -EINVAL;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x20, 0x72,
							 value & 0x0f, 0x0f);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_DVBT2_LBER_MES:
		if (value < 0 || value > 15)
			return -EINVAL;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x20, 0x6f,
							 value & 0x0f, 0x0f);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_DVBT_PER_MES:
		if (value < 0 || value > 15)
			return -EINVAL;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x10, 0x5c,
							 value & 0x0f, 0x0f);
		if (ret)
			return ret;
		break;

	case CXD2880_TNRDMD_CFG_DVBT2_PER_MES:
		if (value < 0 || value > 15)
			return -EINVAL;

		ret =
		    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
							 CXD2880_IO_TGT_DMD,
							 0x24, 0xdc,
							 value & 0x0f, 0x0f);
		if (ret)
			return ret;
		break;

	default:
		return -EINVAL;
	}

	if (need_sub_setting &&
	    tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN)
		ret = cxd2880_tnrdmd_set_cfg(tnr_dmd->diver_sub, id, value);

	return ret;
}

int cxd2880_tnrdmd_gpio_set_cfg(struct cxd2880_tnrdmd *tnr_dmd,
				u8 id,
				u8 en,
				enum cxd2880_tnrdmd_gpio_mode mode,
				u8 open_drain, u8 invert)
{
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	if (id > 2)
		return -EINVAL;

	if (mode > CXD2880_TNRDMD_GPIO_MODE_EEW)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP &&
	    tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	ret =
	    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd, CXD2880_IO_TGT_SYS,
						 0x00, 0x40 + id, mode,
						 0x0f);
	if (ret)
		return ret;

	ret =
	    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd, CXD2880_IO_TGT_SYS,
						 0x00, 0x43,
						 open_drain ? (1 << id) : 0,
						 1 << id);
	if (ret)
		return ret;

	ret =
	    cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd, CXD2880_IO_TGT_SYS,
						 0x00, 0x44,
						 invert ? (1 << id) : 0,
						 1 << id);
	if (ret)
		return ret;

	return cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
						    CXD2880_IO_TGT_SYS,
						    0x00, 0x45,
						    en ? 0 : (1 << id),
						    1 << id);
}

int cxd2880_tnrdmd_gpio_set_cfg_sub(struct cxd2880_tnrdmd *tnr_dmd,
				    u8 id,
				    u8 en,
				    enum cxd2880_tnrdmd_gpio_mode
				    mode, u8 open_drain, u8 invert)
{
	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_MAIN)
		return -EINVAL;

	return cxd2880_tnrdmd_gpio_set_cfg(tnr_dmd->diver_sub, id, en, mode,
					   open_drain, invert);
}

int cxd2880_tnrdmd_gpio_read(struct cxd2880_tnrdmd *tnr_dmd,
			     u8 id, u8 *value)
{
	u8 data = 0;
	int ret;

	if (!tnr_dmd || !value)
		return -EINVAL;

	if (id > 2)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP &&
	    tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x0a);
	if (ret)
		return ret;
	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x20, &data, 1);
	if (ret)
		return ret;

	*value = (data >> id) & 0x01;

	return 0;
}

int cxd2880_tnrdmd_gpio_read_sub(struct cxd2880_tnrdmd *tnr_dmd,
				 u8 id, u8 *value)
{
	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_MAIN)
		return -EINVAL;

	return cxd2880_tnrdmd_gpio_read(tnr_dmd->diver_sub, id, value);
}

int cxd2880_tnrdmd_gpio_write(struct cxd2880_tnrdmd *tnr_dmd,
			      u8 id, u8 value)
{
	if (!tnr_dmd)
		return -EINVAL;

	if (id > 2)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP &&
	    tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	return cxd2880_tnrdmd_set_and_save_reg_bits(tnr_dmd,
						    CXD2880_IO_TGT_SYS,
						    0x00, 0x46,
						    value ? (1 << id) : 0,
						    1 << id);
}

int cxd2880_tnrdmd_gpio_write_sub(struct cxd2880_tnrdmd *tnr_dmd,
				  u8 id, u8 value)
{
	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_MAIN)
		return -EINVAL;

	return cxd2880_tnrdmd_gpio_write(tnr_dmd->diver_sub, id, value);
}

int cxd2880_tnrdmd_interrupt_read(struct cxd2880_tnrdmd *tnr_dmd,
				  u16 *value)
{
	int ret;
	u8 data[2] = { 0 };

	if (!tnr_dmd || !value)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP &&
	    tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x0a);
	if (ret)
		return ret;
	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x15, data, 2);
	if (ret)
		return ret;

	*value = (data[0] << 8) | data[1];

	return 0;
}

int cxd2880_tnrdmd_interrupt_clear(struct cxd2880_tnrdmd *tnr_dmd,
				   u16 value)
{
	int ret;
	u8 data[2] = { 0 };

	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP &&
	    tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x00);
	if (ret)
		return ret;

	data[0] = (value >> 8) & 0xff;
	data[1] = value & 0xff;

	return tnr_dmd->io->write_regs(tnr_dmd->io,
				       CXD2880_IO_TGT_SYS,
				       0x3c, data, 2);
}

int cxd2880_tnrdmd_ts_buf_clear(struct cxd2880_tnrdmd *tnr_dmd,
				u8 clear_overflow_flag,
				u8 clear_underflow_flag,
				u8 clear_buf)
{
	int ret;
	u8 data[2] = { 0 };

	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP &&
	    tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_DMD,
				     0x00, 0x00);
	if (ret)
		return ret;

	data[0] = clear_overflow_flag ? 0x02 : 0x00;
	data[0] |= clear_underflow_flag ? 0x01 : 0x00;
	data[1] = clear_buf ? 0x01 : 0x00;

	return tnr_dmd->io->write_regs(tnr_dmd->io,
				       CXD2880_IO_TGT_DMD,
				       0x9f, data, 2);
}

int cxd2880_tnrdmd_chip_id(struct cxd2880_tnrdmd *tnr_dmd,
			   enum cxd2880_tnrdmd_chip_id *chip_id)
{
	int ret;
	u8 data = 0;

	if (!tnr_dmd || !chip_id)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x00);
	if (ret)
		return ret;
	ret = tnr_dmd->io->read_regs(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0xfd, &data, 1);
	if (ret)
		return ret;

	*chip_id = (enum cxd2880_tnrdmd_chip_id)data;

	return 0;
}

int cxd2880_tnrdmd_set_and_save_reg_bits(struct cxd2880_tnrdmd
					 *tnr_dmd,
					 enum cxd2880_io_tgt tgt,
					 u8 bank, u8 address,
					 u8 value, u8 bit_mask)
{
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP &&
	    tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io, tgt, 0x00, bank);
	if (ret)
		return ret;

	ret = cxd2880_io_set_reg_bits(tnr_dmd->io,
				      tgt, address, value, bit_mask);
	if (ret)
		return ret;

	return set_cfg_mem(tnr_dmd, tgt, bank, address, value, bit_mask);
}

int cxd2880_tnrdmd_set_scan_mode(struct cxd2880_tnrdmd *tnr_dmd,
				 enum cxd2880_dtv_sys sys,
				 u8 scan_mode_end)
{
	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP &&
	    tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	tnr_dmd->scan_mode = scan_mode_end;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_MAIN)
		return cxd2880_tnrdmd_set_scan_mode(tnr_dmd->diver_sub, sys,
						    scan_mode_end);
	else
		return 0;
}

int cxd2880_tnrdmd_set_pid_ftr(struct cxd2880_tnrdmd *tnr_dmd,
			       struct cxd2880_tnrdmd_pid_ftr_cfg
			       *pid_ftr_cfg)
{
	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP &&
	    tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnr_dmd->create_param.ts_output_if == CXD2880_TNRDMD_TSOUT_IF_TS)
		return -ENOTTY;

	if (pid_ftr_cfg) {
		tnr_dmd->pid_ftr_cfg = *pid_ftr_cfg;
		tnr_dmd->pid_ftr_cfg_en = 1;
	} else {
		tnr_dmd->pid_ftr_cfg_en = 0;
	}

	if (tnr_dmd->state == CXD2880_TNRDMD_STATE_ACTIVE)
		return pid_ftr_setting(tnr_dmd, pid_ftr_cfg);
	else
		return 0;
}

int cxd2880_tnrdmd_set_rf_lvl_cmpstn(struct cxd2880_tnrdmd
				     *tnr_dmd,
				     int (*rf_lvl_cmpstn)
				     (struct cxd2880_tnrdmd *,
				     int *))
{
	if (!tnr_dmd)
		return -EINVAL;

	tnr_dmd->rf_lvl_cmpstn = rf_lvl_cmpstn;

	return 0;
}

int cxd2880_tnrdmd_set_rf_lvl_cmpstn_sub(struct cxd2880_tnrdmd
					 *tnr_dmd,
					 int (*rf_lvl_cmpstn)
					 (struct cxd2880_tnrdmd *,
					 int *))
{
	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_MAIN)
		return -EINVAL;

	return cxd2880_tnrdmd_set_rf_lvl_cmpstn(tnr_dmd->diver_sub,
						rf_lvl_cmpstn);
}

int cxd2880_tnrdmd_set_lna_thrs(struct cxd2880_tnrdmd *tnr_dmd,
				struct cxd2880_tnrdmd_lna_thrs_tbl_air
				*tbl_air,
				struct cxd2880_tnrdmd_lna_thrs_tbl_cable
				*tbl_cable)
{
	if (!tnr_dmd)
		return -EINVAL;

	tnr_dmd->lna_thrs_tbl_air = tbl_air;
	tnr_dmd->lna_thrs_tbl_cable = tbl_cable;

	return 0;
}

int cxd2880_tnrdmd_set_lna_thrs_sub(struct cxd2880_tnrdmd *tnr_dmd,
				    struct
				    cxd2880_tnrdmd_lna_thrs_tbl_air
				    *tbl_air,
				    struct cxd2880_tnrdmd_lna_thrs_tbl_cable
				    *tbl_cable)
{
	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->diver_mode != CXD2880_TNRDMD_DIVERMODE_MAIN)
		return -EINVAL;

	return cxd2880_tnrdmd_set_lna_thrs(tnr_dmd->diver_sub,
					   tbl_air, tbl_cable);
}

int cxd2880_tnrdmd_set_ts_pin_high_low(struct cxd2880_tnrdmd
				       *tnr_dmd, u8 en, u8 value)
{
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP)
		return -EINVAL;

	if (tnr_dmd->create_param.ts_output_if != CXD2880_TNRDMD_TSOUT_IF_TS)
		return -ENOTTY;

	ret = tnr_dmd->io->write_reg(tnr_dmd->io,
				     CXD2880_IO_TGT_SYS,
				     0x00, 0x00);
	if (ret)
		return ret;

	if (en) {
		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_SYS,
					     0x50, ((value & 0x1f) | 0x80));
		if (ret)
			return ret;

		ret = tnr_dmd->io->write_reg(tnr_dmd->io,
					     CXD2880_IO_TGT_SYS,
					     0x52, (value & 0x1f));
	} else {
		ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
						  CXD2880_IO_TGT_SYS,
						  set_ts_pin_seq,
						  ARRAY_SIZE(set_ts_pin_seq));
		if (ret)
			return ret;

		ret = load_cfg_mem(tnr_dmd);
	}

	return ret;
}

int cxd2880_tnrdmd_set_ts_output(struct cxd2880_tnrdmd *tnr_dmd,
				 u8 en)
{
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	if (tnr_dmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnr_dmd->state != CXD2880_TNRDMD_STATE_SLEEP &&
	    tnr_dmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	switch (tnr_dmd->create_param.ts_output_if) {
	case CXD2880_TNRDMD_TSOUT_IF_TS:
		if (en) {
			ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
							  CXD2880_IO_TGT_SYS,
							  set_ts_output_seq1,
							  ARRAY_SIZE(set_ts_output_seq1));
			if (ret)
				return ret;

			ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
							  CXD2880_IO_TGT_DMD,
							  set_ts_output_seq2,
							  ARRAY_SIZE(set_ts_output_seq2));
			if (ret)
				return ret;
		} else {
			ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
							  CXD2880_IO_TGT_DMD,
							  set_ts_output_seq3,
							  ARRAY_SIZE(set_ts_output_seq3));
			if (ret)
				return ret;

			ret = cxd2880_io_write_multi_regs(tnr_dmd->io,
							  CXD2880_IO_TGT_SYS,
							  set_ts_output_seq4,
							  ARRAY_SIZE(set_ts_output_seq4));
			if (ret)
				return ret;
		}
		break;

	case CXD2880_TNRDMD_TSOUT_IF_SPI:
		break;

	case CXD2880_TNRDMD_TSOUT_IF_SDIO:
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

int slvt_freeze_reg(struct cxd2880_tnrdmd *tnr_dmd)
{
	u8 data;
	int ret;

	if (!tnr_dmd)
		return -EINVAL;

	switch (tnr_dmd->create_param.ts_output_if) {
	case CXD2880_TNRDMD_TSOUT_IF_SPI:
	case CXD2880_TNRDMD_TSOUT_IF_SDIO:

		ret = tnr_dmd->io->read_regs(tnr_dmd->io,
					     CXD2880_IO_TGT_DMD,
					     0x00, &data, 1);
		if (ret)
			return ret;

		break;
	case CXD2880_TNRDMD_TSOUT_IF_TS:
	default:
		break;
	}

	return tnr_dmd->io->write_reg(tnr_dmd->io,
				      CXD2880_IO_TGT_DMD,
				      0x01, 0x01);
}
