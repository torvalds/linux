// SPDX-License-Identifier: GPL-2.0
/*
 * This Synopsys software and associated documentation (hereinafter the
 * "Software") is an unsupported proprietary work of Synopsys, Inc. unless
 * otherwise expressly agreed to in writing between Synopsys and you. The
 * Software IS NOT an item of Licensed Software or a Licensed Product under
 * any End User Software License Agreement or Agreement for Licensed Products
 * with Synopsys or any supplement thereto. Synopsys is a registered trademark
 * of Synopsys, Inc. Other names included in the SOFTWARE may be the
 * trademarks of their respective owners.
 *
 * The contents of this file are dual-licensed; you may select either version
 * 2 of the GNU General Public License ("GPL") or the BSD-3-Clause license
 * ("BSD-3-Clause"). The GPL is included in the COPYING file accompanying the
 * SOFTWARE. The BSD License is copied below.
 *
 * BSD-3-Clause License:
 * Copyright (c) 2012-2016 Synopsys, Inc. and/or its affiliates.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer, without
 *    modification.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. The names of the above-listed copyright holders may not be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "nisttrng_hw.h"
#include "nisttrng.h"

/* Initialize the NIST_TRNG state structure */
int nisttrng_init(struct nist_trng_state *state, u32 *base)
{
	int err;
	u32 tmp;

	DEBUG(">> %s: initialize the NIST_TRNG\n", __func__);

	memset(state, 0, sizeof(*state));

	state->base = base;

	/* make sure there is no alarm and the core is not busy */
	err = nisttrng_get_alarms(state);
	if (err)
		goto ERR;

	err = nisttrng_wait_on_busy(state);
	if (err)
		goto ERR;

	/* hardware features*/
	tmp = pdu_io_read32(state->base + NIST_TRNG_REG_FEATURES);

	state->config.features.drbg_arch = NIST_TRNG_REG_FEATURES_AES_256(tmp);
	state->config.features.extra_ps_present =
		NIST_TRNG_REG_FEATURES_EXTRA_PS_PRESENT(tmp);
	state->config.features.secure_rst_state =
		NIST_TRNG_REG_FEATURES_SECURE_RST_STATE(tmp);
	state->config.features.diag_level_basic_trng =
		NIST_TRNG_REG_FEATURES_DIAG_LEVEL_BASIC_TRNG(tmp);
	state->config.features.diag_level_stat_hlt =
		NIST_TRNG_REG_FEATURES_DIAG_LEVEL_ST_HLT(tmp);
	state->config.features.diag_level_ns =
		NIST_TRNG_REG_FEATURES_DIAG_LEVEL_NS(tmp);

	/* corekit */
	tmp = pdu_io_read32(state->base + NIST_TRNG_REG_COREKIT_REL);
	state->config.corekit_rel.ext_enum = NIST_TRNG_REG_EXT_ENUM(tmp);
	state->config.corekit_rel.ext_ver = NIST_TRNG_REG_EXT_VER(tmp);
	state->config.corekit_rel.rel_num = NIST_TRNG_REG_REL_NUM(tmp);

	/* clear registers */
	pdu_io_write32(state->base + NIST_TRNG_REG_ALARM, 0xFFFFFFFF);
	pdu_io_write32(state->base + NIST_TRNG_REG_ISTAT, 0xFFFFFFFF);

	/* setup the NIST_TRNG in secure mode, self seeding mode, with prediction resistance, maximum possible security strength */
	/* SMODE */
	tmp = 0;
	tmp = NIST_TRNG_REG_SMODE_SET_SECURE_EN(tmp, 1);
	tmp = NIST_TRNG_REG_SMODE_SET_NONCE(tmp, 0);
	tmp = NIST_TRNG_REG_SMODE_SET_MAX_REJECTS(tmp,
						  NIST_TRNG_DFLT_MAX_REJECTS);
	pdu_io_write32(state->base + NIST_TRNG_REG_SMODE, tmp);
	state->status.secure_mode = 1;
	state->status.nonce_mode = 0;
	/* MODE */
	tmp = 0;
	if (state->config.features.drbg_arch == AES256) {
		tmp = NIST_TRNG_REG_MODE_SET_SEC_ALG(tmp, 1);
		state->status.sec_strength = SEC_STRNT_AES256;

	} else if (state->config.features.drbg_arch == AES128) {
		tmp = NIST_TRNG_REG_MODE_SET_SEC_ALG(tmp, 0);
		state->status.sec_strength = SEC_STRNT_AES128;

	} else {
		SYNHW_PRINT("Invalid DRBG architecture");
		err = CRYPTO_FAILED;
		goto ERR;
	}

	tmp = NIST_TRNG_REG_MODE_SET_PRED_RESIST(tmp, 1);
	pdu_io_write32(state->base + NIST_TRNG_REG_MODE, 0);
	state->status.pred_resist = 1;
	/* rest of the status */
	state->status.alarm_code = 0;
	state->status.pad_ps_addin = 0;

	/* reminders - set the counters to the standard's maximum values. An API is be provided to change those on demand.*/
	nisttrng_set_reminder_max_bits_per_req(state,
					       NIST_DFLT_MAX_BITS_PER_REQ);
	nisttrng_set_reminder_max_req_per_seed(state,
					       NIST_DFLT_MAX_REQ_PER_SEED);

	/* display features */
	SYNHW_PRINT("NIST_TRNG: Hardware rel_num=0x%x, ext_ver=0x%x, ext_enum=0x%x\n",
		    state->config.corekit_rel.rel_num,
		    state->config.corekit_rel.ext_ver,
		    state->config.corekit_rel.ext_enum);
	switch (state->config.features.drbg_arch) {
	case AES128:
		DEBUG("NIST_TRNG: DRBG Architecture=128-bit AES, Extra Personalization Existence=%u\n",
		      state->config.features.extra_ps_present);
		break;
	case AES256:
		DEBUG("NIST_TRNG: DRBG Architecture=256-bit AES, Extra Personalization Existence=%u\n",
		      state->config.features.extra_ps_present);
		break;
	default:
		SYNHW_PRINT("Invalid DRBG architecture");
		err = CRYPTO_FAILED;
		goto ERR;
	}

	DEBUG("initialization is done, going for a zeroize\n");

	// BUILD_CFG0
	tmp = pdu_io_read32(state->base + NIST_TRNG_REG_BUILD_CFG0);
	state->config.build_cfg0.core_type = NIST_TRNG_REG_CFG0_CORE_TYPE(tmp);
	state->config.build_cfg0.bg8 = NIST_TRNG_REG_CFG0_BG8(tmp);
	state->config.build_cfg0.cdc_synch_depth =
		NIST_TRNG_REG_CFG0_CDC_SYNCH_DEPTH(tmp);
	state->config.build_cfg0.background_noise =
		NIST_TRNG_REG_CFG0_BACGROUND_NOISE(tmp);
	state->config.build_cfg0.edu_present =
		NIST_TRNG_REG_CFG0_EDU_PRESENT(tmp);
	state->config.build_cfg0.aes_datapath =
		NIST_TRNG_REG_CFG0_AES_DATAPATH(tmp);
	state->config.build_cfg0.aes_max_key_size =
		NIST_TRNG_REG_CFG0_AES_MAX_KEY_SIZE(tmp);
	state->config.build_cfg0.personilzation_str =
		NIST_TRNG_REG_CFG0_PERSONILIZATION_STR(tmp);
	DEBUG("NIST_TRNG: BUILD_CFG0 core_type=%u, bg8=%u, cdc_synch_depth=%u, background_noise=%u\n",
	      state->config.build_cfg0.core_type, state->config.build_cfg0.bg8,
	      state->config.build_cfg0.cdc_synch_depth,
	      state->config.build_cfg0.background_noise);
	DEBUG("edu_present=%u, aes_datapath=%u, aes_max_key_size=%u, personilzation_str=%u\n",
	      state->config.build_cfg0.edu_present,
	      state->config.build_cfg0.aes_datapath,
	      state->config.build_cfg0.aes_max_key_size,
	      state->config.build_cfg0.personilzation_str);

	tmp = pdu_io_read32(state->base + NIST_TRNG_REG_BUILD_CFG1);
	DEBUG("NIST_TRNG: NIST_TRNG_REG_BUILD_CFG1=0x%x\n", tmp);
	state->config.build_cfg1.num_raw_noise_blks =
		NIST_TRNG_REG_CFG1_NUM_RAW_NOISE_BLKS(tmp);
	state->config.build_cfg1.sticky_startup =
		NIST_TRNG_REG_CFG1_STICKY_STARTUP(tmp);
	state->config.build_cfg1.auto_correlation_test =
		NIST_TRNG_REG_CFG1_AUTO_CORRELATION_TEST(tmp);
	state->config.build_cfg1.mono_bit_test =
		NIST_TRNG_REG_CFG1_MONO_BIT_TEST(tmp);
	state->config.build_cfg1.run_test = NIST_TRNG_REG_CFG1_RUN_TEST(tmp);
	state->config.build_cfg1.poker_test =
		NIST_TRNG_REG_CFG1_POKER_TEST(tmp);
	state->config.build_cfg1.raw_ht_adap_test =
		NIST_TRNG_REG_CFG1_RAW_HT_ADAP_TEST(tmp);
	state->config.build_cfg1.raw_ht_rep_test =
		NIST_TRNG_REG_CFG1_RAW_HT_REP_TEST(tmp);
	state->config.build_cfg1.ent_src_rep_smpl_size =
		NIST_TRNG_REG_CFG1_ENT_SRC_REP_SMPL_SIZE(tmp);
	state->config.build_cfg1.ent_src_rep_test =
		NIST_TRNG_REG_CFG1_ENT_SRC_REP_TEST(tmp);
	state->config.build_cfg1.ent_src_rep_min_entropy =
		NIST_TRNG_REG_CFG1_ENT_SRC_REP_MIN_ENTROPY(tmp);
	DEBUG("NIST_TRNG: BUILD_CFG1 num_raw_noise_blks=%u, sticky_startup=%u, auto_correlation_test=%u\n",
	      state->config.build_cfg1.num_raw_noise_blks,
	      state->config.build_cfg1.sticky_startup,
	      state->config.build_cfg1.auto_correlation_test);
	DEBUG("mono_bit_test=%u, run_test=%u, poker_test=%u, raw_ht_adap_test=%u\n",
	      state->config.build_cfg1.mono_bit_test,
	      state->config.build_cfg1.run_test,
	      state->config.build_cfg1.poker_test,
	      state->config.build_cfg1.raw_ht_adap_test);
	DEBUG("raw_ht_rep_test=%u, ent_src_rep_smpl_size=%u, ent_src_rep_test=%u, ent_src_rep_min_entropy=%u\n",
	      state->config.build_cfg1.raw_ht_rep_test,
	      state->config.build_cfg1.ent_src_rep_smpl_size,
	      state->config.build_cfg1.ent_src_rep_test,
	      state->config.build_cfg1.ent_src_rep_min_entropy);

	tmp = pdu_io_read32(state->base + NIST_TRNG_EDU_BUILD_CFG0);
	state->config.edu_build_cfg0.rbc2_rate_width =
		NIST_TRNG_REG_EDU_CFG0_RBC2_RATE_WIDTH(tmp);
	state->config.edu_build_cfg0.rbc1_rate_width =
		NIST_TRNG_REG_EDU_CFG0_RBC1_RATE_WIDTH(tmp);
	state->config.edu_build_cfg0.rbc0_rate_width =
		NIST_TRNG_REG_EDU_CFG0_RBC0_RATE_WIDTH(tmp);
	state->config.edu_build_cfg0.public_vtrng_channels =
		NIST_TRNG_REG_EDU_CFG0_PUBLIC_VTRNG_CHANNELS(tmp);
	state->config.edu_build_cfg0.esm_channel =
		NIST_TRNG_REG_EDU_CFG0_ESM_CHANNEL(tmp);
	state->config.edu_build_cfg0.rbc_channels =
		NIST_TRNG_REG_EDU_CFG0_RBC_CHANNELS(tmp);
	state->config.edu_build_cfg0.fifo_depth =
		NIST_TRNG_REG_EDU_CFG0_FIFO_DEPTH(tmp);
	DEBUG("NIST_TRNG: EDU_BUILD_CFG0  rbc2_rate_width=%u, rbc1_rate_width=%u, rbc0_rate_width=%u\n",
	      state->config.edu_build_cfg0.rbc2_rate_width,
	      state->config.edu_build_cfg0.rbc1_rate_width,
	      state->config.edu_build_cfg0.rbc0_rate_width);
	DEBUG("public_vtrng_channels=%u, esm_channel=%u, rbc_channels=%u, fifo_depth=%u\n",
	      state->config.edu_build_cfg0.public_vtrng_channels,
	      state->config.edu_build_cfg0.esm_channel,
	      state->config.edu_build_cfg0.rbc_channels,
	      state->config.edu_build_cfg0.fifo_depth);

	state->status.edu_vstat.seed_enum =
		NIST_TRNG_REG_EDU_VSTAT_SEED_ENUM(tmp);
	state->status.edu_vstat.rnc_enabled =
		NIST_TRNG_REG_EDU_VSTAT_RNC_ENABLED(tmp);

	err = nisttrng_zeroize(state);
	if (err)
		goto ERR;

	err = CRYPTO_OK;
	state->status.current_state = NIST_TRNG_STATE_INITIALIZE;
ERR:
	DEBUG("--- %s Return, err = %i\n", __func__, err);
	return err;
} /* nisttrng_init */
EXPORT_SYMBOL(nisttrng_init);

