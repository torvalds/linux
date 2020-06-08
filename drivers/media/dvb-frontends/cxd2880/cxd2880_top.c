// SPDX-License-Identifier: GPL-2.0
/*
 * cxd2880_top.c
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, __func__

#include <linux/spi/spi.h>

#include <media/dvb_frontend.h>
#include <media/dvb_math.h>

#include "cxd2880.h"
#include "cxd2880_tnrdmd_mon.h"
#include "cxd2880_tnrdmd_dvbt2_mon.h"
#include "cxd2880_tnrdmd_dvbt_mon.h"
#include "cxd2880_integ.h"
#include "cxd2880_tnrdmd_dvbt2.h"
#include "cxd2880_tnrdmd_dvbt.h"
#include "cxd2880_devio_spi.h"
#include "cxd2880_spi_device.h"
#include "cxd2880_tnrdmd_driver_version.h"

struct cxd2880_priv {
	struct cxd2880_tnrdmd tnrdmd;
	struct spi_device *spi;
	struct cxd2880_io regio;
	struct cxd2880_spi_device spi_device;
	struct cxd2880_spi cxd2880_spi;
	struct cxd2880_dvbt_tune_param dvbt_tune_param;
	struct cxd2880_dvbt2_tune_param dvbt2_tune_param;
	struct mutex *spi_mutex; /* For SPI access exclusive control */
	unsigned long pre_ber_update;
	unsigned long pre_ber_interval;
	unsigned long post_ber_update;
	unsigned long post_ber_interval;
	unsigned long ucblock_update;
	unsigned long ucblock_interval;
	enum fe_status s;
};

static int cxd2880_pre_bit_err_t(struct cxd2880_tnrdmd *tnrdmd,
				 u32 *pre_bit_err, u32 *pre_bit_count)
{
	u8 rdata[2];
	int ret;

	if (!tnrdmd || !pre_bit_err || !pre_bit_count)
		return -EINVAL;

	if (tnrdmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnrdmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnrdmd->sys != CXD2880_DTV_SYS_DVBT)
		return -EINVAL;

	ret = slvt_freeze_reg(tnrdmd);
	if (ret)
		return ret;

	ret = tnrdmd->io->write_reg(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x00, 0x10);
	if (ret) {
		slvt_unfreeze_reg(tnrdmd);
		return ret;
	}

	ret = tnrdmd->io->read_regs(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x39, rdata, 1);
	if (ret) {
		slvt_unfreeze_reg(tnrdmd);
		return ret;
	}

	if ((rdata[0] & 0x01) == 0) {
		slvt_unfreeze_reg(tnrdmd);
		return -EAGAIN;
	}

	ret = tnrdmd->io->read_regs(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x22, rdata, 2);
	if (ret) {
		slvt_unfreeze_reg(tnrdmd);
		return ret;
	}

	*pre_bit_err = (rdata[0] << 8) | rdata[1];

	ret = tnrdmd->io->read_regs(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x6f, rdata, 1);
	if (ret) {
		slvt_unfreeze_reg(tnrdmd);
		return ret;
	}

	slvt_unfreeze_reg(tnrdmd);

	*pre_bit_count = ((rdata[0] & 0x07) == 0) ?
			 256 : (0x1000 << (rdata[0] & 0x07));

	return 0;
}

