/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2026 Marvell.
 *
 */

#ifndef NPC_CN20K_H
#define NPC_CN20K_H

#define MKEX_CN20K_SIGN	0x19bbfdbd160

#define MAX_NUM_BANKS 2
#define MAX_NUM_SUB_BANKS 32
#define MAX_SUBBANK_DEPTH 256

/* strtoull of "mkexprof" with base:36 */
#define MKEX_END_SIGN  0xdeadbeef

#define NPC_CN20K_BYTESM GENMASK_ULL(18, 16)
#define NPC_CN20K_PARSE_NIBBLE GENMASK_ULL(22, 0)
#define NPC_CN20K_TOTAL_NIBBLE 23

#define CN20K_SET_EXTR_LT(intf, extr, ltype, cfg)	\
	rvu_write64(rvu, BLKADDR_NPC,	\
		    NPC_AF_INTFX_EXTRACTORX_LTX_CFG(intf, extr, ltype), cfg)

#define CN20K_GET_KEX_CFG(intf)	\
	rvu_read64(rvu, BLKADDR_NPC, NPC_AF_INTFX_KEX_CFG(intf))

#define CN20K_GET_EXTR_LID(intf, extr)	\
	rvu_read64(rvu, BLKADDR_NPC,	\
		   NPC_AF_INTFX_EXTRACTORX_CFG(intf, extr))

#define CN20K_SET_EXTR_LT(intf, extr, ltype, cfg)	\
	rvu_write64(rvu, BLKADDR_NPC,	\
		    NPC_AF_INTFX_EXTRACTORX_LTX_CFG(intf, extr, ltype), cfg)

#define CN20K_GET_EXTR_LT(intf, extr, ltype)	\
	rvu_read64(rvu, BLKADDR_NPC,	\
		   NPC_AF_INTFX_EXTRACTORX_LTX_CFG(intf, extr, ltype))

/* NPC_PARSE_KEX_S nibble definitions for each field */
#define NPC_CN20K_PARSE_NIBBLE_CHAN GENMASK_ULL(2, 0)
#define NPC_CN20K_PARSE_NIBBLE_ERRLEV BIT_ULL(3)
#define NPC_CN20K_PARSE_NIBBLE_ERRCODE GENMASK_ULL(5, 4)
#define NPC_CN20K_PARSE_NIBBLE_L2L3_BCAST BIT_ULL(6)
#define NPC_CN20K_PARSE_NIBBLE_LA_FLAGS BIT_ULL(7)
#define NPC_CN20K_PARSE_NIBBLE_LA_LTYPE BIT_ULL(8)
#define NPC_CN20K_PARSE_NIBBLE_LB_FLAGS BIT_ULL(9)
#define NPC_CN20K_PARSE_NIBBLE_LB_LTYPE BIT_ULL(10)
#define NPC_CN20K_PARSE_NIBBLE_LC_FLAGS BIT_ULL(11)
#define NPC_CN20K_PARSE_NIBBLE_LC_LTYPE BIT_ULL(12)
#define NPC_CN20K_PARSE_NIBBLE_LD_FLAGS BIT_ULL(13)
#define NPC_CN20K_PARSE_NIBBLE_LD_LTYPE BIT_ULL(14)
#define NPC_CN20K_PARSE_NIBBLE_LE_FLAGS BIT_ULL(15)
#define NPC_CN20K_PARSE_NIBBLE_LE_LTYPE BIT_ULL(16)
#define NPC_CN20K_PARSE_NIBBLE_LF_FLAGS BIT_ULL(17)
#define NPC_CN20K_PARSE_NIBBLE_LF_LTYPE BIT_ULL(18)
#define NPC_CN20K_PARSE_NIBBLE_LG_FLAGS BIT_ULL(19)
#define NPC_CN20K_PARSE_NIBBLE_LG_LTYPE BIT_ULL(20)
#define NPC_CN20K_PARSE_NIBBLE_LH_FLAGS BIT_ULL(21)
#define NPC_CN20K_PARSE_NIBBLE_LH_LTYPE BIT_ULL(22)