/* Instantiate the DRBG state */
int nisttrng_instantiate(struct nist_trng_state *state, int req_sec_strength,
			 int pred_resist, void *personal_str)
{
	int err;
	u32 tmp;
	u32 zero_ps[12] = { 0 };
	int i = 0;

	DEBUG(">> %s: security strength = %u, pred_resist = %u, personilization string existence = %u\n",
	      __func__, req_sec_strength, pred_resist, (personal_str) ? 1 : 0);

	/* make sure there is no alarm and the core is not busy */
	err = nisttrng_get_alarms(state);
	if (err)
		goto ERR;

	err = nisttrng_wait_on_busy(state);
	if (err)
		goto ERR;

	/* If DRBG is already instantiated or if current state does not allow an instantiate, return error */
	if (DRBG_INSTANTIATED(state->status.current_state)) {
		DEBUG("Initial check: DRBG state is already instantiated\n");
		err = CRYPTO_FAILED;
		goto ERR;
	}
	if (state->status.current_state != NIST_TRNG_STATE_INITIALIZE &&
	    state->status.current_state != NIST_TRNG_STATE_UNINSTANTIATE) {
		DEBUG("Cannot instantiate in the current state (%u)\n",
		      state->status.current_state);
		err = CRYPTO_FAILED;
		goto ERR;
	}

	/* if hardware is not configured to accept extra personalization string, but personal_str is not NULL, return error */
	if (!state->config.features.extra_ps_present && personal_str) {
		DEBUG("HW config does not allow extra PS\n");
		err = CRYPTO_FAILED;
		goto ERR;
	}

	/* Validate and set the security strength */
	err = nisttrng_set_sec_strength(state, req_sec_strength);
	if (err)
		goto ERR;

	/* get entropy - noise seeding. If the mode is nonce, get_entropy must be called by the user prior to the instantiate function */
	DEBUG("Seeding mode is: %s\n",
	      state->status.nonce_mode ? "Nonce" : "Noise");
	if (!state->status.nonce_mode) { /* noise seeding */
		err = nisttrng_get_entropy_input(state, NULL, 0);
		if (err)
			goto ERR;
	}

	/* load the personilization string if hardware is configured to accept it */
	if (state->config.features.extra_ps_present) {
		/* if HW is configured to accept personilizatoin string, it will use whatever is in the NPA_DATAx. So, if the string is NULL, just load 0. */
		if (!personal_str)
			personal_str = &zero_ps[0];

		err = nisttrng_load_ps_addin(state, personal_str);
		if (err)
			goto ERR;
	}

	/* initiate the Create_State command and wait on done */
	DEBUG("Create the DRBG state\n");

	pdu_io_write32(state->base + NIST_TRNG_REG_CTRL,
		       NIST_TRNG_REG_CTRL_CMD_CREATE_STATE);
	err = nisttrng_wait_on_done(state);
	if (err)
		goto ERR;

	/* check STAT register to make sure DRBG is instantiated */
	tmp = pdu_io_read32(state->base + NIST_TRNG_REG_STAT);
	if (!NIST_TRNG_REG_STAT_GET_DRBG_STATE(tmp)) {
		err = CRYPTO_FAILED;
		goto ERR;
	}

	/* reset reminder and alarms counters */
	nisttrng_reset_counters(state);

	//if EDU is available enable RNC and disable prediction resistance , disable all RBC,s
	//state->config.build_cfg0.edu_present = 0;
	if (state->config.build_cfg0.edu_present) {
		//disable prediction resistance
		err = nisttrng_set_pred_resist(state, 0);
		if (err)
			goto ERR;

		//enable RNC
		nisttrng_rnc(state, NIST_TRNG_EDU_RNC_CTRL_CMD_RNC_ENABLE);
		// disable all RBC,s

		tmp = pdu_io_read32(state->base + NIST_TRNG_EDU_RBC_CTRL);
		for (i = 0; i < state->config.edu_build_cfg0.rbc_channels;
		     i++) {
			err = nisttrng_rbc(state, 0, i, 0,
					   CHX_URUN_BLANK_AFTER_RESET);
			if (err)
				goto ERR;
		}
		tmp = pdu_io_read32(state->base + NIST_TRNG_EDU_RBC_CTRL);

	} else {
		/* set the prediction resistance */
		err = nisttrng_set_pred_resist(state, pred_resist);
		if (err)
			goto ERR;
	}

	err = CRYPTO_OK;
	state->status.current_state = NIST_TRNG_STATE_INSTANTIATE;
ERR:
	DEBUG("--- Return %s, err = %i\n", __func__, err);
	return err;
} /* nisttrng_instantiate */
EXPORT_SYMBOL(nisttrng_instantiate);

