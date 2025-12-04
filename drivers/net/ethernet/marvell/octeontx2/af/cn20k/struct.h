/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#ifndef STRUCT_H
#define STRUCT_H

#define NIX_MAX_CTX_SIZE		128

/*
 * CN20k RVU PF MBOX Interrupt Vector Enumeration
 *
 * Vectors 0 - 3 are compatible with pre cn20k and hence
 * existing macros are being reused.
 */
enum rvu_mbox_pf_int_vec_e {
	RVU_MBOX_PF_INT_VEC_VFPF_MBOX0	= 0x4,
	RVU_MBOX_PF_INT_VEC_VFPF_MBOX1	= 0x5,
	RVU_MBOX_PF_INT_VEC_VFPF1_MBOX0	= 0x6,
	RVU_MBOX_PF_INT_VEC_VFPF1_MBOX1	= 0x7,
	RVU_MBOX_PF_INT_VEC_AFPF_MBOX	= 0x8,
	RVU_MBOX_PF_INT_VEC_CNT		= 0x9,
};

/* RVU Admin function Interrupt Vector Enumeration */
enum rvu_af_cn20k_int_vec_e {
	RVU_AF_CN20K_INT_VEC_POISON		= 0x0,
	RVU_AF_CN20K_INT_VEC_PFFLR0		= 0x1,
	RVU_AF_CN20K_INT_VEC_PFFLR1		= 0x2,
	RVU_AF_CN20K_INT_VEC_PFME0		= 0x3,
	RVU_AF_CN20K_INT_VEC_PFME1		= 0x4,
	RVU_AF_CN20K_INT_VEC_GEN		= 0x5,
	RVU_AF_CN20K_INT_VEC_PFAF_MBOX0		= 0x6,
	RVU_AF_CN20K_INT_VEC_PFAF_MBOX1		= 0x7,
	RVU_AF_CN20K_INT_VEC_PFAF1_MBOX0	= 0x8,
	RVU_AF_CN20K_INT_VEC_PFAF1_MBOX1	= 0x9,
	RVU_AF_CN20K_INT_VEC_CNT		= 0xa,
};

struct nix_cn20k_sq_ctx_s {
	u64 ena                         :  1; /* W0 */
	u64 qint_idx                    :  6;
	u64 substream                   : 20;
	u64 sdp_mcast                   :  1;
	u64 cq                          : 20;
	u64 sqe_way_mask                : 16;
	u64 smq                         : 11; /* W1 */
	u64 cq_ena                      :  1;
	u64 xoff                        :  1;
	u64 sso_ena                     :  1;
	u64 smq_rr_weight               : 14;
	u64 default_chan                : 12;
	u64 sqb_count                   : 16;
	u64 reserved_120_120            :  1;
	u64 smq_rr_count_lb             :  7;
	u64 smq_rr_count_ub             : 25; /* W2 */
	u64 sqb_aura                    : 20;
	u64 sq_int                      :  8;
	u64 sq_int_ena                  :  8;
	u64 sqe_stype                   :  2;
	u64 reserved_191_191            :  1;
	u64 max_sqe_size                :  2; /* W3 */
	u64 cq_limit                    :  8;
	u64 lmt_dis                     :  1;
	u64 mnq_dis                     :  1;
	u64 smq_next_sq                 : 20;
	u64 smq_lso_segnum              :  8;
	u64 tail_offset                 :  6;
	u64 smenq_offset                :  6;
	u64 head_offset                 :  6;
	u64 smenq_next_sqb_vld          :  1;
	u64 smq_pend                    :  1;
	u64 smq_next_sq_vld             :  1;
	u64 reserved_253_255            :  3;
	u64 next_sqb                    : 64; /* W4 */
	u64 tail_sqb                    : 64; /* W5 */
	u64 smenq_sqb                   : 64; /* W6 */
	u64 smenq_next_sqb              : 64; /* W7 */
	u64 head_sqb                    : 64; /* W8 */
	u64 reserved_576_583            :  8; /* W9 */
	u64 vfi_lso_total               : 18;
	u64 vfi_lso_sizem1              :  3;
	u64 vfi_lso_sb                  :  8;
	u64 vfi_lso_mps                 : 14;
	u64 vfi_lso_vlan0_ins_ena       :  1;
	u64 vfi_lso_vlan1_ins_ena       :  1;
	u64 vfi_lso_vld                 :  1;
	u64 reserved_630_639            : 10;
	u64 scm_lso_rem                 : 18; /* W10 */
	u64 reserved_658_703            : 46;
	u64 octs                        : 48; /* W11 */
	u64 reserved_752_767            : 16;
	u64 pkts                        : 48; /* W12 */
	u64 reserved_816_831            : 16;
	u64 aged_drop_octs              : 32; /* W13 */
	u64 aged_drop_pkts              : 32;
	u64 dropped_octs                : 48; /* W14 */
	u64 reserved_944_959            : 16;
	u64 dropped_pkts                : 48; /* W15 */
	u64 reserved_1008_1023          : 16;
};

