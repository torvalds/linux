// SPDX-License-Identifier: GPL-2.0
/*
 *  c8sectpfe-dvb.c - C8SECTPFE STi DVB driver
 *
 * Copyright (c) STMicroelectronics 2015
 *
 *  Author Peter Griffin <peter.griffin@linaro.org>
 *
 */
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>

#include <dt-bindings/media/c8sectpfe.h>

#include "c8sectpfe-common.h"
#include "c8sectpfe-core.h"
#include "c8sectpfe-dvb.h"

#include "dvb-pll.h"
#include "lnbh24.h"
#include "stv0367.h"
#include "stv0367_priv.h"
#include "stv6110x.h"
#include "stv090x.h"
#include "tda18212.h"

static inline const char *dvb_card_str(unsigned int c)
{
	switch (c) {
	case STV0367_TDA18212_NIMA_1:	return "STV0367_TDA18212_NIMA_1";
	case STV0367_TDA18212_NIMA_2:	return "STV0367_TDA18212_NIMA_2";
	case STV0367_TDA18212_NIMB_1:	return "STV0367_TDA18212_NIMB_1";
	case STV0367_TDA18212_NIMB_2:	return "STV0367_TDA18212_NIMB_2";
	case STV0903_6110_LNB24_NIMA:	return "STV0903_6110_LNB24_NIMA";
	case STV0903_6110_LNB24_NIMB:	return "STV0903_6110_LNB24_NIMB";
	default:			return "unknown dvb frontend card";
	}
}

static struct stv090x_config stv090x_config = {
	.device                 = STV0903,
	.demod_mode             = STV090x_SINGLE,
	.clk_mode               = STV090x_CLK_EXT,
	.xtal                   = 16000000,
	.address                = 0x69,

	.ts1_mode               = STV090x_TSMODE_SERIAL_CONTINUOUS,
	.ts2_mode               = STV090x_TSMODE_SERIAL_CONTINUOUS,

	.repeater_level         = STV090x_RPTLEVEL_64,

	.tuner_init             = NULL,
	.tuner_set_mode         = NULL,
	.tuner_set_frequency    = NULL,
	.tuner_get_frequency    = NULL,
	.tuner_set_bandwidth    = NULL,
	.tuner_get_bandwidth    = NULL,
	.tuner_set_bbgain       = NULL,
	.tuner_get_bbgain       = NULL,
	.tuner_set_refclk       = NULL,
	.tuner_get_status       = NULL,
};

static struct stv6110x_config stv6110x_config = {
	.addr                   = 0x60,
	.refclk                 = 16000000,
};

#define NIMA 0
#define NIMB 1

static struct stv0367_config stv0367_tda18212_config[] = {
	{
		.demod_address = 0x1c,
		.xtal = 16000000,
		.if_khz = 4500,
		.if_iq_mode = FE_TER_NORMAL_IF_TUNER,
		.ts_mode = STV0367_SERIAL_PUNCT_CLOCK,
		.clk_pol = STV0367_CLOCKPOLARITY_DEFAULT,
	}, {
		.demod_address = 0x1d,
		.xtal = 16000000,
		.if_khz = 4500,
		.if_iq_mode = FE_TER_NORMAL_IF_TUNER,
		.ts_mode = STV0367_SERIAL_PUNCT_CLOCK,
		.clk_pol = STV0367_CLOCKPOLARITY_DEFAULT,
	}, {
		.demod_address = 0x1e,
		.xtal = 16000000,
		.if_khz = 4500,
		.if_iq_mode = FE_TER_NORMAL_IF_TUNER,
		.ts_mode = STV0367_SERIAL_PUNCT_CLOCK,
		.clk_pol = STV0367_CLOCKPOLARITY_DEFAULT,
	},
};

static struct tda18212_config tda18212_conf = {
	.if_dvbt_6 = 4150,
	.if_dvbt_7 = 4150,
	.if_dvbt_8 = 4500,
	.if_dvbc = 5000,
};

int c8sectpfe_frontend_attach(struct dvb_frontend **fe,
		struct c8sectpfe *c8sectpfe,
		struct channel_info *tsin, int chan_num)
{
	struct tda18212_config *tda18212;
	const struct stv6110x_devctl *fe2;
	struct i2c_client *client;
	struct i2c_board_info tda18212_info = {
		.type = "tda18212",
		.addr = 0x60,
	};

	if (!tsin)
		return -EINVAL;