static int cxd2880_pre_bit_err_t2(struct cxd2880_tnrdmd *tnrdmd,
				  u32 *pre_bit_err,
				  u32 *pre_bit_count)
{
	u32 period_exp = 0;
	u32 n_ldpc = 0;
	u8 data[5];
	int ret;

	if (!tnrdmd || !pre_bit_err || !pre_bit_count)
		return -EINVAL;

	if (tnrdmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnrdmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnrdmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = slvt_freeze_reg(tnrdmd);
	if (ret)
		return ret;

	ret = tnrdmd->io->write_reg(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x00, 0x0b);
	if (ret) {
		slvt_unfreeze_reg(tnrdmd);
		return ret;
	}

	ret = tnrdmd->io->read_regs(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x3c, data, sizeof(data));
	if (ret) {
		slvt_unfreeze_reg(tnrdmd);
		return ret;
	}

	if (!(data[0] & 0x01)) {
		slvt_unfreeze_reg(tnrdmd);
		return -EAGAIN;
	}
	*pre_bit_err =
	((data[1] & 0x0f) << 24) | (data[2] << 16) | (data[3] << 8) | data[4];

	ret = tnrdmd->io->read_regs(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0xa0, data, 1);
	if (ret) {
		slvt_unfreeze_reg(tnrdmd);
		return ret;
	}

	if (((enum cxd2880_dvbt2_plp_fec)(data[0] & 0x03)) ==
	    CXD2880_DVBT2_FEC_LDPC_16K)
		n_ldpc = 16200;
	else
		n_ldpc = 64800;
	slvt_unfreeze_reg(tnrdmd);

	ret = tnrdmd->io->write_reg(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x00, 0x20);
	if (ret)
		return ret;

	ret = tnrdmd->io->read_regs(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x6f, data, 1);
	if (ret)
		return ret;

	period_exp = data[0] & 0x0f;

	*pre_bit_count = (1U << period_exp) * n_ldpc;

	return 0;
}

static int cxd2880_post_bit_err_t(struct cxd2880_tnrdmd *tnrdmd,
				  u32 *post_bit_err,
				  u32 *post_bit_count)
{
	u8 rdata[3];
	u32 bit_error = 0;
	u32 period_exp = 0;
	int ret;

	if (!tnrdmd || !post_bit_err || !post_bit_count)
		return -EINVAL;

	if (tnrdmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnrdmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnrdmd->sys != CXD2880_DTV_SYS_DVBT)
		return -EINVAL;

	ret = tnrdmd->io->write_reg(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x00, 0x0d);
	if (ret)
		return ret;

	ret = tnrdmd->io->read_regs(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x15, rdata, 3);
	if (ret)
		return ret;

	if ((rdata[0] & 0x40) == 0)
		return -EAGAIN;

	*post_bit_err = ((rdata[0] & 0x3f) << 16) | (rdata[1] << 8) | rdata[2];

	ret = tnrdmd->io->write_reg(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x00, 0x10);
	if (ret)
		return ret;

	ret = tnrdmd->io->read_regs(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x60, rdata, 1);
	if (ret)
		return ret;

	period_exp = (rdata[0] & 0x1f);

	if (period_exp <= 11 && (bit_error > (1U << period_exp) * 204 * 8))
		return -EAGAIN;

	*post_bit_count = (1U << period_exp) * 204 * 8;

	return 0;
}

static int cxd2880_post_bit_err_t2(struct cxd2880_tnrdmd *tnrdmd,
				   u32 *post_bit_err,
				   u32 *post_bit_count)
{
	u32 period_exp = 0;
	u32 n_bch = 0;
	u8 data[3];
	enum cxd2880_dvbt2_plp_fec plp_fec_type =
		CXD2880_DVBT2_FEC_LDPC_16K;
	enum cxd2880_dvbt2_plp_code_rate plp_code_rate =
		CXD2880_DVBT2_R1_2;
	int ret;
	static const u16 n_bch_bits_lookup[2][8] = {
		{7200, 9720, 10800, 11880, 12600, 13320, 5400, 6480},
		{32400, 38880, 43200, 48600, 51840, 54000, 21600, 25920}
	};

	if (!tnrdmd || !post_bit_err || !post_bit_count)
		return -EINVAL;

	if (tnrdmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnrdmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnrdmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = slvt_freeze_reg(tnrdmd);
	if (ret)
		return ret;

	ret = tnrdmd->io->write_reg(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x00, 0x0b);
	if (ret) {
		slvt_unfreeze_reg(tnrdmd);
		return ret;
	}

	ret = tnrdmd->io->read_regs(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x15, data, 3);
	if (ret) {
		slvt_unfreeze_reg(tnrdmd);
		return ret;
	}

	if (!(data[0] & 0x40)) {
		slvt_unfreeze_reg(tnrdmd);
		return -EAGAIN;
	}

	*post_bit_err =
		((data[0] & 0x3f) << 16) | (data[1] << 8) | data[2];

	ret = tnrdmd->io->read_regs(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x9d, data, 1);
	if (ret) {
		slvt_unfreeze_reg(tnrdmd);
		return ret;
	}

	plp_code_rate =
	(enum cxd2880_dvbt2_plp_code_rate)(data[0] & 0x07);

	ret = tnrdmd->io->read_regs(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0xa0, data, 1);
	if (ret) {
		slvt_unfreeze_reg(tnrdmd);
		return ret;
	}

	plp_fec_type = (enum cxd2880_dvbt2_plp_fec)(data[0] & 0x03);

	slvt_unfreeze_reg(tnrdmd);

	ret = tnrdmd->io->write_reg(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x00, 0x20);
	if (ret)
		return ret;

	ret = tnrdmd->io->read_regs(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x72, data, 1);
	if (ret)
		return ret;

	period_exp = data[0] & 0x0f;

	if (plp_fec_type > CXD2880_DVBT2_FEC_LDPC_64K ||
	    plp_code_rate > CXD2880_DVBT2_R2_5)
		return -EAGAIN;

	n_bch = n_bch_bits_lookup[plp_fec_type][plp_code_rate];

	if (*post_bit_err > ((1U << period_exp) * n_bch))
		return -EAGAIN;

	*post_bit_count = (1U << period_exp) * n_bch;

	return 0;
}

static int cxd2880_read_block_err_t(struct cxd2880_tnrdmd *tnrdmd,
				    u32 *block_err,
				    u32 *block_count)
{
	u8 rdata[3];
	int ret;

	if (!tnrdmd || !block_err || !block_count)
		return -EINVAL;

	if (tnrdmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnrdmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;

	if (tnrdmd->sys != CXD2880_DTV_SYS_DVBT)
		return -EINVAL;

	ret = tnrdmd->io->write_reg(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x00, 0x0d);
	if (ret)
		return ret;

	ret = tnrdmd->io->read_regs(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x18, rdata, 3);
	if (ret)
		return ret;

	if ((rdata[0] & 0x01) == 0)
		return -EAGAIN;

	*block_err = (rdata[1] << 8) | rdata[2];

	ret = tnrdmd->io->write_reg(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x00, 0x10);
	if (ret)
		return ret;

	ret = tnrdmd->io->read_regs(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x5c, rdata, 1);
	if (ret)
		return ret;

	*block_count = 1U << (rdata[0] & 0x0f);

	if ((*block_count == 0) || (*block_err > *block_count))
		return -EAGAIN;

	return 0;
}

static int cxd2880_read_block_err_t2(struct cxd2880_tnrdmd *tnrdmd,
				     u32 *block_err,
				     u32 *block_count)
{
	u8 rdata[3];
	int ret;

	if (!tnrdmd || !block_err || !block_count)
		return -EINVAL;

	if (tnrdmd->diver_mode == CXD2880_TNRDMD_DIVERMODE_SUB)
		return -EINVAL;

	if (tnrdmd->state != CXD2880_TNRDMD_STATE_ACTIVE)
		return -EINVAL;
	if (tnrdmd->sys != CXD2880_DTV_SYS_DVBT2)
		return -EINVAL;

	ret = tnrdmd->io->write_reg(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x00, 0x0b);
	if (ret)
		return ret;

	ret = tnrdmd->io->read_regs(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x18, rdata, 3);
	if (ret)
		return ret;

	if ((rdata[0] & 0x01) == 0)
		return -EAGAIN;

	*block_err = (rdata[1] << 8) | rdata[2];

	ret = tnrdmd->io->write_reg(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0x00, 0x24);
	if (ret)
		return ret;

	ret = tnrdmd->io->read_regs(tnrdmd->io,
				    CXD2880_IO_TGT_DMD,
				    0xdc, rdata, 1);
	if (ret)
		return ret;

	*block_count = 1U << (rdata[0] & 0x0f);

	if ((*block_count == 0) || (*block_err > *block_count))
		return -EAGAIN;

	return 0;
}

static void cxd2880_release(struct dvb_frontend *fe)
{
	struct cxd2880_priv *priv = NULL;

	if (!fe) {
		pr_err("invalid arg.\n");
		return;
	}
	priv = fe->demodulator_priv;
	kfree(priv);
}

static int cxd2880_init(struct dvb_frontend *fe)
{
	int ret;
	struct cxd2880_priv *priv = NULL;
	struct cxd2880_tnrdmd_create_param create_param;

	if (!fe) {
		pr_err("invalid arg.\n");
		return -EINVAL;
	}

	priv = fe->demodulator_priv;

	create_param.ts_output_if = CXD2880_TNRDMD_TSOUT_IF_SPI;
	create_param.xtal_share_type = CXD2880_TNRDMD_XTAL_SHARE_NONE;
	create_param.en_internal_ldo = 1;
	create_param.xosc_cap = 18;
	create_param.xosc_i = 8;
	create_param.stationary_use = 1;

	mutex_lock(priv->spi_mutex);
	if (priv->tnrdmd.io != &priv->regio) {
		ret = cxd2880_tnrdmd_create(&priv->tnrdmd,
					    &priv->regio, &create_param);
		if (ret) {
			mutex_unlock(priv->spi_mutex);
			pr_info("cxd2880 tnrdmd create failed %d\n", ret);
			return ret;
		}
	}
	ret = cxd2880_integ_init(&priv->tnrdmd);
	if (ret) {
		mutex_unlock(priv->spi_mutex);
		pr_err("cxd2880 integ init failed %d\n", ret);
		return ret;
	}

	ret = cxd2880_tnrdmd_set_cfg(&priv->tnrdmd,
				     CXD2880_TNRDMD_CFG_TSPIN_CURRENT,
				     0x00);
	if (ret) {
		mutex_unlock(priv->spi_mutex);
		pr_err("cxd2880 set config failed %d\n", ret);
		return ret;
	}
	mutex_unlock(priv->spi_mutex);

	pr_debug("OK.\n");

	return ret;
}

static int cxd2880_sleep(struct dvb_frontend *fe)
{
	int ret;
	struct cxd2880_priv *priv = NULL;

	if (!fe) {
		pr_err("invalid arg\n");
		return -EINVAL;
	}

	priv = fe->demodulator_priv;

	mutex_lock(priv->spi_mutex);
	ret = cxd2880_tnrdmd_sleep(&priv->tnrdmd);
	mutex_unlock(priv->spi_mutex);

	pr_debug("tnrdmd_sleep ret %d\n", ret);

	return ret;
}

static int cxd2880_read_signal_strength(struct dvb_frontend *fe,
					u16 *strength)
{
	int ret;
	struct cxd2880_priv *priv = NULL;
	struct dtv_frontend_properties *c = NULL;
	int level = 0;

	if (!fe || !strength) {
		pr_err("invalid arg\n");
		return -EINVAL;
	}

	priv = fe->demodulator_priv;
	c = &fe->dtv_property_cache;

	mutex_lock(priv->spi_mutex);
	if (c->delivery_system == SYS_DVBT ||
	    c->delivery_system == SYS_DVBT2) {
		ret = cxd2880_tnrdmd_mon_rf_lvl(&priv->tnrdmd, &level);
	} else {
		pr_debug("invalid system\n");
		mutex_unlock(priv->spi_mutex);
		return -EINVAL;
	}
	mutex_unlock(priv->spi_mutex);

	level /= 125;
	/*
	 * level should be between -105dBm and -30dBm.
	 * E.g. they should be between:
	 * -105000/125 = -840 and -30000/125 = -240
	 */
	level = clamp(level, -840, -240);
	/* scale value to 0x0000-0xffff */
	*strength = ((level + 840) * 0xffff) / (-240 + 840);

	if (ret)
		pr_debug("ret = %d\n", ret);

	return ret;
}

static int cxd2880_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	int ret;
	int snrvalue = 0;
	struct cxd2880_priv *priv = NULL;
	struct dtv_frontend_properties *c = NULL;

	if (!fe || !snr) {
		pr_err("invalid arg\n");
		return -EINVAL;
	}

	priv = fe->demodulator_priv;
	c = &fe->dtv_property_cache;

	mutex_lock(priv->spi_mutex);
	if (c->delivery_system == SYS_DVBT) {
		ret = cxd2880_tnrdmd_dvbt_mon_snr(&priv->tnrdmd,
						  &snrvalue);
	} else if (c->delivery_system == SYS_DVBT2) {
		ret = cxd2880_tnrdmd_dvbt2_mon_snr(&priv->tnrdmd,
						   &snrvalue);
	} else {
		pr_err("invalid system\n");
		mutex_unlock(priv->spi_mutex);
		return -EINVAL;
	}
	mutex_unlock(priv->spi_mutex);

	if (snrvalue < 0)
		snrvalue = 0;
	*snr = snrvalue;

	if (ret)
		pr_debug("ret = %d\n", ret);

	return ret;
}

static int cxd2880_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	int ret;
	struct cxd2880_priv *priv = NULL;
	struct dtv_frontend_properties *c = NULL;

	if (!fe || !ucblocks) {
		pr_err("invalid arg\n");
		return -EINVAL;
	}

	priv = fe->demodulator_priv;
	c = &fe->dtv_property_cache;

	mutex_lock(priv->spi_mutex);
	if (c->delivery_system == SYS_DVBT) {
		ret = cxd2880_tnrdmd_dvbt_mon_packet_error_number(&priv->tnrdmd,
								  ucblocks);
	} else if (c->delivery_system == SYS_DVBT2) {
		ret = cxd2880_tnrdmd_dvbt2_mon_packet_error_number(&priv->tnrdmd,
								   ucblocks);
	} else {
		pr_err("invalid system\n");
		mutex_unlock(priv->spi_mutex);
		return -EINVAL;
	}
	mutex_unlock(priv->spi_mutex);

	if (ret)
		pr_debug("ret = %d\n", ret);

	return ret;
}

static int cxd2880_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	*ber = 0;

	return 0;
}