/* Uninstantiate the DRBG state and zeroize */
int nisttrng_uninstantiate(struct nist_trng_state *state)
{
	int err;
	int err_tmp;
	u32 tmp;

	DEBUG(">> %s: uninstantiate the DRBG and zeroize\n", __func__);
	//printf(" nisttrng_uninstantiate: uninstantiate the DRBG and zeroize\n");
	err = CRYPTO_OK;
	err_tmp = CRYPTO_OK;

	//disable RNC
	if (state->config.build_cfg0.edu_present) {
		if (state->status.edu_vstat.rnc_enabled) {
			DEBUG("%s: disable RNC\n", __func__);
			nisttrng_rnc(state, NIST_TRNG_EDU_RNC_CTRL_CMD_RNC_FINISH_TO_IDLE);
			//always clear the busy bit after disabling RNC
			pdu_io_write32(state->base + NIST_TRNG_REG_ISTAT, tmp);
		}
	}

	/* if DRBG is instantiated, return CRYPTO_NOT_INSTANTIATED, but still do the zeroize */
	if (!DRBG_INSTANTIATED(state->status.current_state))
		err_tmp = CRYPTO_NOT_INSTANTIATED;

	/* zeroize */
	err = nisttrng_zeroize(state);
	if (err)
		goto ERR;

	if (err == CRYPTO_OK && err_tmp == CRYPTO_NOT_INSTANTIATED)
		err = CRYPTO_NOT_INSTANTIATED;

	state->status.current_state = NIST_TRNG_STATE_UNINSTANTIATE;
ERR:
	DEBUG("--- Return %s, err = %i\n", __func__, err);
	return err;
} /* nisttrng_uninstantiate */
EXPORT_SYMBOL(nisttrng_uninstantiate);