static_assert(sizeof(struct nix_cn20k_sq_ctx_s) == NIX_MAX_CTX_SIZE);

struct nix_cn20k_cq_ctx_s {
	u64 base                        : 64; /* W0 */
	u64 lbp_ena                     :  1; /* W1 */
	u64 lbpid_low                   :  3;
	u64 bp_ena                      :  1;
	u64 lbpid_med                   :  3;
	u64 bpid                        :  9;
	u64 lbpid_high                  :  3;
	u64 qint_idx                    :  7;
	u64 cq_err                      :  1;
	u64 cint_idx                    :  7;
	u64 avg_con                     :  9;
	u64 wrptr                       : 20;
	u64 tail                        : 20; /* W2 */
	u64 head                        : 20;
	u64 avg_level                   :  8;
	u64 update_time                 : 16;
	u64 bp                          :  8; /* W3 */
	u64 drop                        :  8;
	u64 drop_ena                    :  1;
	u64 ena                         :  1;
	u64 cpt_drop_err_en             :  1;
	u64 reserved_211_211            :  1;
	u64 msh_dst                     : 11;
	u64 msh_valid                   :  1;
	u64 stash_thresh                :  4;
	u64 lbp_frac                    :  4;
	u64 caching                     :  1;
	u64 stashing                    :  1;
	u64 reserved_234_235            :  2;
	u64 qsize                       :  4;
	u64 cq_err_int                  :  8;
	u64 cq_err_int_ena              :  8;
	u64 bpid_ext                    :  2; /* W4 */
	u64 reserved_258_259            :  2;
	u64 lbpid_ext                   :  2;
	u64 reserved_262_319            : 58;
	u64 reserved_320_383            : 64; /* W5 */
	u64 reserved_384_447            : 64; /* W6 */
	u64 reserved_448_511            : 64; /* W7 */
	u64 padding[8];
};

static_assert(sizeof(struct nix_cn20k_sq_ctx_s) == NIX_MAX_CTX_SIZE);