static int cxd2880_set_ber_per_period_t(struct dvb_frontend *fe)
{
	int ret;
	struct cxd2880_priv *priv;
	struct cxd2880_dvbt_tpsinfo info;
	enum cxd2880_dtv_bandwidth bw;
	u32 pre_ber_rate = 0;
	u32 post_ber_rate = 0;
	u32 ucblock_rate = 0;
	u32 mes_exp = 0;
	static const int cr_table[5] = {31500, 42000, 47250, 52500, 55125};
	static const int denominator_tbl[4] = {125664, 129472, 137088, 152320};

	if (!fe) {
		pr_err("invalid arg\n");
		return -EINVAL;
	}

	priv = fe->demodulator_priv;
	bw = priv->dvbt_tune_param.bandwidth;

	ret = cxd2880_tnrdmd_dvbt_mon_tps_info(&priv->tnrdmd,
					       &info);
	if (ret) {
		pr_err("tps monitor error ret = %d\n", ret);
		info.hierarchy = CXD2880_DVBT_HIERARCHY_NON;
		info.constellation = CXD2880_DVBT_CONSTELLATION_QPSK;
		info.guard = CXD2880_DVBT_GUARD_1_4;
		info.rate_hp = CXD2880_DVBT_CODERATE_1_2;
		info.rate_lp = CXD2880_DVBT_CODERATE_1_2;
	}

	if (info.hierarchy == CXD2880_DVBT_HIERARCHY_NON) {
		pre_ber_rate = 63000000 * bw * (info.constellation * 2 + 2) /
			       denominator_tbl[info.guard];

		post_ber_rate =	1000 * cr_table[info.rate_hp] * bw *
				(info.constellation * 2 + 2) /
				denominator_tbl[info.guard];

		ucblock_rate = 875 * cr_table[info.rate_hp] * bw *
			       (info.constellation * 2 + 2) /
			       denominator_tbl[info.guard];
	} else {
		u8 data = 0;
		struct cxd2880_tnrdmd *tnrdmd = &priv->tnrdmd;

		ret = tnrdmd->io->write_reg(tnrdmd->io,
					    CXD2880_IO_TGT_DMD,
					    0x00, 0x10);
		if (!ret) {
			ret = tnrdmd->io->read_regs(tnrdmd->io,
						    CXD2880_IO_TGT_DMD,
						    0x67, &data, 1);
			if (ret)
				data = 0x00;
		} else {
			data = 0x00;
		}

		if (data & 0x01) { /* Low priority */
			pre_ber_rate =
				63000000 * bw * (info.constellation * 2 + 2) /
				denominator_tbl[info.guard];

			post_ber_rate = 1000 * cr_table[info.rate_lp] * bw *
					(info.constellation * 2 + 2) /
					denominator_tbl[info.guard];

			ucblock_rate = (1000 * 7 / 8) *	cr_table[info.rate_lp] *
				       bw * (info.constellation * 2 + 2) /
				       denominator_tbl[info.guard];
		} else { /* High priority */
			pre_ber_rate =
				63000000 * bw * 2 / denominator_tbl[info.guard];

			post_ber_rate = 1000 * cr_table[info.rate_hp] * bw * 2 /
					denominator_tbl[info.guard];

			ucblock_rate = (1000 * 7 / 8) * cr_table[info.rate_hp] *
					bw * 2 / denominator_tbl[info.guard];
		}
	}

	mes_exp = pre_ber_rate < 8192 ? 8 : intlog2(pre_ber_rate) >> 24;
	priv->pre_ber_interval =
		((1U << mes_exp) * 1000 + (pre_ber_rate / 2)) /
		pre_ber_rate;
	cxd2880_tnrdmd_set_cfg(&priv->tnrdmd,
			       CXD2880_TNRDMD_CFG_DVBT_VBER_PERIOD,
			       mes_exp == 8 ? 0 : mes_exp - 12);

	mes_exp = intlog2(post_ber_rate) >> 24;
	priv->post_ber_interval =
		((1U << mes_exp) * 1000 + (post_ber_rate / 2)) /
		post_ber_rate;
	cxd2880_tnrdmd_set_cfg(&priv->tnrdmd,
			       CXD2880_TNRDMD_CFG_DVBT_BERN_PERIOD,
			       mes_exp);

	mes_exp = intlog2(ucblock_rate) >> 24;
	priv->ucblock_interval =
		((1U << mes_exp) * 1000 + (ucblock_rate / 2)) /
		ucblock_rate;
	cxd2880_tnrdmd_set_cfg(&priv->tnrdmd,
			       CXD2880_TNRDMD_CFG_DVBT_PER_MES,
			       mes_exp);

	return 0;
}

