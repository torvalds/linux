/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2024 Intel Corporation */

#ifndef ADF_MSTATE_MGR_H
#define ADF_MSTATE_MGR_H

#define ADF_MSTATE_ID_LEN		8

#define ADF_MSTATE_ETRB_IDS		"ETRBAR"
#define ADF_MSTATE_MISCB_IDS		"MISCBAR"
#define ADF_MSTATE_EXTB_IDS		"EXTBAR"
#define ADF_MSTATE_GEN_IDS		"GENER"
#define ADF_MSTATE_CONFIG_IDS		"CONFIG"
#define ADF_MSTATE_SECTION_NUM		5

#define ADF_MSTATE_BANK_IDX_IDS		"bnk"

#define ADF_MSTATE_ETR_REGS_IDS		"mregs"
#define ADF_MSTATE_VINTSRC_IDS		"visrc"
#define ADF_MSTATE_VINTMSK_IDS		"vimsk"
#define ADF_MSTATE_SLA_IDS		"sla"
#define ADF_MSTATE_IOV_INIT_IDS		"iovinit"
#define ADF_MSTATE_COMPAT_VER_IDS	"compver"
#define ADF_MSTATE_GEN_CAP_IDS		"gencap"
#define ADF_MSTATE_GEN_SVCMAP_IDS	"svcmap"
#define ADF_MSTATE_GEN_EXTDC_IDS	"extdc"
#define ADF_MSTATE_VINTSRC_PF2VM_IDS	"vispv"
#define ADF_MSTATE_VINTMSK_PF2VM_IDS	"vimpv"
#define ADF_MSTATE_VM2PF_IDS		"vm2pf"
#define ADF_MSTATE_PF2VM_IDS		"pf2vm"

struct adf_mstate_mgr {
	u8 *buf;
	u8 *state;
	u32 size;
	u32 n_sects;
};

struct adf_mstate_preh {
	u32 magic;
	u32 version;
	u16 preh_len;
	u16 n_sects;
	u32 size;
};

struct adf_mstate_vreginfo {
	void *addr;
	u32 size;
};

struct adf_mstate_sect_h;

typedef int (*adf_mstate_preamble_checker)(struct adf_mstate_preh *preamble, void *opa);
typedef int (*adf_mstate_populate)(struct adf_mstate_mgr *sub_mgr, u8 *buf,
				   u32 size, void *opa);
typedef int (*adf_mstate_action)(struct adf_mstate_mgr *sub_mgr, u8 *buf, u32 size,
				 void *opa);

struct adf_mstate_mgr *adf_mstate_mgr_new(u8 *buf, u32 size);
void adf_mstate_mgr_destroy(struct adf_mstate_mgr *mgr);
void adf_mstate_mgr_init(struct adf_mstate_mgr *mgr, u8 *buf, u32 size);
void adf_mstate_mgr_init_from_parent(struct adf_mstate_mgr *mgr,
				     struct adf_mstate_mgr *p_mgr);
void adf_mstate_mgr_init_from_psect(struct adf_mstate_mgr *mgr,
				    struct adf_mstate_sect_h *p_sect);
int adf_mstate_mgr_init_from_remote(struct adf_mstate_mgr *mgr,
				    u8 *buf, u32 size,
				    adf_mstate_preamble_checker checker,
				    void *opaque);
struct adf_mstate_preh *adf_mstate_preamble_add(struct adf_mstate_mgr *mgr);
int adf_mstate_preamble_update(struct adf_mstate_mgr *mgr);
u32 adf_mstate_state_size(struct adf_mstate_mgr *mgr);
u32 adf_mstate_state_size_from_remote(struct adf_mstate_mgr *mgr);
void adf_mstate_sect_update(struct adf_mstate_mgr *p_mgr,
			    struct adf_mstate_mgr *curr_mgr,
			    struct adf_mstate_sect_h *sect);
struct adf_mstate_sect_h *adf_mstate_sect_add_vreg(struct adf_mstate_mgr *mgr,
						   const char *id,
						   struct adf_mstate_vreginfo *info);
struct adf_mstate_sect_h *adf_mstate_sect_add(struct adf_mstate_mgr *mgr,
					      const char *id,
					      adf_mstate_populate populate,
					      void *opaque);
struct adf_mstate_sect_h *adf_mstate_sect_lookup(struct adf_mstate_mgr *mgr,
						 const char *id,
						 adf_mstate_action action,
						 void *opaque);
#endif