/* Rx parse key extract nibble enable */
#define NPC_CN20K_PARSE_NIBBLE_INTF_RX  (NPC_CN20K_PARSE_NIBBLE_CHAN | \
					 NPC_CN20K_PARSE_NIBBLE_L2L3_BCAST | \
					 NPC_CN20K_PARSE_NIBBLE_LA_LTYPE | \
					 NPC_CN20K_PARSE_NIBBLE_LB_LTYPE | \
					 NPC_CN20K_PARSE_NIBBLE_LC_FLAGS | \
					 NPC_CN20K_PARSE_NIBBLE_LC_LTYPE | \
					 NPC_CN20K_PARSE_NIBBLE_LD_LTYPE | \
					 NPC_CN20K_PARSE_NIBBLE_LE_LTYPE)

/* Tx parse key extract nibble enable */
#define NPC_CN20K_PARSE_NIBBLE_INTF_TX	(NPC_CN20K_PARSE_NIBBLE_LA_LTYPE | \
					 NPC_CN20K_PARSE_NIBBLE_LB_LTYPE | \
					 NPC_CN20K_PARSE_NIBBLE_LC_LTYPE | \
					 NPC_CN20K_PARSE_NIBBLE_LD_LTYPE | \
					 NPC_CN20K_PARSE_NIBBLE_LE_LTYPE)

/**
 * enum npc_subbank_flag - NPC subbank status
 *
 * subbank flag indicates whether the subbank is free
 * or used.
 *
 * @NPC_SUBBANK_FLAG_UNINIT: Subbank is not initialized.
 * @NPC_SUBBANK_FLAG_FREE: Subbank is free.
 * @NPC_SUBBANK_FLAG_USED: Subbank is used.
 */
enum npc_subbank_flag {
	NPC_SUBBANK_FLAG_UNINIT,
	NPC_SUBBANK_FLAG_FREE = BIT(0),
	NPC_SUBBANK_FLAG_USED = BIT(1),
};

/**
 * struct npc_subbank - Subbank fields.
 * @b0b:	Subbanks bottom index for bank0
 * @b1b:	Subbanks bottom index for bank1
 * @b0t:	Subbanks top index for bank0
 * @b1t:	Subbanks top index for bank1
 * @flags:	Subbank flags
 * @lock:	Mutex lock for flags and rsrc mofiication
 * @b0map:	Bitmap map for bank0 indexes
 * @b1map:	Bitmap map for bank1 indexes
 * @idx:	Subbank index
 * @arr_idx:	Index to the free array or used array
 * @free_cnt:	Number of free slots in the subbank.
 * @key_type:	X4 or X2 subbank.
 *
 * MCAM resource is divided horizontally into multiple subbanks and
 * Resource allocation from each subbank is managed by this data
 * structure.
 */
struct npc_subbank {
	u16 b0t, b0b, b1t, b1b;
	enum npc_subbank_flag flags;
	struct mutex lock;	/* Protect subbank resources */
	DECLARE_BITMAP(b0map, MAX_SUBBANK_DEPTH);
	DECLARE_BITMAP(b1map, MAX_SUBBANK_DEPTH);
	u16 idx;
	u16 arr_idx;
	u16 free_cnt;
	u8 key_type;
};

/**
 * struct npc_priv_t - NPC private structure.
 * @bank_depth:		Total entries in each bank.
 * @num_banks:		Number of banks.
 * @num_subbanks:	Number of subbanks.
 * @subbank_depth:	Depth of subbank.
 * @kw:			Kex configured key type.
 * @sb:			Subbank array.
 * @xa_sb_used:		Array of used subbanks.
 * @xa_sb_free:		Array of free subbanks.
 * @xa_pf2idx_map:	PF to mcam index map.
 * @xa_idx2pf_map:	Mcam index to PF map.
 * @xa_pf_map:		Pcifunc to index map.
 * @pf_cnt:		Number of PFs.
 * @init_done:		Indicates MCAM initialization is done.
 *
 * This structure is populated during probing time by reading
 * HW csr registers.
 */
struct npc_priv_t {
	int bank_depth;
	const int num_banks;
	int num_subbanks;
	int subbank_depth;
	u8 kw;
	struct npc_subbank *sb;
	struct xarray xa_sb_used;
	struct xarray xa_sb_free;
	struct xarray *xa_pf2idx_map;
	struct xarray xa_idx2pf_map;
	struct xarray xa_pf_map;
	int pf_cnt;
	bool init_done;
};