static int cxd2880_set_ber_per_period_t2(struct dvb_frontend *fe)
{
	int ret;
	struct cxd2880_priv *priv;
	struct cxd2880_dvbt2_l1pre l1pre;
	struct cxd2880_dvbt2_l1post l1post;
	struct cxd2880_dvbt2_plp plp;
	struct cxd2880_dvbt2_bbheader bbheader;
	enum cxd2880_dtv_bandwidth bw = CXD2880_DTV_BW_1_7_MHZ;
	u32 pre_ber_rate = 0;
	u32 post_ber_rate = 0;
	u32 ucblock_rate = 0;
	u32 mes_exp = 0;
	u32 term_a = 0;
	u32 term_b = 0;
	u32 denominator = 0;
	static const u32 gi_tbl[7] = {32, 64, 128, 256, 8, 152, 76};
	static const u8 n_tbl[6] = {8, 2, 4, 16, 1, 1};
	static const u8 mode_tbl[6] = {2, 8, 4, 1, 16, 32};
	static const u32 kbch_tbl[2][8] = {
		{6952, 9472, 10552, 11632, 12352, 13072, 5152, 6232},
		{32128, 38608, 42960, 48328, 51568, 53760, 0, 0}
	};

	if (!fe) {
		pr_err("invalid arg\n");
		return -EINVAL;
	}

	priv = fe->demodulator_priv;
	bw = priv->dvbt2_tune_param.bandwidth;

	ret = cxd2880_tnrdmd_dvbt2_mon_l1_pre(&priv->tnrdmd, &l1pre);
	if (ret) {
		pr_info("l1 pre error\n");
		goto error_ber_setting;
	}

	ret = cxd2880_tnrdmd_dvbt2_mon_active_plp(&priv->tnrdmd,
						  CXD2880_DVBT2_PLP_DATA, &plp);
	if (ret) {
		pr_info("plp info error\n");
		goto error_ber_setting;
	}

	ret = cxd2880_tnrdmd_dvbt2_mon_l1_post(&priv->tnrdmd, &l1post);
	if (ret) {
		pr_info("l1 post error\n");
		goto error_ber_setting;
	}

	term_a =
		(mode_tbl[l1pre.fft_mode] * (1024 + gi_tbl[l1pre.gi])) *
		(l1pre.num_symbols + n_tbl[l1pre.fft_mode]) + 2048;

	if (l1pre.mixed && l1post.fef_intvl) {
		term_b = (l1post.fef_length + (l1post.fef_intvl / 2)) /
			 l1post.fef_intvl;
	} else {
		term_b = 0;
	}

	switch (bw) {
	case CXD2880_DTV_BW_1_7_MHZ:
		denominator = ((term_a + term_b) * 71 + (131 / 2)) / 131;
		break;
	case CXD2880_DTV_BW_5_MHZ:
		denominator = ((term_a + term_b) * 7 + 20) / 40;
		break;
	case CXD2880_DTV_BW_6_MHZ:
		denominator = ((term_a + term_b) * 7 + 24) / 48;
		break;
	case CXD2880_DTV_BW_7_MHZ:
		denominator = ((term_a + term_b) + 4) / 8;
		break;
	case CXD2880_DTV_BW_8_MHZ:
	default:
		denominator = ((term_a + term_b) * 7 + 32) / 64;
		break;
	}

	if (plp.til_type && plp.til_len) {
		pre_ber_rate =
			(plp.num_blocks_max * 1000000 + (denominator / 2)) /
			denominator;
		pre_ber_rate = (pre_ber_rate + (plp.til_len / 2)) /
			       plp.til_len;
	} else {
		pre_ber_rate =
			(plp.num_blocks_max * 1000000 + (denominator / 2)) /
			denominator;
	}

	post_ber_rate = pre_ber_rate;

	mes_exp = intlog2(pre_ber_rate) >> 24;
	priv->pre_ber_interval =
		((1U << mes_exp) * 1000 + (pre_ber_rate / 2)) /
		pre_ber_rate;
	cxd2880_tnrdmd_set_cfg(&priv->tnrdmd,
			       CXD2880_TNRDMD_CFG_DVBT2_LBER_MES,
			       mes_exp);

	mes_exp = intlog2(post_ber_rate) >> 24;
	priv->post_ber_interval =
		((1U << mes_exp) * 1000 + (post_ber_rate / 2)) /
		post_ber_rate;
	cxd2880_tnrdmd_set_cfg(&priv->tnrdmd,
			       CXD2880_TNRDMD_CFG_DVBT2_BBER_MES,
			       mes_exp);

	ret = cxd2880_tnrdmd_dvbt2_mon_bbheader(&priv->tnrdmd,
						CXD2880_DVBT2_PLP_DATA,
						&bbheader);
	if (ret) {
		pr_info("bb header error\n");
		goto error_ucblock_setting;
	}

	if (bbheader.plp_mode == CXD2880_DVBT2_PLP_MODE_NM) {
		if (!bbheader.issy_indicator) {
			ucblock_rate =
				(pre_ber_rate * kbch_tbl[plp.fec][plp.plp_cr] +
				752) / 1504;
		} else {
			ucblock_rate =
				(pre_ber_rate * kbch_tbl[plp.fec][plp.plp_cr] +
				764) / 1528;
		}
	} else if (bbheader.plp_mode == CXD2880_DVBT2_PLP_MODE_HEM) {
		ucblock_rate =
			(pre_ber_rate * kbch_tbl[plp.fec][plp.plp_cr] + 748) /
			1496;
	} else {
		pr_info("plp mode is not Normal or HEM\n");
		goto error_ucblock_setting;
	}

	mes_exp = intlog2(ucblock_rate) >> 24;
	priv->ucblock_interval =
		((1U << mes_exp) * 1000 + (ucblock_rate / 2)) /
		ucblock_rate;
	cxd2880_tnrdmd_set_cfg(&priv->tnrdmd,
			       CXD2880_TNRDMD_CFG_DVBT2_PER_MES,
			       mes_exp);

	return 0;

error_ber_setting:
	priv->pre_ber_interval = 1000;
	cxd2880_tnrdmd_set_cfg(&priv->tnrdmd,
				     CXD2880_TNRDMD_CFG_DVBT2_LBER_MES, 0);

	priv->post_ber_interval = 1000;
	cxd2880_tnrdmd_set_cfg(&priv->tnrdmd,
			       CXD2880_TNRDMD_CFG_DVBT2_BBER_MES, 0);

error_ucblock_setting:
	priv->ucblock_interval = 1000;
	cxd2880_tnrdmd_set_cfg(&priv->tnrdmd,
			       CXD2880_TNRDMD_CFG_DVBT2_PER_MES, 8);

	return 0;
}

static int cxd2880_dvbt_tune(struct cxd2880_tnrdmd *tnr_dmd,
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

	atomic_set(&tnr_dmd->cancel, 0);

	if (tune_param->bandwidth != CXD2880_DTV_BW_5_MHZ &&
	    tune_param->bandwidth != CXD2880_DTV_BW_6_MHZ &&
	    tune_param->bandwidth != CXD2880_DTV_BW_7_MHZ &&
	    tune_param->bandwidth != CXD2880_DTV_BW_8_MHZ) {
		return -ENOTTY;
	}

	ret = cxd2880_tnrdmd_dvbt_tune1(tnr_dmd, tune_param);
	if (ret)
		return ret;

	usleep_range(CXD2880_TNRDMD_WAIT_AGC_STABLE * 10000,
		     CXD2880_TNRDMD_WAIT_AGC_STABLE * 10000 + 1000);

	return cxd2880_tnrdmd_dvbt_tune2(tnr_dmd, tune_param);
}

static int cxd2880_dvbt2_tune(struct cxd2880_tnrdmd *tnr_dmd,
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

	atomic_set(&tnr_dmd->cancel, 0);

	if (tune_param->bandwidth != CXD2880_DTV_BW_1_7_MHZ &&
	    tune_param->bandwidth != CXD2880_DTV_BW_5_MHZ &&
	    tune_param->bandwidth != CXD2880_DTV_BW_6_MHZ &&
	    tune_param->bandwidth != CXD2880_DTV_BW_7_MHZ &&
	    tune_param->bandwidth != CXD2880_DTV_BW_8_MHZ) {
		return -ENOTTY;
	}

	if (tune_param->profile != CXD2880_DVBT2_PROFILE_BASE &&
	    tune_param->profile != CXD2880_DVBT2_PROFILE_LITE)
		return -EINVAL;

	ret = cxd2880_tnrdmd_dvbt2_tune1(tnr_dmd, tune_param);
	if (ret)
		return ret;

	usleep_range(CXD2880_TNRDMD_WAIT_AGC_STABLE * 10000,
		     CXD2880_TNRDMD_WAIT_AGC_STABLE * 10000 + 1000);

	return cxd2880_tnrdmd_dvbt2_tune2(tnr_dmd, tune_param);
}