/* enable/disable specific rbc
 * rbc_num = rbc channel num
 * urun_blnk = underrun blanking duration for rbc channel
 * rate = sets rate of serial entropy output for rbc channel
 */
int nisttrng_rbc(struct nist_trng_state *state, int enable, int rbc_num, int rate,
		 int urun_blnk)
{
	int err = 0;
	u32 tmp_rbc = 0;

	tmp_rbc = pdu_io_read32(state->base + NIST_TRNG_EDU_RBC_CTRL);

	if (enable) {
		if (rate > 15) {
			DEBUG("Incorrect rate = %d\n", rate);
			err = CRYPTO_FAILED;
			goto ERR;
		}
		if (urun_blnk > 3) {
			DEBUG("Incorrect urun_blnk = %d\n", urun_blnk);
			err = CRYPTO_FAILED;
			goto ERR;
		}
	} else { //disable
		rate = NISTTRNG_EDU_RBC_CTRL_GET_CH_RATE_AFTER_RESET;
		urun_blnk = NISTTRNG_EDU_RBC_CTRL_SET_CH_URUN_BLANK_AFTER_RESET;
	}

	switch (rbc_num) {
	case 0:
		tmp_rbc = NISTTRNG_EDU_RBC_CTRL_SET_CH_RATE(rate, tmp_rbc, _NIST_TRNG_EDU_RBC_CTRL_CH0_RATE);
		tmp_rbc = NISTTRNG_EDU_RBC_CTRL_SET_CH_URUN_BLANK(urun_blnk, tmp_rbc,
								  _NIST_TRNG_EDU_RBC_CTRL_CH0_URUN_BLANK);

		break;
	case 1:
		tmp_rbc = NISTTRNG_EDU_RBC_CTRL_SET_CH_RATE(rate, tmp_rbc, _NIST_TRNG_EDU_RBC_CTRL_CH1_RATE);
		tmp_rbc = NISTTRNG_EDU_RBC_CTRL_SET_CH_URUN_BLANK(urun_blnk, tmp_rbc,
								  _NIST_TRNG_EDU_RBC_CTRL_CH1_URUN_BLANK);

		break;
	case 2:
		tmp_rbc = NISTTRNG_EDU_RBC_CTRL_SET_CH_RATE(rate, tmp_rbc, _NIST_TRNG_EDU_RBC_CTRL_CH2_RATE);
		tmp_rbc = NISTTRNG_EDU_RBC_CTRL_SET_CH_URUN_BLANK(urun_blnk, tmp_rbc,
								  _NIST_TRNG_EDU_RBC_CTRL_CH2_URUN_BLANK);
		break;
	default:
		DEBUG("Incorrect rbc_num = %d\n", rbc_num);
		err = CRYPTO_FAILED;
		goto ERR;
	}

	pdu_io_write32(state->base + NIST_TRNG_EDU_RBC_CTRL, tmp_rbc);

ERR:
	DEBUG("--- Return %s, err = %i\n", __func__, err);
	return err;
}