struct nix_cn20k_rq_ctx_s {
	u64 ena                         :  1;
	u64 sso_ena                     :  1;
	u64 ipsech_ena                  :  1;
	u64 ena_wqwd                    :  1;
	u64 cq                          : 20;
	u64 reserved_24_34              : 11;
	u64 port_il4_dis                :  1;
	u64 port_ol4_dis                :  1;
	u64 lenerr_dis                  :  1;
	u64 csum_il4_dis                :  1;
	u64 csum_ol4_dis                :  1;
	u64 len_il4_dis                 :  1;
	u64 len_il3_dis                 :  1;
	u64 len_ol4_dis                 :  1;
	u64 len_ol3_dis                 :  1;
	u64 wqe_aura                    : 20;
	u64 spb_aura                    : 20;
	u64 lpb_aura                    : 20;
	u64 sso_grp                     : 10;
	u64 sso_tt                      :  2;
	u64 pb_caching                  :  2;
	u64 wqe_caching                 :  1;
	u64 xqe_drop_ena                :  1;
	u64 spb_drop_ena                :  1;
	u64 lpb_drop_ena                :  1;
	u64 pb_stashing                 :  1;
	u64 ipsecd_drop_en              :  1;
	u64 chi_ena                     :  1;
	u64 reserved_125_127            :  3;
	u64 band_prof_id_l              : 10;
	u64 sso_fc_ena                  :  1;
	u64 policer_ena                 :  1;
	u64 spb_sizem1                  :  6;
	u64 wqe_skip                    :  2;
	u64 spb_high_sizem1             :  3;
	u64 spb_ena                     :  1;
	u64 lpb_sizem1                  : 12;
	u64 first_skip                  :  7;
	u64 reserved_171_171            :  1;
	u64 later_skip                  :  6;
	u64 xqe_imm_size                :  6;
	u64 band_prof_id_h              :  4;
	u64 reserved_188_189            :  2;
	u64 xqe_imm_copy                :  1;
	u64 xqe_hdr_split               :  1;
	u64 xqe_drop                    :  8;
	u64 xqe_pass                    :  8;
	u64 wqe_pool_drop               :  8;
	u64 wqe_pool_pass               :  8;
	u64 spb_aura_drop               :  8;
	u64 spb_aura_pass               :  8;
	u64 spb_pool_drop               :  8;
	u64 spb_pool_pass               :  8;
	u64 lpb_aura_drop               :  8;
	u64 lpb_aura_pass               :  8;
	u64 lpb_pool_drop               :  8;
	u64 lpb_pool_pass               :  8;
	u64 reserved_288_291            :  4;
	u64 rq_int                      :  8;
	u64 rq_int_ena                  :  8;
	u64 qint_idx                    :  7;
	u64 reserved_315_319            :  5;
	u64 ltag                        : 24;
	u64 good_utag                   :  8;
	u64 bad_utag                    :  8;
	u64 flow_tagw                   :  6;
	u64 ipsec_vwqe                  :  1;
	u64 vwqe_ena                    :  1;
	u64 vtime_wait                  :  8;
	u64 max_vsize_exp               :  4;
	u64 vwqe_skip                   :  2;
	u64 reserved_382_383            :  2;
	u64 octs                        : 48;
	u64 reserved_432_447            : 16;
	u64 pkts                        : 48;
	u64 reserved_496_511            : 16;
	u64 drop_octs                   : 48;
	u64 reserved_560_575            : 16;
	u64 drop_pkts                   : 48;
	u64 reserved_624_639            : 16;
	u64 re_pkts                     : 48;
	u64 reserved_688_703            : 16;
	u64 reserved_704_767            : 64;
	u64 reserved_768_831            : 64;
	u64 reserved_832_895            : 64;
	u64 reserved_896_959            : 64;
	u64 reserved_960_1023           : 64;
};

static_assert(sizeof(struct nix_cn20k_rq_ctx_s) == NIX_MAX_CTX_SIZE);

struct npa_cn20k_aura_s {
	u64 pool_addr;			/* W0 */
	u64 ena                   : 1;  /* W1 */
	u64 reserved_65           : 2;
	u64 pool_caching          : 1;
	u64 reserved_68           : 16;
	u64 avg_con               : 9;
	u64 reserved_93           : 1;
	u64 pool_drop_ena         : 1;
	u64 aura_drop_ena         : 1;
	u64 bp_ena                : 1;
	u64 reserved_97_103       : 7;
	u64 aura_drop             : 8;
	u64 shift                 : 6;
	u64 reserved_118_119      : 2;
	u64 avg_level             : 8;
	u64 count                 : 36; /* W2 */
	u64 reserved_164_167      : 4;
	u64 bpid                  : 12;
	u64 reserved_180_191      : 12;
	u64 limit                 : 36; /* W3 */
	u64 reserved_228_231      : 4;
	u64 bp                    : 7;
	u64 reserved_239_243      : 5;
	u64 fc_ena                : 1;
	u64 fc_up_crossing        : 1;
	u64 fc_stype              : 2;
	u64 fc_hyst_bits          : 4;
	u64 reserved_252_255      : 4;
	u64 fc_addr;			/* W4 */
	u64 pool_drop             : 8;  /* W5 */
	u64 update_time           : 16;
	u64 err_int               : 8;
	u64 err_int_ena           : 8;
	u64 thresh_int            : 1;
	u64 thresh_int_ena        : 1;
	u64 thresh_up             : 1;
	u64 reserved_363          : 1;
	u64 thresh_qint_idx       : 7;
	u64 reserved_371          : 1;
	u64 err_qint_idx          : 7;
	u64 reserved_379_383      : 5;
	u64 thresh                : 36; /* W6*/
	u64 rsvd_423_420          : 4;
	u64 fc_msh_dst            : 11;
	u64 reserved_435_438      : 4;
	u64 op_dpc_ena            : 1;
	u64 op_dpc_set            : 5;
	u64 reserved_445_445      : 1;
	u64 stream_ctx            : 1;
	u64 unified_ctx           : 1;
	u64 reserved_448_511;		/* W7 */
	u64 padding[8];
};