static int cxd2880_set_frontend(struct dvb_frontend *fe)
{
	int ret;
	struct dtv_frontend_properties *c;
	struct cxd2880_priv *priv;
	enum cxd2880_dtv_bandwidth bw = CXD2880_DTV_BW_1_7_MHZ;

	if (!fe) {
		pr_err("invalid arg\n");
		return -EINVAL;
	}

	priv = fe->demodulator_priv;
	c = &fe->dtv_property_cache;

	c->pre_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->pre_bit_error.stat[0].uvalue = 0;
	c->pre_bit_error.len = 1;
	c->pre_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->pre_bit_count.stat[0].uvalue = 0;
	c->pre_bit_count.len = 1;
	c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->post_bit_error.stat[0].uvalue = 0;
	c->post_bit_error.len = 1;
	c->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->post_bit_count.stat[0].uvalue = 0;
	c->post_bit_count.len = 1;
	c->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->block_error.stat[0].uvalue = 0;
	c->block_error.len = 1;
	c->block_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->block_count.stat[0].uvalue = 0;
	c->block_count.len = 1;

	switch (c->bandwidth_hz) {
	case 1712000:
		bw = CXD2880_DTV_BW_1_7_MHZ;
		break;
	case 5000000:
		bw = CXD2880_DTV_BW_5_MHZ;
		break;
	case 6000000:
		bw = CXD2880_DTV_BW_6_MHZ;
		break;
	case 7000000:
		bw = CXD2880_DTV_BW_7_MHZ;
		break;
	case 8000000:
		bw = CXD2880_DTV_BW_8_MHZ;
		break;
	default:
		return -EINVAL;
	}

	priv->s = 0;

	pr_info("sys:%d freq:%d bw:%d\n",
		c->delivery_system, c->frequency, bw);
	mutex_lock(priv->spi_mutex);
	if (c->delivery_system == SYS_DVBT) {
		priv->tnrdmd.sys = CXD2880_DTV_SYS_DVBT;
		priv->dvbt_tune_param.center_freq_khz = c->frequency / 1000;
		priv->dvbt_tune_param.bandwidth = bw;
		priv->dvbt_tune_param.profile = CXD2880_DVBT_PROFILE_HP;
		ret = cxd2880_dvbt_tune(&priv->tnrdmd,
					&priv->dvbt_tune_param);
	} else if (c->delivery_system == SYS_DVBT2) {
		priv->tnrdmd.sys = CXD2880_DTV_SYS_DVBT2;
		priv->dvbt2_tune_param.center_freq_khz = c->frequency / 1000;
		priv->dvbt2_tune_param.bandwidth = bw;
		priv->dvbt2_tune_param.data_plp_id = (u16)c->stream_id;
		priv->dvbt2_tune_param.profile = CXD2880_DVBT2_PROFILE_BASE;
		ret = cxd2880_dvbt2_tune(&priv->tnrdmd,
					 &priv->dvbt2_tune_param);
	} else {
		pr_err("invalid system\n");
		mutex_unlock(priv->spi_mutex);
		return -EINVAL;
	}
	mutex_unlock(priv->spi_mutex);

	pr_info("tune result %d\n", ret);

	return ret;
}

static int cxd2880_get_stats(struct dvb_frontend *fe,
			     enum fe_status status)
{
	struct cxd2880_priv *priv = NULL;
	struct dtv_frontend_properties *c = NULL;
	u32 pre_bit_err = 0, pre_bit_count = 0;
	u32 post_bit_err = 0, post_bit_count = 0;
	u32 block_err = 0, block_count = 0;
	int ret;

	if (!fe) {
		pr_err("invalid arg\n");
		return -EINVAL;
	}

	priv = fe->demodulator_priv;
	c = &fe->dtv_property_cache;

	if (!(status & FE_HAS_LOCK) || !(status & FE_HAS_CARRIER)) {
		c->pre_bit_error.len = 1;
		c->pre_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->pre_bit_count.len = 1;
		c->pre_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->post_bit_error.len = 1;
		c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->post_bit_count.len = 1;
		c->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->block_error.len = 1;
		c->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->block_count.len = 1;
		c->block_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

		return 0;
	}

	if (time_after(jiffies, priv->pre_ber_update)) {
		priv->pre_ber_update =
			 jiffies + msecs_to_jiffies(priv->pre_ber_interval);
		if (c->delivery_system == SYS_DVBT) {
			mutex_lock(priv->spi_mutex);
			ret = cxd2880_pre_bit_err_t(&priv->tnrdmd,
						    &pre_bit_err,
						    &pre_bit_count);
			mutex_unlock(priv->spi_mutex);
		} else if (c->delivery_system == SYS_DVBT2) {
			mutex_lock(priv->spi_mutex);
			ret = cxd2880_pre_bit_err_t2(&priv->tnrdmd,
						     &pre_bit_err,
						     &pre_bit_count);
			mutex_unlock(priv->spi_mutex);
		} else {
			return -EINVAL;
		}

		if (!ret) {
			c->pre_bit_error.len = 1;
			c->pre_bit_error.stat[0].scale = FE_SCALE_COUNTER;
			c->pre_bit_error.stat[0].uvalue += pre_bit_err;
			c->pre_bit_count.len = 1;
			c->pre_bit_count.stat[0].scale = FE_SCALE_COUNTER;
			c->pre_bit_count.stat[0].uvalue += pre_bit_count;
		} else {
			c->pre_bit_error.len = 1;
			c->pre_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
			c->pre_bit_count.len = 1;
			c->pre_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
			pr_debug("pre_bit_error_t failed %d\n", ret);
		}
	}

	if (time_after(jiffies, priv->post_ber_update)) {
		priv->post_ber_update =
			jiffies + msecs_to_jiffies(priv->post_ber_interval);
		if (c->delivery_system == SYS_DVBT) {
			mutex_lock(priv->spi_mutex);
			ret = cxd2880_post_bit_err_t(&priv->tnrdmd,
						     &post_bit_err,
						     &post_bit_count);
			mutex_unlock(priv->spi_mutex);
		} else if (c->delivery_system == SYS_DVBT2) {
			mutex_lock(priv->spi_mutex);
			ret = cxd2880_post_bit_err_t2(&priv->tnrdmd,
						      &post_bit_err,
						      &post_bit_count);
			mutex_unlock(priv->spi_mutex);
		} else {
			return -EINVAL;
		}

		if (!ret) {
			c->post_bit_error.len = 1;
			c->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
			c->post_bit_error.stat[0].uvalue += post_bit_err;
			c->post_bit_count.len = 1;
			c->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
			c->post_bit_count.stat[0].uvalue += post_bit_count;
		} else {
			c->post_bit_error.len = 1;
			c->post_bit_error.stat[0].scale =
							FE_SCALE_NOT_AVAILABLE;
			c->post_bit_count.len = 1;
			c->post_bit_count.stat[0].scale =
							FE_SCALE_NOT_AVAILABLE;
			pr_debug("post_bit_err_t %d\n", ret);
		}
	}

	if (time_after(jiffies, priv->ucblock_update)) {
		priv->ucblock_update =
			jiffies + msecs_to_jiffies(priv->ucblock_interval);
		if (c->delivery_system == SYS_DVBT) {
			mutex_lock(priv->spi_mutex);
			ret = cxd2880_read_block_err_t(&priv->tnrdmd,
						       &block_err,
						       &block_count);
			mutex_unlock(priv->spi_mutex);
		} else if (c->delivery_system == SYS_DVBT2) {
			mutex_lock(priv->spi_mutex);
			ret = cxd2880_read_block_err_t2(&priv->tnrdmd,
							&block_err,
							&block_count);
			mutex_unlock(priv->spi_mutex);
		} else {
			return -EINVAL;
		}
		if (!ret) {
			c->block_error.len = 1;
			c->block_error.stat[0].scale = FE_SCALE_COUNTER;
			c->block_error.stat[0].uvalue += block_err;
			c->block_count.len = 1;
			c->block_count.stat[0].scale = FE_SCALE_COUNTER;
			c->block_count.stat[0].uvalue += block_count;
		} else {
			c->block_error.len = 1;
			c->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
			c->block_count.len = 1;
			c->block_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
			pr_debug("read_block_err_t  %d\n", ret);
		}
	}

