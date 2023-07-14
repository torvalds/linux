#ifndef __NVFW_ACR_H__
#define __NVFW_ACR_H__

struct wpr_header {
#define WPR_HEADER_V0_FALCON_ID_INVALID                              0xffffffff
	u32 falcon_id;
	u32 lsb_offset;
	u32 bootstrap_owner;
	u32 lazy_bootstrap;
#define WPR_HEADER_V0_STATUS_NONE                                             0
#define WPR_HEADER_V0_STATUS_COPY                                             1
#define WPR_HEADER_V0_STATUS_VALIDATION_CODE_FAILED                           2
#define WPR_HEADER_V0_STATUS_VALIDATION_DATA_FAILED                           3
#define WPR_HEADER_V0_STATUS_VALIDATION_DONE                                  4
#define WPR_HEADER_V0_STATUS_VALIDATION_SKIPPED                               5
#define WPR_HEADER_V0_STATUS_BOOTSTRAP_READY                                  6
	u32 status;
};

void wpr_header_dump(struct nvkm_subdev *, const struct wpr_header *);

struct wpr_header_v1 {
#define WPR_HEADER_V1_FALCON_ID_INVALID                              0xffffffff
	u32 falcon_id;
	u32 lsb_offset;
	u32 bootstrap_owner;
	u32 lazy_bootstrap;
	u32 bin_version;
#define WPR_HEADER_V1_STATUS_NONE                                             0
#define WPR_HEADER_V1_STATUS_COPY                                             1
#define WPR_HEADER_V1_STATUS_VALIDATION_CODE_FAILED                           2
#define WPR_HEADER_V1_STATUS_VALIDATION_DATA_FAILED                           3
#define WPR_HEADER_V1_STATUS_VALIDATION_DONE                                  4
#define WPR_HEADER_V1_STATUS_VALIDATION_SKIPPED                               5
#define WPR_HEADER_V1_STATUS_BOOTSTRAP_READY                                  6
#define WPR_HEADER_V1_STATUS_REVOCATION_CHECK_FAILED                          7
	u32 status;
};

void wpr_header_v1_dump(struct nvkm_subdev *, const struct wpr_header_v1 *);

struct wpr_generic_header {
#define WPR_GENERIC_HEADER_ID_LSF_UCODE_DESC     1
#define WPR_GENERIC_HEADER_ID_LSF_WPR_HEADER     2
#define WPR_GENERIC_HEADER_ID_LSF_SHARED_SUB_WPR 3
#define WPR_GENERIC_HEADER_ID_LSF_LSB_HEADER     4
	u16 identifier;
	u16 version;
	u32 size;
};

struct wpr_header_v2 {
	struct wpr_generic_header hdr;
	struct wpr_header_v1 wpr;
};

void wpr_header_v2_dump(struct nvkm_subdev *, const struct wpr_header_v2 *);

struct lsf_signature {
	u8 prd_keys[2][16];
	u8 dbg_keys[2][16];
	u32 b_prd_present;
	u32 b_dbg_present;
	u32 falcon_id;
};

struct lsf_signature_v1 {
	u8 prd_keys[2][16];
	u8 dbg_keys[2][16];
	u32 b_prd_present;
	u32 b_dbg_present;
	u32 falcon_id;
	u32 supports_versioning;
	u32 version;
	u32 depmap_count;
	u8 depmap[11/*LSF_LSB_DEPMAP_SIZE*/ * 2 * 4];
	u8 kdf[16];
};

struct lsb_header_tail {
	u32 ucode_off;
	u32 ucode_size;
	u32 data_size;
	u32 bl_code_size;
	u32 bl_imem_off;
	u32 bl_data_off;
	u32 bl_data_size;
	u32 app_code_off;
	u32 app_code_size;
	u32 app_data_off;
	u32 app_data_size;
	u32 flags;
};

struct lsb_header {
	struct lsf_signature signature;
	struct lsb_header_tail tail;
};

void lsb_header_dump(struct nvkm_subdev *, struct lsb_header *);

struct lsb_header_v1 {
	struct lsf_signature_v1 signature;
	struct lsb_header_tail tail;
};