	switch (tsin->dvb_card) {

	case STV0367_TDA18212_NIMA_1:
	case STV0367_TDA18212_NIMA_2:
	case STV0367_TDA18212_NIMB_1:
	case STV0367_TDA18212_NIMB_2:
		if (tsin->dvb_card == STV0367_TDA18212_NIMA_1)
			*fe = dvb_attach(stv0367ter_attach,
				 &stv0367_tda18212_config[0],
					tsin->i2c_adapter);
		else if (tsin->dvb_card == STV0367_TDA18212_NIMB_1)
			*fe = dvb_attach(stv0367ter_attach,
				 &stv0367_tda18212_config[1],
					tsin->i2c_adapter);
		else
			*fe = dvb_attach(stv0367ter_attach,
				 &stv0367_tda18212_config[2],
					tsin->i2c_adapter);

		if (!*fe) {
			dev_err(c8sectpfe->device,
				"%s: stv0367ter_attach failed for NIM card %s\n"
				, __func__, dvb_card_str(tsin->dvb_card));
			return -ENODEV;
		}

		/*
		 * init the demod so that i2c gate_ctrl
		 * to the tuner works correctly
		 */
		(*fe)->ops.init(*fe);

		/* Allocate the tda18212 structure */
		tda18212 = devm_kzalloc(c8sectpfe->device,
					sizeof(struct tda18212_config),
					GFP_KERNEL);
		if (!tda18212) {
			dev_err(c8sectpfe->device,
				"%s: devm_kzalloc failed\n", __func__);
			return -ENOMEM;
		}

		memcpy(tda18212, &tda18212_conf,
			sizeof(struct tda18212_config));

		tda18212->fe = (*fe);

		tda18212_info.platform_data = tda18212;

		/* attach tuner */
		request_module("tda18212");
		client = i2c_new_client_device(tsin->i2c_adapter,
					       &tda18212_info);
		if (!i2c_client_has_driver(client)) {
			dvb_frontend_detach(*fe);
			return -ENODEV;
		}

		if (!try_module_get(client->dev.driver->owner)) {
			i2c_unregister_device(client);
			dvb_frontend_detach(*fe);
			return -ENODEV;
		}

		tsin->i2c_client = client;

		break;

	case STV0903_6110_LNB24_NIMA:
		*fe = dvb_attach(stv090x_attach,	&stv090x_config,
				tsin->i2c_adapter, STV090x_DEMODULATOR_0);
		if (!*fe) {
			dev_err(c8sectpfe->device, "%s: stv090x_attach failed\n"
				"\tfor NIM card %s\n",
				__func__, dvb_card_str(tsin->dvb_card));
			return -ENODEV;
		}

		fe2 = dvb_attach(stv6110x_attach, *fe,
					&stv6110x_config, tsin->i2c_adapter);
		if (!fe2) {
			dev_err(c8sectpfe->device,
				"%s: stv6110x_attach failed for NIM card %s\n"
				, __func__, dvb_card_str(tsin->dvb_card));
			return -ENODEV;
		}

		stv090x_config.tuner_init = fe2->tuner_init;
		stv090x_config.tuner_set_mode = fe2->tuner_set_mode;
		stv090x_config.tuner_set_frequency = fe2->tuner_set_frequency;
		stv090x_config.tuner_get_frequency = fe2->tuner_get_frequency;
		stv090x_config.tuner_set_bandwidth = fe2->tuner_set_bandwidth;
		stv090x_config.tuner_get_bandwidth = fe2->tuner_get_bandwidth;
		stv090x_config.tuner_set_bbgain = fe2->tuner_set_bbgain;
		stv090x_config.tuner_get_bbgain = fe2->tuner_get_bbgain;
		stv090x_config.tuner_set_refclk = fe2->tuner_set_refclk;
		stv090x_config.tuner_get_status = fe2->tuner_get_status;

		dvb_attach(lnbh24_attach, *fe, tsin->i2c_adapter, 0, 0, 0x9);
		break;

	default:
		dev_err(c8sectpfe->device,
			"%s: DVB frontend card %s not yet supported\n",
			__func__, dvb_card_str(tsin->dvb_card));
		return -ENODEV;
	}

	(*fe)->id = chan_num;

	dev_info(c8sectpfe->device,
			"DVB frontend card %s successfully attached",
			dvb_card_str(tsin->dvb_card));
	return 0;
}