	return 0;
}

static int cxd2880_check_l1post_plp(struct dvb_frontend *fe)
{
	u8 valid = 0;
	u8 plp_not_found;
	int ret;
	struct cxd2880_priv *priv = NULL;

	if (!fe) {
		pr_err("invalid arg\n");
		return -EINVAL;
	}

	priv = fe->demodulator_priv;

	ret = cxd2880_tnrdmd_dvbt2_check_l1post_valid(&priv->tnrdmd,
						      &valid);
	if (ret)
		return ret;

	if (!valid)
		return -EAGAIN;

	ret = cxd2880_tnrdmd_dvbt2_mon_data_plp_error(&priv->tnrdmd,
						      &plp_not_found);
	if (ret)
		return ret;

	if (plp_not_found) {
		priv->dvbt2_tune_param.tune_info =
			CXD2880_TNRDMD_DVBT2_TUNE_INFO_INVALID_PLP_ID;
	} else {
		priv->dvbt2_tune_param.tune_info =
			CXD2880_TNRDMD_DVBT2_TUNE_INFO_OK;
	}

	return 0;
}

static int cxd2880_read_status(struct dvb_frontend *fe,
			       enum fe_status *status)
{
	int ret;
	u8 sync = 0;
	u8 lock = 0;
	u8 unlock = 0;
	struct cxd2880_priv *priv = NULL;
	struct dtv_frontend_properties *c = NULL;

	if (!fe || !status) {
		pr_err("invalid arg\n");
		return -EINVAL;
	}

	priv = fe->demodulator_priv;
	c = &fe->dtv_property_cache;
	*status = 0;

	if (priv->tnrdmd.state == CXD2880_TNRDMD_STATE_ACTIVE) {
		mutex_lock(priv->spi_mutex);
		if (c->delivery_system == SYS_DVBT) {
			ret = cxd2880_tnrdmd_dvbt_mon_sync_stat(&priv->tnrdmd,
								&sync,
								&lock,
								&unlock);
		} else if (c->delivery_system == SYS_DVBT2) {
			ret = cxd2880_tnrdmd_dvbt2_mon_sync_stat(&priv->tnrdmd,
								 &sync,
								 &lock,
								 &unlock);
		} else {
			pr_err("invalid system");
			mutex_unlock(priv->spi_mutex);
			return -EINVAL;
		}

		mutex_unlock(priv->spi_mutex);
		if (ret) {
			pr_err("failed. sys = %d\n", priv->tnrdmd.sys);
			return  ret;
		}

		if (sync == 6) {
			*status = FE_HAS_SIGNAL |
				  FE_HAS_CARRIER;
		}
		if (lock)
			*status |= FE_HAS_VITERBI |
				   FE_HAS_SYNC |
				   FE_HAS_LOCK;
	}

	pr_debug("status %d\n", *status);

	if (priv->s == 0 && (*status & FE_HAS_LOCK) &&
	    (*status & FE_HAS_CARRIER)) {
		mutex_lock(priv->spi_mutex);
		if (c->delivery_system == SYS_DVBT) {
			ret = cxd2880_set_ber_per_period_t(fe);
			priv->s = *status;
		} else if (c->delivery_system == SYS_DVBT2) {
			ret = cxd2880_check_l1post_plp(fe);
			if (!ret) {
				ret = cxd2880_set_ber_per_period_t2(fe);
				priv->s = *status;
			}
		} else {
			pr_err("invalid system\n");
			mutex_unlock(priv->spi_mutex);
			return -EINVAL;
		}
		mutex_unlock(priv->spi_mutex);
	}

	cxd2880_get_stats(fe, *status);
	return  0;
}

static int cxd2880_tune(struct dvb_frontend *fe,
			bool retune,
			unsigned int mode_flags,
			unsigned int *delay,
			enum fe_status *status)
{
	int ret;

	if (!fe || !delay || !status) {
		pr_err("invalid arg.");
		return -EINVAL;
	}

	if (retune) {
		ret = cxd2880_set_frontend(fe);
		if (ret) {
			pr_err("cxd2880_set_frontend failed %d\n", ret);
			return ret;
		}
	}

	*delay = HZ / 5;

	return cxd2880_read_status(fe, status);
}

static int cxd2880_get_frontend_t(struct dvb_frontend *fe,
				  struct dtv_frontend_properties *c)
{
	int ret;
	struct cxd2880_priv *priv = NULL;
	enum cxd2880_dvbt_mode mode = CXD2880_DVBT_MODE_2K;
	enum cxd2880_dvbt_guard guard = CXD2880_DVBT_GUARD_1_32;
	struct cxd2880_dvbt_tpsinfo tps;
	enum cxd2880_tnrdmd_spectrum_sense sense;
	u16 snr = 0;
	int strength = 0;

	if (!fe || !c) {
		pr_err("invalid arg\n");
		return -EINVAL;
	}

	priv = fe->demodulator_priv;

	mutex_lock(priv->spi_mutex);
	ret = cxd2880_tnrdmd_dvbt_mon_mode_guard(&priv->tnrdmd,
						 &mode, &guard);
	mutex_unlock(priv->spi_mutex);
	if (!ret) {
		switch (mode) {
		case CXD2880_DVBT_MODE_2K:
			c->transmission_mode = TRANSMISSION_MODE_2K;
			break;
		case CXD2880_DVBT_MODE_8K:
			c->transmission_mode = TRANSMISSION_MODE_8K;
			break;
		default:
			c->transmission_mode = TRANSMISSION_MODE_2K;
			pr_debug("transmission mode is invalid %d\n", mode);
			break;
		}
		switch (guard) {
		case CXD2880_DVBT_GUARD_1_32:
			c->guard_interval = GUARD_INTERVAL_1_32;
			break;
		case CXD2880_DVBT_GUARD_1_16:
			c->guard_interval = GUARD_INTERVAL_1_16;
			break;
		case CXD2880_DVBT_GUARD_1_8:
			c->guard_interval = GUARD_INTERVAL_1_8;
			break;
		case CXD2880_DVBT_GUARD_1_4:
			c->guard_interval = GUARD_INTERVAL_1_4;
			break;
		default:
			c->guard_interval = GUARD_INTERVAL_1_32;
			pr_debug("guard interval is invalid %d\n",
				 guard);
			break;
		}
	} else {
		c->transmission_mode = TRANSMISSION_MODE_2K;
		c->guard_interval = GUARD_INTERVAL_1_32;
		pr_debug("ModeGuard err %d\n", ret);
	}

