/* SPDX-License-Identifier: GPL-2.0 */
// ------------------------------------------------------------------------
//
//                (C) COPYRIGHT 2012 - 2016 SYNOPSYS, INC.
//                          ALL RIGHTS RESERVED
//
//  (C) COPYRIGHT 2012-2016 Synopsys, Inc.
//  This Synopsys software and all associated documentation are
//  proprietary to Synopsys, Inc. and may only be used pursuant
//  to the terms and conditions of a written license agreement
//  with Synopsys, Inc. All other use, reproduction, modification,
//  or distribution of the Synopsys software or the associated
//  documentation is strictly prohibited.
//
// ------------------------------------------------------------------------

#ifndef NISTTRNG_COMMON_H
#define NISTTRNG_COMMON_H

#define NIST_TRNG_RETRY_MAX 5000000UL

#define NIST_DFLT_MAX_BITS_PER_REQ	BIT(19)
#define NIST_DFLT_MAX_REQ_PER_SEED	BIT(48)

/* Do not change the following parameters */
#define NIST_TRNG_DFLT_MAX_REJECTS 10

#define DEBUG(...)
//#define DEBUG(...) printk(__VA_ARGS__)

enum nisttrng_sec_strength {
	SEC_STRNT_AES128 = 0,
	SEC_STRNT_AES256 = 1
};

enum nisttrng_drbg_arch {
	AES128 = 0,
	AES256 = 1
};

enum nisttrng_current_state {
	NIST_TRNG_STATE_INITIALIZE = 0,
	NIST_TRNG_STATE_UNINSTANTIATE,
	NIST_TRNG_STATE_INSTANTIATE,
	NIST_TRNG_STATE_RESEED,
	NIST_TRNG_STATE_GENERATE
};

struct nist_trng_state {
	u32 *base;

	/* Hardware features and build ID */
	struct {
		struct {
			enum nisttrng_drbg_arch drbg_arch;
			unsigned int extra_ps_present,
				 secure_rst_state,
				 diag_level_basic_trng,
				 diag_level_stat_hlt,
				 diag_level_ns;
		} features;

		struct {
			unsigned int ext_enum,
				 ext_ver,
				 rel_num;
		} corekit_rel;

		struct {
			unsigned int core_type,
				 bg8,
				 cdc_synch_depth,
				 background_noise,
				 edu_present,
				 aes_datapath,
				 aes_max_key_size,
				 personilzation_str;
		} build_cfg0;

		struct {
			unsigned int num_raw_noise_blks,
				 sticky_startup,
				 auto_correlation_test,
				 mono_bit_test,
				 run_test,
				 poker_test,
				 raw_ht_adap_test,
				 raw_ht_rep_test,
				 ent_src_rep_smpl_size,
				 ent_src_rep_test,
				 ent_src_rep_min_entropy;
		} build_cfg1;

		struct {
			unsigned int rbc2_rate_width,
				 rbc1_rate_width,
				 rbc0_rate_width,
				 public_vtrng_channels,
				 esm_channel,
				 rbc_channels,
				 fifo_depth;
		} edu_build_cfg0;
	} config;

	/* status */
	struct {
		//nist_trng_current_state current_state;
		enum nisttrng_current_state current_state;  // old for now
		unsigned int nonce_mode,
			 secure_mode,
			 pred_resist;
		//nist_trng_sec_strength sec_strength;
		enum nisttrng_sec_strength sec_strength;
		unsigned int pad_ps_addin;
		unsigned int alarm_code;
		// Private VTRNG STAT, all the public trng will have the same STAT as public TRNG in terms of
		// rnc_enabled and seed_enum
		struct {
			unsigned int seed_enum,
				 rnc_enabled;
		} edu_vstat;
	} status;

	/* reminders and alarms */
	struct {
		unsigned long max_bits_per_req;
		unsigned long long max_req_per_seed;
		unsigned long bits_per_req_left;
		unsigned long long req_per_seed_left;
	} counters;
};

#define nist_trng_zero_status(x) \
	memset(&((x)->status), 0, sizeof((x)->status))

#define DRBG_INSTANTIATED(cs) \
	((((cs) == NIST_TRNG_STATE_INSTANTIATE) || \
	((cs) == NIST_TRNG_STATE_RESEED) || \
	((cs) == NIST_TRNG_STATE_GENERATE)) ? 1 : 0)

#define REQ_SEC_STRENGTH_IS_VALID(sec_st) \
	((((sec_st) > 0) && ((sec_st) <= 256)) ? 1 : 0)

#endif