/* Reseed */
int nisttrng_reseed(struct nist_trng_state *state, int pred_resist, void *addin_str)
{
	int rnc_flag = 0;
	int err;

	DEBUG(">> %s: pred_resist = %u, additional strign existence = %u\n",
	      __func__, pred_resist, (addin_str) ? 1 : 0);

	if (state->config.build_cfg0.edu_present) {
		if (state->status.edu_vstat.rnc_enabled) {
			// disable_rnc
			err = nisttrng_rnc(state, NIST_TRNG_EDU_RNC_CTRL_CMD_RNC_DISABLE_TO_HOLD);
			if (err)
				goto ERR;

			rnc_flag = 1;
		}
	}

	/* make sure there is no alarm and the core is not busy */
	err = nisttrng_get_alarms(state);
	if (err)
		goto ERR;

	err = nisttrng_wait_on_busy(state);
	if (err)
		goto ERR;

	/* if the DRBG is not instantiated return error */
	if (!DRBG_INSTANTIATED(state->status.current_state)) {
		DEBUG("DRBG is not instantiated\n");
		err = CRYPTO_FAILED;
		goto ERR;
	}

	/* if pred_resist is set but, pred_resist that the DRBG is instantiated with is not 1, return error */
	err = nisttrng_set_pred_resist(state, pred_resist);
	if (err)
		goto ERR;

	/* get entropy - noise seeding. If the mode is nonce, get_entropy must be called by the user prior to the instantiate function */
	if (!state->status.nonce_mode) { /* noise seeding */
		err = nisttrng_get_entropy_input(state, NULL, 0);
		if (err)
			goto ERR;
	}

	/* if addin_str is not NULL, it means that the additionl input is available and has to be loaded */
	if (addin_str) {
		/* set the ADDIN_PRESENT field of the MODE register to 1 */
		err = nisttrng_set_addin_present(state, 1);
		if (err)
			goto ERR;

		/* load the additional input */
		err = nisttrng_load_ps_addin(state, addin_str);
		if (err)
			goto ERR;

	} else {
		/* set the ADDIN_PRESENT field of the MODE register to 0 */
		err = nisttrng_set_addin_present(state, 0);
		if (err)
			goto ERR;
	}

	/* initiate the reseed and wait on done */
	pdu_io_write32(state->base + NIST_TRNG_REG_CTRL,
		       NIST_TRNG_REG_CTRL_CMD_RENEW_STATE);
	err = nisttrng_wait_on_done(state);
	if (err)
		goto ERR;

	/* reset reminder and alarms counters */
	nisttrng_reset_counters(state);

	if (rnc_flag) {
		// rnc_enable
		err = nisttrng_rnc(state, NIST_TRNG_EDU_RNC_CTRL_CMD_RNC_ENABLE);
		if (err)
			goto ERR;
	}

	err = CRYPTO_OK;
	state->status.current_state = NIST_TRNG_STATE_RESEED;
ERR:
	DEBUG("--- Return %s, err = %i\n", __func__, err);
	return err;
} /* nisttrng_reseed */
EXPORT_SYMBOL(nisttrng_reseed);