	mutex_lock(priv->spi_mutex);
	ret = cxd2880_tnrdmd_dvbt_mon_tps_info(&priv->tnrdmd, &tps);
	mutex_unlock(priv->spi_mutex);
	if (!ret) {
		switch (tps.hierarchy) {
		case CXD2880_DVBT_HIERARCHY_NON:
			c->hierarchy = HIERARCHY_NONE;
			break;
		case CXD2880_DVBT_HIERARCHY_1:
			c->hierarchy = HIERARCHY_1;
			break;
		case CXD2880_DVBT_HIERARCHY_2:
			c->hierarchy = HIERARCHY_2;
			break;
		case CXD2880_DVBT_HIERARCHY_4:
			c->hierarchy = HIERARCHY_4;
			break;
		default:
			c->hierarchy = HIERARCHY_NONE;
			pr_debug("TPSInfo hierarchy is invalid %d\n",
				 tps.hierarchy);
			break;
		}

		switch (tps.rate_hp) {
		case CXD2880_DVBT_CODERATE_1_2:
			c->code_rate_HP = FEC_1_2;
			break;
		case CXD2880_DVBT_CODERATE_2_3:
			c->code_rate_HP = FEC_2_3;
			break;
		case CXD2880_DVBT_CODERATE_3_4:
			c->code_rate_HP = FEC_3_4;
			break;
		case CXD2880_DVBT_CODERATE_5_6:
			c->code_rate_HP = FEC_5_6;
			break;
		case CXD2880_DVBT_CODERATE_7_8:
			c->code_rate_HP = FEC_7_8;
			break;
		default:
			c->code_rate_HP = FEC_NONE;
			pr_debug("TPSInfo rateHP is invalid %d\n",
				 tps.rate_hp);
			break;
		}
		switch (tps.rate_lp) {
		case CXD2880_DVBT_CODERATE_1_2:
			c->code_rate_LP = FEC_1_2;
			break;
		case CXD2880_DVBT_CODERATE_2_3:
			c->code_rate_LP = FEC_2_3;
			break;
		case CXD2880_DVBT_CODERATE_3_4:
			c->code_rate_LP = FEC_3_4;
			break;
		case CXD2880_DVBT_CODERATE_5_6:
			c->code_rate_LP = FEC_5_6;
			break;
		case CXD2880_DVBT_CODERATE_7_8:
			c->code_rate_LP = FEC_7_8;
			break;
		default:
			c->code_rate_LP = FEC_NONE;
			pr_debug("TPSInfo rateLP is invalid %d\n",
				 tps.rate_lp);
			break;
		}
		switch (tps.constellation) {
		case CXD2880_DVBT_CONSTELLATION_QPSK:
			c->modulation = QPSK;
			break;
		case CXD2880_DVBT_CONSTELLATION_16QAM:
			c->modulation = QAM_16;
			break;
		case CXD2880_DVBT_CONSTELLATION_64QAM:
			c->modulation = QAM_64;
			break;
		default:
			c->modulation = QPSK;
			pr_debug("TPSInfo constellation is invalid %d\n",
				 tps.constellation);
			break;
		}
	} else {
		c->hierarchy = HIERARCHY_NONE;
		c->code_rate_HP = FEC_NONE;
		c->code_rate_LP = FEC_NONE;
		c->modulation = QPSK;
		pr_debug("TPS info err %d\n", ret);
	}

	mutex_lock(priv->spi_mutex);
	ret = cxd2880_tnrdmd_dvbt_mon_spectrum_sense(&priv->tnrdmd, &sense);
	mutex_unlock(priv->spi_mutex);
	if (!ret) {
		switch (sense) {
		case CXD2880_TNRDMD_SPECTRUM_NORMAL:
			c->inversion = INVERSION_OFF;
			break;
		case CXD2880_TNRDMD_SPECTRUM_INV:
			c->inversion = INVERSION_ON;
			break;
		default:
			c->inversion = INVERSION_OFF;
			pr_debug("spectrum sense is invalid %d\n", sense);
			break;
		}
	} else {
		c->inversion = INVERSION_OFF;
		pr_debug("spectrum_sense %d\n", ret);
	}

	mutex_lock(priv->spi_mutex);
	ret = cxd2880_tnrdmd_mon_rf_lvl(&priv->tnrdmd, &strength);
	mutex_unlock(priv->spi_mutex);
	if (!ret) {
		c->strength.len = 1;
		c->strength.stat[0].scale = FE_SCALE_DECIBEL;
		c->strength.stat[0].svalue = strength;
	} else {
		c->strength.len = 1;
		c->strength.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		pr_debug("mon_rf_lvl %d\n", ret);
	}

	ret = cxd2880_read_snr(fe, &snr);
	if (!ret) {
		c->cnr.len = 1;
		c->cnr.stat[0].scale = FE_SCALE_DECIBEL;
		c->cnr.stat[0].svalue = snr;
	} else {
		c->cnr.len = 1;
		c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		pr_debug("read_snr %d\n", ret);
	}

	return 0;
}

static int cxd2880_get_frontend_t2(struct dvb_frontend *fe,
				   struct dtv_frontend_properties *c)
{
	int ret;
	struct cxd2880_priv *priv = NULL;
	struct cxd2880_dvbt2_l1pre l1pre;
	enum cxd2880_dvbt2_plp_code_rate coderate;
	enum cxd2880_dvbt2_plp_constell qam;
	enum cxd2880_tnrdmd_spectrum_sense sense;
	u16 snr = 0;
	int strength = 0;

	if (!fe || !c) {
		pr_err("invalid arg.\n");
		return -EINVAL;
	}

	priv = fe->demodulator_priv;

	mutex_lock(priv->spi_mutex);
	ret = cxd2880_tnrdmd_dvbt2_mon_l1_pre(&priv->tnrdmd, &l1pre);
	mutex_unlock(priv->spi_mutex);
	if (!ret) {
		switch (l1pre.fft_mode) {
		case CXD2880_DVBT2_M2K:
			c->transmission_mode = TRANSMISSION_MODE_2K;
			break;
		case CXD2880_DVBT2_M8K:
			c->transmission_mode = TRANSMISSION_MODE_8K;
			break;
		case CXD2880_DVBT2_M4K:
			c->transmission_mode = TRANSMISSION_MODE_4K;
			break;
		case CXD2880_DVBT2_M1K:
			c->transmission_mode = TRANSMISSION_MODE_1K;
			break;
		case CXD2880_DVBT2_M16K:
			c->transmission_mode = TRANSMISSION_MODE_16K;
			break;
		case CXD2880_DVBT2_M32K:
			c->transmission_mode = TRANSMISSION_MODE_32K;
			break;
		default:
			c->transmission_mode = TRANSMISSION_MODE_2K;
			pr_debug("L1Pre fft_mode is invalid %d\n",
				 l1pre.fft_mode);
			break;
		}
		switch (l1pre.gi) {
		case CXD2880_DVBT2_G1_32:
			c->guard_interval = GUARD_INTERVAL_1_32;
			break;
		case CXD2880_DVBT2_G1_16:
			c->guard_interval = GUARD_INTERVAL_1_16;
			break;
		case CXD2880_DVBT2_G1_8:
			c->guard_interval = GUARD_INTERVAL_1_8;
			break;
		case CXD2880_DVBT2_G1_4:
			c->guard_interval = GUARD_INTERVAL_1_4;
			break;
		case CXD2880_DVBT2_G1_128:
			c->guard_interval = GUARD_INTERVAL_1_128;
			break;
		case CXD2880_DVBT2_G19_128:
			c->guard_interval = GUARD_INTERVAL_19_128;
			break;
		case CXD2880_DVBT2_G19_256:
			c->guard_interval = GUARD_INTERVAL_19_256;
			break;
		default:
			c->guard_interval = GUARD_INTERVAL_1_32;
			pr_debug("L1Pre guard interval is invalid %d\n",
				 l1pre.gi);
			break;
		}
	} else {
		c->transmission_mode = TRANSMISSION_MODE_2K;
		c->guard_interval = GUARD_INTERVAL_1_32;
		pr_debug("L1Pre err %d\n", ret);
	}

	mutex_lock(priv->spi_mutex);
	ret = cxd2880_tnrdmd_dvbt2_mon_code_rate(&priv->tnrdmd,
						 CXD2880_DVBT2_PLP_DATA,
						 &coderate);
	mutex_unlock(priv->spi_mutex);
	if (!ret) {
		switch (coderate) {
		case CXD2880_DVBT2_R1_2:
			c->fec_inner = FEC_1_2;
			break;
		case CXD2880_DVBT2_R3_5:
			c->fec_inner = FEC_3_5;
			break;
		case CXD2880_DVBT2_R2_3:
			c->fec_inner = FEC_2_3;
			break;
		case CXD2880_DVBT2_R3_4:
			c->fec_inner = FEC_3_4;
			break;
		case CXD2880_DVBT2_R4_5:
			c->fec_inner = FEC_4_5;
			break;
		case CXD2880_DVBT2_R5_6:
			c->fec_inner = FEC_5_6;
			break;
		default:
			c->fec_inner = FEC_NONE;
			pr_debug("CodeRate is invalid %d\n", coderate);
			break;
		}
	} else {
		c->fec_inner = FEC_NONE;
		pr_debug("CodeRate %d\n", ret);
	}