static_assert(sizeof(struct npa_cn20k_aura_s) == NIX_MAX_CTX_SIZE);

struct npa_cn20k_pool_s {
	u64 stack_base;			/* W0 */
	u64 ena                   : 1;
	u64 nat_align             : 1;
	u64 reserved_66_67        : 2;
	u64 stack_caching         : 1;
	u64 reserved_69_87        : 19;
	u64 buf_offset            : 12;
	u64 reserved_100_103      : 4;
	u64 buf_size              : 12;
	u64 reserved_116_119      : 4;
	u64 ref_cnt_prof          : 3;
	u64 reserved_123_127      : 5;
	u64 stack_max_pages       : 32;
	u64 stack_pages           : 32;
	uint64_t bp_0             : 7;
	uint64_t bp_1             : 7;
	uint64_t bp_2             : 7;
	uint64_t bp_3             : 7;
	uint64_t bp_4             : 7;
	uint64_t bp_5             : 7;
	uint64_t bp_6             : 7;
	uint64_t bp_7             : 7;
	uint64_t bp_ena_0         : 1;
	uint64_t bp_ena_1         : 1;
	uint64_t bp_ena_2         : 1;
	uint64_t bp_ena_3         : 1;
	uint64_t bp_ena_4         : 1;
	uint64_t bp_ena_5         : 1;
	uint64_t bp_ena_6         : 1;
	uint64_t bp_ena_7         : 1;
	u64 stack_offset          : 4;
	u64 reserved_260_263      : 4;
	u64 shift                 : 6;
	u64 reserved_270_271      : 2;
	u64 avg_level             : 8;
	u64 avg_con               : 9;
	u64 fc_ena                : 1;
	u64 fc_stype              : 2;
	u64 fc_hyst_bits          : 4;
	u64 fc_up_crossing        : 1;
	u64 reserved_297_299      : 3;
	u64 update_time           : 16;
	u64 reserved_316_319      : 4;
	u64 fc_addr;			/* W5 */
	u64 ptr_start;			/* W6 */
	u64 ptr_end;			/* W7 */
	u64 bpid_0                : 12;
	u64 reserved_524_535      : 12;
	u64 err_int               : 8;
	u64 err_int_ena           : 8;
	u64 thresh_int            : 1;
	u64 thresh_int_ena        : 1;
	u64 thresh_up             : 1;
	u64 reserved_555          : 1;
	u64 thresh_qint_idx       : 7;
	u64 reserved_563          : 1;
	u64 err_qint_idx          : 7;
	u64 reserved_571_575      : 5;
	u64 thresh                : 36;
	u64 rsvd_612_615	  : 4;
	u64 fc_msh_dst		  : 11;
	u64 reserved_627_630      : 4;
	u64 op_dpc_ena            : 1;
	u64 op_dpc_set            : 5;
	u64 reserved_637_637      : 1;
	u64 stream_ctx            : 1;
	u64 reserved_639          : 1;
	u64 reserved_640_703;		/* W10 */
	u64 reserved_704_767;		/* W11 */
	u64 reserved_768_831;		/* W12 */
	u64 reserved_832_895;		/* W13 */
	u64 reserved_896_959;		/* W14 */
	u64 reserved_960_1023;		/* W15 */
};

static_assert(sizeof(struct npa_cn20k_pool_s) == NIX_MAX_CTX_SIZE);

#endif