int nisttrng_vtrng_wait_on_busy(struct nist_trng_state *state, int priv, int vtrng)
{
	u32 tmp, t;

	t = NIST_TRNG_RETRY_MAX;

	if (priv) { //private vtrng
		do {
			tmp = pdu_io_read32(state->base + NIST_TRNG_EDU_VSTAT);
		} while (NIST_TRNG_REG_EDU_VSTAT_BUSY(tmp) && --t);

	} else { //public vtrng
		do {
			tmp = pdu_io_read32(state->base +
					    NIST_TRNG_EDU_VTRNG_VSTAT0 +
					    8 * vtrng);
		} while (NIST_TRNG_REG_EDU_VSTAT_BUSY(tmp) && --t);
	}

	if (t)
		return CRYPTO_OK;

	SYNHW_PRINT("wait_on_: failed timeout: %08lx\n",
		    (unsigned long)tmp);

	return CRYPTO_TIMEOUT;
} /* nisttrng_vtrng_wait_on_busy */

int nisttrng_generate_public_vtrng(struct nist_trng_state *state, void *random_bits,
				   unsigned long req_num_bytes, int vtrng)
{
	int err = 0;
	u32 tmp;
	unsigned int remained_bytes;
	unsigned long req_num_blks;
	int i, j;

	DEBUG(">> %s : requested number of bytes = %lu, vtrng num = %u\n",
	      __func__, req_num_bytes, vtrng);

	/* make sure random_bits is not NULL */
	if (!random_bits) {
		DEBUG("random_bits pointer cannot be NULL\n");
		err = CRYPTO_FAILED;
		goto ERR;
	}

	if (vtrng > state->config.edu_build_cfg0.public_vtrng_channels) {
		DEBUG("vtrng channel invalid (%u)\n", vtrng);
		err = CRYPTO_FAILED;
		goto ERR;
	}

	if (state->status.edu_vstat.rnc_enabled == 0)
		DEBUG("rnc_disabled\n");

	if (state->status.edu_vstat.seed_enum == 0)
		DEBUG("not seed_enum\n");

	/* loop on generate to get the requested number of bits. Each generate gives NIST_TRNG_RAND_BLK_SIZE_BITS bits. */
	req_num_blks = ((req_num_bytes * 8) % NIST_TRNG_RAND_BLK_SIZE_BITS) ?
			(((req_num_bytes * 8) / NIST_TRNG_RAND_BLK_SIZE_BITS) + 1) :
			((req_num_bytes * 8) / NIST_TRNG_RAND_BLK_SIZE_BITS);

	for (i = 0; i < req_num_blks; i++) {
		tmp = pdu_io_read32(state->base + NIST_TRNG_EDU_VTRNG_VCTRL0 +
				    (vtrng * 8));
		tmp = NIST_TRNG_EDU_VTRNG_VCTRL_CMD_SET(tmp, NIST_TRNG_EDU_VTRNG_VCTRL_CMD_GET_RANDOM);
		pdu_io_write32(state->base + NIST_TRNG_EDU_VTRNG_VCTRL0 + (vtrng * 8),
			       tmp);

		// check busy
		err = nisttrng_vtrng_wait_on_busy(state, 0, vtrng);
		if (err)
			goto ERR;

		// check for error
		tmp = pdu_io_read32(state->base + NIST_TRNG_EDU_VTRNG_VISTAT0 +
				    (vtrng * 8));
		if (NIST_TRNG_REG_EDU_VSTAT_ANY_RW1(tmp))
			DEBUG("EDU_VSTAT_ANY_RW1 set 0x%x\n", tmp);

		// check that all valid
		tmp = pdu_io_read32(state->base + NIST_TRNG_EDU_VTRNG_VSTAT0 +
				    8 * vtrng);
		if ((NIST_TRNG_REG_EDU_VSTAT_SLICE_VLD0(tmp) == 0) ||
		    (NIST_TRNG_REG_EDU_VSTAT_SLICE_VLD1(tmp) == 0) ||
		    (NIST_TRNG_REG_EDU_VSTAT_SLICE_VLD2(tmp) == 0) ||
		    (NIST_TRNG_REG_EDU_VSTAT_SLICE_VLD3(tmp) == 0)) {
			DEBUG("EDU_VSTAT_SLICE_VLD fail 0x%x\n", tmp);
		}

		/* read the generated random number block and store */
		for (j = 0; j < (NIST_TRNG_RAND_BLK_SIZE_BITS / 32); j++) {
			tmp = pdu_io_read32(state->base +
					    NIST_TRNG_EDU_VTRNG_VRAND0_0 +
					    (vtrng * 8) + j);
			/* copy to random_bits byte-by-byte, until req_num_bytes are copied */
			remained_bytes = req_num_bytes -
				(i * (NIST_TRNG_RAND_BLK_SIZE_BITS / 8) +
				 j * 4);
			if (remained_bytes > 4) {
				memcpy(random_bits + i * (NIST_TRNG_RAND_BLK_SIZE_BITS / 8) +
				       j * 4, &tmp, 4);

				/* decrement the bits counter and return error if generated more than the maximum*/
				state->counters.bits_per_req_left =
					state->counters.bits_per_req_left -
					4 * 8;
				if (state->counters.bits_per_req_left < 0) {
					err = CRYPTO_FAILED;
					goto ERR;
				}
			} else {
				memcpy(random_bits + i * (NIST_TRNG_RAND_BLK_SIZE_BITS / 8) +
				       j * 4, &tmp, remained_bytes);

				/* decrement the bits counter and return error if generated more than the maximum*/
				state->counters.bits_per_req_left =
					state->counters.bits_per_req_left -
					remained_bytes * 8;
				if (state->counters.bits_per_req_left < 0) {
					err = CRYPTO_FAILED;
					goto ERR;
				}
				break;
			}
		}
	}

	err = CRYPTO_OK;
	state->status.current_state = NIST_TRNG_STATE_GENERATE;
ERR:
	if (err)
		random_bits = NULL;

	DEBUG("--- Return %s, err = %i\n", __func__, err);
	return err;
}