	mutex_lock(priv->spi_mutex);
	ret = cxd2880_tnrdmd_dvbt2_mon_qam(&priv->tnrdmd,
					   CXD2880_DVBT2_PLP_DATA,
					   &qam);
	mutex_unlock(priv->spi_mutex);
	if (!ret) {
		switch (qam) {
		case CXD2880_DVBT2_QPSK:
			c->modulation = QPSK;
			break;
		case CXD2880_DVBT2_QAM16:
			c->modulation = QAM_16;
			break;
		case CXD2880_DVBT2_QAM64:
			c->modulation = QAM_64;
			break;
		case CXD2880_DVBT2_QAM256:
			c->modulation = QAM_256;
			break;
		default:
			c->modulation = QPSK;
			pr_debug("QAM is invalid %d\n", qam);
			break;
		}
	} else {
		c->modulation = QPSK;
		pr_debug("QAM %d\n", ret);
	}

	mutex_lock(priv->spi_mutex);
	ret = cxd2880_tnrdmd_dvbt2_mon_spectrum_sense(&priv->tnrdmd, &sense);
	mutex_unlock(priv->spi_mutex);
	if (!ret) {
		switch (sense) {
		case CXD2880_TNRDMD_SPECTRUM_NORMAL:
			c->inversion = INVERSION_OFF;
			break;
		case CXD2880_TNRDMD_SPECTRUM_INV:
			c->inversion = INVERSION_ON;
			break;
		default:
			c->inversion = INVERSION_OFF;
			pr_debug("spectrum sense is invalid %d\n", sense);
			break;
		}
	} else {
		c->inversion = INVERSION_OFF;
		pr_debug("SpectrumSense %d\n", ret);
	}

	mutex_lock(priv->spi_mutex);
	ret = cxd2880_tnrdmd_mon_rf_lvl(&priv->tnrdmd, &strength);
	mutex_unlock(priv->spi_mutex);
	if (!ret) {
		c->strength.len = 1;
		c->strength.stat[0].scale = FE_SCALE_DECIBEL;
		c->strength.stat[0].svalue = strength;
	} else {
		c->strength.len = 1;
		c->strength.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		pr_debug("mon_rf_lvl %d\n", ret);
	}

	ret = cxd2880_read_snr(fe, &snr);
	if (!ret) {
		c->cnr.len = 1;
		c->cnr.stat[0].scale = FE_SCALE_DECIBEL;
		c->cnr.stat[0].svalue = snr;
	} else {
		c->cnr.len = 1;
		c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		pr_debug("read_snr %d\n", ret);
	}

	return 0;
}

static int cxd2880_get_frontend(struct dvb_frontend *fe,
				struct dtv_frontend_properties *props)
{
	int ret;

	if (!fe || !props) {
		pr_err("invalid arg.");
		return -EINVAL;
	}

	pr_debug("system=%d\n", fe->dtv_property_cache.delivery_system);
	switch (fe->dtv_property_cache.delivery_system) {
	case SYS_DVBT:
		ret = cxd2880_get_frontend_t(fe, props);
		break;
	case SYS_DVBT2:
		ret = cxd2880_get_frontend_t2(fe, props);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static enum dvbfe_algo cxd2880_get_frontend_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_HW;
}

static struct dvb_frontend_ops cxd2880_dvbt_t2_ops = {
	.info = {
		.name = "Sony CXD2880",
		.frequency_min_hz = 174 * MHz,
		.frequency_max_hz = 862 * MHz,
		.frequency_stepsize_hz = 1 * kHz,
		.caps = FE_CAN_INVERSION_AUTO |
				FE_CAN_FEC_1_2 |
				FE_CAN_FEC_2_3 |
				FE_CAN_FEC_3_4 |
				FE_CAN_FEC_4_5 |
				FE_CAN_FEC_5_6	|
				FE_CAN_FEC_7_8	|
				FE_CAN_FEC_AUTO |
				FE_CAN_QPSK |
				FE_CAN_QAM_16 |
				FE_CAN_QAM_32 |
				FE_CAN_QAM_64 |
				FE_CAN_QAM_128 |
				FE_CAN_QAM_256 |
				FE_CAN_QAM_AUTO |
				FE_CAN_TRANSMISSION_MODE_AUTO |
				FE_CAN_GUARD_INTERVAL_AUTO |
				FE_CAN_2G_MODULATION |
				FE_CAN_RECOVER |
				FE_CAN_MUTE_TS,
	},
	.delsys = { SYS_DVBT, SYS_DVBT2 },

	.release = cxd2880_release,
	.init = cxd2880_init,
	.sleep = cxd2880_sleep,
	.tune = cxd2880_tune,
	.set_frontend = cxd2880_set_frontend,
	.get_frontend = cxd2880_get_frontend,
	.read_status = cxd2880_read_status,
	.read_ber = cxd2880_read_ber,
	.read_signal_strength = cxd2880_read_signal_strength,
	.read_snr = cxd2880_read_snr,
	.read_ucblocks = cxd2880_read_ucblocks,
	.get_frontend_algo = cxd2880_get_frontend_algo,
};

struct dvb_frontend *cxd2880_attach(struct dvb_frontend *fe,
				    struct cxd2880_config *cfg)
{
	int ret;
	enum cxd2880_tnrdmd_chip_id chipid =
					CXD2880_TNRDMD_CHIP_ID_UNKNOWN;
	static struct cxd2880_priv *priv;
	u8 data = 0;

	if (!fe) {
		pr_err("invalid arg.\n");
		return NULL;
	}

	priv = kzalloc(sizeof(struct cxd2880_priv), GFP_KERNEL);
	if (!priv)
		return NULL;

	priv->spi = cfg->spi;
	priv->spi_mutex = cfg->spi_mutex;
	priv->spi_device.spi = cfg->spi;

	memcpy(&fe->ops, &cxd2880_dvbt_t2_ops,
	       sizeof(struct dvb_frontend_ops));

	ret = cxd2880_spi_device_initialize(&priv->spi_device,
					    CXD2880_SPI_MODE_0,
					    55000000);
	if (ret) {
		pr_err("spi_device_initialize failed. %d\n", ret);
		kfree(priv);
		return NULL;
	}

	ret = cxd2880_spi_device_create_spi(&priv->cxd2880_spi,
					    &priv->spi_device);
	if (ret) {
		pr_err("spi_device_create_spi failed. %d\n", ret);
		kfree(priv);
		return NULL;
	}

	ret = cxd2880_io_spi_create(&priv->regio, &priv->cxd2880_spi, 0);
	if (ret) {
		pr_err("io_spi_create failed. %d\n", ret);
		kfree(priv);
		return NULL;
	}
	ret = priv->regio.write_reg(&priv->regio,
				    CXD2880_IO_TGT_SYS, 0x00, 0x00);
	if (ret) {
		pr_err("set bank to 0x00 failed.\n");
		kfree(priv);
		return NULL;
	}
	ret = priv->regio.read_regs(&priv->regio,
				    CXD2880_IO_TGT_SYS, 0xfd, &data, 1);
	if (ret) {
		pr_err("read chip id failed.\n");
		kfree(priv);
		return NULL;
	}

	chipid = (enum cxd2880_tnrdmd_chip_id)data;
	if (chipid != CXD2880_TNRDMD_CHIP_ID_CXD2880_ES1_0X &&
	    chipid != CXD2880_TNRDMD_CHIP_ID_CXD2880_ES1_11) {
		pr_err("chip id invalid.\n");
		kfree(priv);
		return NULL;
	}

	fe->demodulator_priv = priv;
	pr_info("CXD2880 driver version: Ver %s\n",
		CXD2880_TNRDMD_DRIVER_VERSION);

	return fe;
}
EXPORT_SYMBOL(cxd2880_attach);

MODULE_DESCRIPTION("Sony CXD2880 DVB-T2/T tuner + demod driver");
MODULE_AUTHOR("Sony Semiconductor Solutions Corporation");
MODULE_LICENSE("GPL v2");