void lsb_header_v1_dump(struct nvkm_subdev *, struct lsb_header_v1 *);

struct lsb_header_v2 {
	struct wpr_generic_header hdr;
	struct lsf_signature_v2 {
		struct wpr_generic_header hdr;
		u32 falcon_id;
		u8 prd_present;
		u8 dbg_present;
		u16 reserved;
		u32 sig_size;
		u8 prod_sig[2][384 + 128];
		u8 debug_sig[2][384 + 128];
		u16 sig_algo_ver;
		u16 sig_algo;
		u16 hash_algo_ver;
		u16 hash_algo;
		u32 sig_algo_padding_type;
		u8 depmap[11 * 2 * 4];
		u32 depmap_count;
		u8 supports_versioning;
		u8 pad[3];
		u32 ls_ucode_version;
		u32 ls_ucode_id;
		u32 ucode_ls_encrypted;
		u32 ls_eng_algo_type;
		u32 ls_eng_algo_ver;
		u8 ls_enc_iv[16];
		u8 rsvd[36];
	} signature;
	u32 ucode_off;
	u32 ucode_size;
	u32 data_size;
	u32 bl_code_size;
	u32 bl_imem_off;
	u32 bl_data_off;
	u32 bl_data_size;
	u32 rsvd0;
	u32 app_code_off;
	u32 app_code_size;
	u32 app_data_off;
	u32 app_data_size;
	u32 app_imem_offset;
	u32 app_dmem_offset;
	u32 flags;
	u32 monitor_code_offset;
	u32 monitor_data_offset;
	u32 manifest_offset;
	struct hs_fmc_params {
		u8 hs_fmc;
		u8 padding[3];
		u16 pkc_algo;
		u16 pkc_algo_version;
		u32 engid_mask;
		u32 ucode_id;
		u32 fuse_ver;
		u8 pkc_signature[384 + 128];
		u8 pkc_key[2048];
		u8 rsvd[4];
	} hs_fmc_params;
	struct hs_ovl_sig_blob_params {
		u8 hs_ovl_sig_blob_present;
		u32 hs_ovl_sig_blob_offset;
		u32 hs_ovl_sig_blob_size;
	} hs_ovl_sig_blob_params;
	u8 rsvd[20];
};

void lsb_header_v2_dump(struct nvkm_subdev *, struct lsb_header_v2 *);

struct flcn_acr_desc {
	union {
		u8 reserved_dmem[0x200];
		u32 signatures[4];
	} ucode_reserved_space;
	u32 wpr_region_id;
	u32 wpr_offset;
	u32 mmu_mem_range;
	struct {
		u32 no_regions;
		struct {
			u32 start_addr;
			u32 end_addr;
			u32 region_id;
			u32 read_mask;
			u32 write_mask;
			u32 client_mask;
		} region_props[2];
	} regions;
	u32 ucode_blob_size;
	u64 ucode_blob_base __aligned(8);
	struct {
		u32 vpr_enabled;
		u32 vpr_start;
		u32 vpr_end;
		u32 hdcp_policies;
	} vpr_desc;
};

void flcn_acr_desc_dump(struct nvkm_subdev *, struct flcn_acr_desc *);

struct flcn_acr_desc_v1 {
	u8 reserved_dmem[0x200];
	u32 signatures[4];
	u32 wpr_region_id;
	u32 wpr_offset;
	u32 mmu_memory_range;
	struct {
		u32 no_regions;
		struct {
			u32 start_addr;
			u32 end_addr;
			u32 region_id;
			u32 read_mask;
			u32 write_mask;
			u32 client_mask;
			u32 shadow_mem_start_addr;
		} region_props[2];
	} regions;
	u32 ucode_blob_size;
	u64 ucode_blob_base __aligned(8);
	struct {
		u32 vpr_enabled;
		u32 vpr_start;
		u32 vpr_end;
		u32 hdcp_policies;
	} vpr_desc;
};

void flcn_acr_desc_v1_dump(struct nvkm_subdev *, struct flcn_acr_desc_v1 *);
#endif