int nisttrng_generate_private_vtrng(struct nist_trng_state *state, void *random_bits,
				    unsigned long req_num_bytes)
{
	int err;
	u32 tmp;
	unsigned int remained_bytes;
	unsigned long req_num_blks;
	int i, j;

	DEBUG(">> %s : requested number of bytes = %lu ",
	      __func__, req_num_bytes);

	/* requested number of bits has to be less that the programmed maximum */
	if ((req_num_bytes * 8) > state->counters.max_bits_per_req) {
		SYNHW_PRINT("requested number of bits (%lu) is larger than the set maximum (%lu)\n",
			    (req_num_bytes * 8), state->counters.max_bits_per_req);
		err = CRYPTO_FAILED;
		goto ERR;
	}

	/* make sure random_bits is not NULL */
	if (!random_bits) {
		SYNHW_PRINT("random_bits pointer cannot be NULL\n");
		err = CRYPTO_FAILED;
		goto ERR;
	}

	if (state->status.edu_vstat.rnc_enabled == 0)
		DEBUG("rnc_disabled\n");

	if (state->status.edu_vstat.seed_enum == 0)
		DEBUG("not seed_enum\n");

	/* loop on generate to get the requested number of bits. Each generate gives NIST_TRNG_RAND_BLK_SIZE_BITS bits. */
	req_num_blks = ((req_num_bytes * 8) % NIST_TRNG_RAND_BLK_SIZE_BITS) ?
			(((req_num_bytes * 8) / NIST_TRNG_RAND_BLK_SIZE_BITS) + 1) :
			((req_num_bytes * 8) / NIST_TRNG_RAND_BLK_SIZE_BITS);

	for (i = 0; i < req_num_blks; i++) {
		tmp = pdu_io_read32(state->base + NIST_TRNG_EDU_VCTRL);
		tmp = NIST_TRNG_EDU_VTRNG_VCTRL_CMD_SET(tmp, NIST_TRNG_EDU_VTRNG_VCTRL_CMD_GET_RANDOM);
		pdu_io_write32(state->base + NIST_TRNG_EDU_VCTRL, tmp);

		// check busy
		err = nisttrng_vtrng_wait_on_busy(state, 1, 0);
		if (err)
			goto ERR;

		// check for error
		tmp = pdu_io_read32(state->base + NIST_TRNG_EDU_VISTAT);
		if (NIST_TRNG_REG_EDU_VSTAT_ANY_RW1(tmp))
			DEBUG("EDU_VSTAT_ANY_RW1 set 0x%x\n", tmp);

		//check that all valid
		tmp = pdu_io_read32(state->base + NIST_TRNG_EDU_VSTAT);
		if ((NIST_TRNG_REG_EDU_VSTAT_SLICE_VLD0(tmp) == 0) ||
		    (NIST_TRNG_REG_EDU_VSTAT_SLICE_VLD1(tmp) == 0) ||
		    (NIST_TRNG_REG_EDU_VSTAT_SLICE_VLD2(tmp) == 0) ||
		    (NIST_TRNG_REG_EDU_VSTAT_SLICE_VLD3(tmp) == 0)) {
			DEBUG("EDU_VSTAT_SLICE_VLD fail 0x%x\n", tmp);
		}

		/* read the generated random number block and store */
		for (j = 0; j < (NIST_TRNG_RAND_BLK_SIZE_BITS / 32); j++) {
			tmp = pdu_io_read32(state->base +
					    NIST_TRNG_EDU_VRAND_0 + j);
			/* copy to random_bits byte-by-byte, until req_num_bytes are copied */
			remained_bytes = req_num_bytes -
				(i * (NIST_TRNG_RAND_BLK_SIZE_BITS / 8) +
				 j * 4);
			if (remained_bytes > 4) {
				memcpy(random_bits + i * (NIST_TRNG_RAND_BLK_SIZE_BITS / 8) +
				       j * 4, &tmp, 4);

				/* decrement the bits counter and return error if generated more than the maximum*/
				state->counters.bits_per_req_left =
					state->counters.bits_per_req_left -
					4 * 8;
				if (state->counters.bits_per_req_left < 0) {
					err = CRYPTO_FAILED;
					goto ERR;
				}
			} else {
				memcpy(random_bits + i * (NIST_TRNG_RAND_BLK_SIZE_BITS / 8) +
				       j * 4, &tmp, remained_bytes);

				/* decrement the bits counter and return error if generated more than the maximum*/
				state->counters.bits_per_req_left =
					state->counters.bits_per_req_left -
					remained_bytes * 8;
				if (state->counters.bits_per_req_left < 0) {
					err = CRYPTO_FAILED;
					goto ERR;
				}
				break;
			}
		}
	}
ERR:
	DEBUG("--- Return %s, err = %i\n", __func__, err);
	return err;
}