struct npc_kpm_action0 {
#if defined(__BIG_ENDIAN_BITFIELD)
	u64 rsvd_63_57     : 7;
	u64 byp_count      : 3;
	u64 capture_ena    : 1;
	u64 parse_done     : 1;
	u64 next_state     : 8;
	u64 rsvd_43        : 1;
	u64 capture_lid    : 3;
	u64 capture_ltype  : 4;
	u64 rsvd_32_35     : 4;
	u64 capture_flags  : 4;
	u64 ptr_advance    : 8;
	u64 var_len_offset : 8;
	u64 var_len_mask   : 8;
	u64 var_len_right  : 1;
	u64 var_len_shift  : 3;
#else
	u64 var_len_shift  : 3;
	u64 var_len_right  : 1;
	u64 var_len_mask   : 8;
	u64 var_len_offset : 8;
	u64 ptr_advance    : 8;
	u64 capture_flags  : 4;
	u64 rsvd_32_35     : 4;
	u64 capture_ltype  : 4;
	u64 capture_lid    : 3;
	u64 rsvd_43        : 1;
	u64 next_state     : 8;
	u64 parse_done     : 1;
	u64 capture_ena    : 1;
	u64 byp_count      : 3;
	u64 rsvd_63_57     : 7;
#endif
};

struct npc_mcam_kex_extr {
	/* MKEX Profle Header */
	u64 mkex_sign; /* "mcam-kex-profile" (8 bytes/ASCII characters) */
	u8 name[MKEX_NAME_LEN];   /* MKEX Profile name */
	u64 cpu_model;   /* Format as profiled by CPU hardware */
	u64 kpu_version; /* KPU firmware/profile version */
	u64 reserved; /* Reserved for extension */

	/* MKEX Profle Data */
	u64 keyx_cfg[NPC_MAX_INTF]; /* NPC_AF_INTF(0..1)_KEX_CFG */
#define NPC_MAX_EXTRACTOR	24
	/* MKEX Extractor data */
	u64 intf_extr_lid[NPC_MAX_INTF][NPC_MAX_EXTRACTOR];
	/* KEX configuration per extractor */
	u64 intf_extr_lt[NPC_MAX_INTF][NPC_MAX_EXTRACTOR][NPC_MAX_LT];
} __packed;

struct npc_cn20k_kpu_profile_fwdata {
#define KPU_SIGN	0x00666f727075706b
#define KPU_NAME_LEN	32
	/* Maximum number of custom KPU entries supported by
	 * the built-in profile.
	 */
#define KPU_CN20K_MAX_CST_ENT	6
	/* KPU Profle Header */
	__le64	signature; /* "kpuprof\0" (8 bytes/ASCII characters) */
	u8	name[KPU_NAME_LEN]; /* KPU Profile name */
	__le64	version; /* KPU profile version */
	u8	kpus;
	u8	reserved[7];

	/* Default MKEX profile to be used with this KPU profile. May be
	 * overridden with mkex_profile module parameter.
	 * Format is same as for the MKEX profile to streamline processing.
	 */
	struct npc_mcam_kex_extr	mkex;
	/* LTYPE values for specific HW offloaded protocols. */
	struct npc_lt_def_cfg		lt_def;
	/* Dynamically sized data:
	 *  Custom KPU CAM and ACTION configuration entries.
	 * struct npc_kpu_fwdata kpu[kpus];
	 */
	u8	data[];
} __packed;

struct rvu;

struct npc_priv_t *npc_priv_get(void);
int npc_cn20k_init(struct rvu *rvu);
void npc_cn20k_deinit(struct rvu *rvu);

void npc_cn20k_subbank_calc_free(struct rvu *rvu, int *x2_free,
				 int *x4_free, int *sb_free);

int npc_cn20k_ref_idx_alloc(struct rvu *rvu, int pcifunc, int key_type,
			    int prio, u16 *mcam_idx, int ref, int limit,
			    bool contig, int count);
int npc_cn20k_idx_free(struct rvu *rvu, u16 *mcam_idx, int count);
void npc_cn20k_parser_profile_init(struct rvu *rvu, int blkaddr);
struct npc_mcam_kex_extr *npc_mkex_extr_default_get(void);
void npc_cn20k_load_mkex_profile(struct rvu *rvu, int blkaddr,
				 const char *mkex_profile);
int npc_cn20k_apply_custom_kpu(struct rvu *rvu,
			       struct npc_kpu_profile_adapter *profile);

void
npc_cn20k_update_action_entries_n_flags(struct rvu *rvu,
					struct npc_kpu_profile_adapter *pfl);
#endif /* NPC_CN20K_H */