/* Generate */
int nisttrng_generate(struct nist_trng_state *state, void *random_bits,
		      unsigned long req_num_bytes, int req_sec_strength,
		      int pred_resist, void *addin_str)
{
	int err;
	int reseed_required;

	DEBUG(">> %s: requested number of bytes = %lu, security strength = %u, pred_resist = %u, additional string existence = %u\n",
	      __func__, req_num_bytes, req_sec_strength, pred_resist,
	      (addin_str) ? 1 : 0);

	/* make sure there is no alarm and the core is not busy */
	err = nisttrng_get_alarms(state);
	if (err)
		goto ERR;

	err = nisttrng_wait_on_busy(state);
	if (err)
		goto ERR;

	/* if the DRBG is not instantiated return error */
	if (!DRBG_INSTANTIATED(state->status.current_state)) {
		SYNHW_PRINT("DRBG is not instantiated\n");
		err = CRYPTO_FAILED;
		goto ERR;
	}

	/* requested number of bits has to be less that the programmed maximum */
	if ((req_num_bytes * 8) > state->counters.max_bits_per_req) {
		SYNHW_PRINT("requested number of bits (%lu) is larger than the set maximum (%lu)\n",
			    (req_num_bytes * 8), state->counters.max_bits_per_req);
		err = CRYPTO_FAILED;
		goto ERR;
	}

	/* security strength has to be lower than what the DRBG is instantiated with. set_sec_strength function checks this. */
	err = nisttrng_set_sec_strength(state, req_sec_strength);
	if (err)
		goto ERR;

	/* set the prediction resistance - if pred_resist is set but, pred_resist that the DRBG is instantiated with is not 1, return error */
	err = nisttrng_set_pred_resist(state, pred_resist);
	if (err)
		goto ERR;

	/* make sure random_bits is not NULL */
	if (!random_bits) {
		DEBUG("random_bits pointer cannot be NULL\n");
		err = CRYPTO_FAILED;
		goto ERR;
	}

	/* set the reseed required flag to 0. The loop is to check at the end whether a reseed is required at the end and jump back to reseed and generate if needed. This is the NIST mandated procedure */
	reseed_required = 0;

	if (!addin_str) {
		/* set the ADDIN_PRESENT field of the MODE register to 1 */
		err = nisttrng_set_addin_present(state, 0);
		if (err)
			goto ERR;
	}

	do {
		void *generate_addin_str = addin_str;

		if (pred_resist | reseed_required) {
			err = nisttrng_reseed(state, pred_resist, addin_str);
			if (err)
				goto ERR;

			/* SP800-90a says that if reseed is executed, any additional input string is only used in the reseed phase and replaced by NULL in the generate phase */
			generate_addin_str = NULL;
			err = nisttrng_set_addin_present(state, 0);
			if (err)
				goto ERR;

			/* ADDIN_PRESENT field in MODE has to be set back to 0 to avoid illegal cmd sequence */
			reseed_required = 0;
		}

		/* generate process */
		if (nisttrng_check_seed_lifetime(state) == CRYPTO_RESEED_REQUIRED) {
			reseed_required = 1;

		} else {
			reseed_required = 0;

			/* Refresh_Addin command if additional input is not NULL*/
			if (generate_addin_str) {
				err = nisttrng_refresh_addin(state, generate_addin_str);
				if (err)
					goto ERR;
			}

			/* Generate all random bits */
			/* if EDU present then get random number from private vtrng */

			//state->config.build_cfg0.edu_present = 0;
			if (state->config.build_cfg0.edu_present) {
				err = nisttrng_generate_private_vtrng(state, random_bits,
								      req_num_bytes);
				if (err)
					goto ERR;

			} else {
				err = nisttrng_gen_random(state, random_bits,
							  req_num_bytes);
				if (err)
					goto ERR;

				/* Advance the state - if it returns CRYPTO_RESEED_REQUIRED, have to jump back and do a reseed and generate */
				err = nisttrng_advance_state(state);
				if (err)
					goto ERR;
			}
		}
	} while (reseed_required);

	err = CRYPTO_OK;
	state->status.current_state = NIST_TRNG_STATE_GENERATE;
ERR:
	if (err)
		random_bits = NULL;

	DEBUG("--- Return %s, err = %i\n", __func__, err);
	return err;
} /* nisttrng_generate */
EXPORT_SYMBOL(nisttrng_generate);
