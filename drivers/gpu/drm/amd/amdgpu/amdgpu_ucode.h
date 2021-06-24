/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#ifndef __AMDGPU_UCODE_H__
#define __AMDGPU_UCODE_H__

#include "amdgpu_socbb.h"

struct common_firmware_header {
	uint32_t size_bytes; /* size of the entire header+image(s) in bytes */
	uint32_t header_size_bytes; /* size of just the header in bytes */
	uint16_t header_version_major; /* header version */
	uint16_t header_version_minor; /* header version */
	uint16_t ip_version_major; /* IP version */
	uint16_t ip_version_minor; /* IP version */
	uint32_t ucode_version;
	uint32_t ucode_size_bytes; /* size of ucode in bytes */
	uint32_t ucode_array_offset_bytes; /* payload offset from the start of the header */
	uint32_t crc32;  /* crc32 checksum of the payload */
};

/* version_major=1, version_minor=0 */
struct mc_firmware_header_v1_0 {
	struct common_firmware_header header;
	uint32_t io_debug_size_bytes; /* size of debug array in dwords */
	uint32_t io_debug_array_offset_bytes; /* payload offset from the start of the header */
};

/* version_major=1, version_minor=0 */
struct smc_firmware_header_v1_0 {
	struct common_firmware_header header;
	uint32_t ucode_start_addr;
};

/* version_major=2, version_minor=0 */
struct smc_firmware_header_v2_0 {
	struct smc_firmware_header_v1_0 v1_0;
	uint32_t ppt_offset_bytes; /* soft pptable offset */
	uint32_t ppt_size_bytes; /* soft pptable size */
};

struct smc_soft_pptable_entry {
        uint32_t id;
        uint32_t ppt_offset_bytes;
        uint32_t ppt_size_bytes;
};

/* version_major=2, version_minor=1 */
struct smc_firmware_header_v2_1 {
        struct smc_firmware_header_v1_0 v1_0;
        uint32_t pptable_count;
        uint32_t pptable_entry_offset;
};

struct psp_fw_bin_desc {
	uint32_t fw_version;
	uint32_t offset_bytes;
	uint32_t size_bytes;
};

/* version_major=1, version_minor=0 */
struct psp_firmware_header_v1_0 {
	struct common_firmware_header header;
	struct psp_fw_bin_desc sos;
};

/* version_major=1, version_minor=1 */
struct psp_firmware_header_v1_1 {
	struct psp_firmware_header_v1_0 v1_0;
	struct psp_fw_bin_desc toc;
	struct psp_fw_bin_desc kdb;
};

/* version_major=1, version_minor=2 */
struct psp_firmware_header_v1_2 {
	struct psp_firmware_header_v1_0 v1_0;
	struct psp_fw_bin_desc res;
	struct psp_fw_bin_desc kdb;
};

/* version_major=1, version_minor=3 */
struct psp_firmware_header_v1_3 {
	struct psp_firmware_header_v1_1 v1_1;
	struct psp_fw_bin_desc spl;
	struct psp_fw_bin_desc rl;
	struct psp_fw_bin_desc sys_drv_aux;
	struct psp_fw_bin_desc sos_aux;
};

/* version_major=1, version_minor=0 */
struct ta_firmware_header_v1_0 {
	struct common_firmware_header header;
	uint32_t ta_xgmi_ucode_version;
	uint32_t ta_xgmi_offset_bytes;
	uint32_t ta_xgmi_size_bytes;
	uint32_t ta_ras_ucode_version;
	uint32_t ta_ras_offset_bytes;
	uint32_t ta_ras_size_bytes;
	uint32_t ta_hdcp_ucode_version;
	uint32_t ta_hdcp_offset_bytes;
	uint32_t ta_hdcp_size_bytes;
	uint32_t ta_dtm_ucode_version;
	uint32_t ta_dtm_offset_bytes;
	uint32_t ta_dtm_size_bytes;
	uint32_t ta_securedisplay_ucode_version;
	uint32_t ta_securedisplay_offset_bytes;
	uint32_t ta_securedisplay_size_bytes;
};

enum ta_fw_type {
	TA_FW_TYPE_UNKOWN,
	TA_FW_TYPE_PSP_ASD,
	TA_FW_TYPE_PSP_XGMI,
	TA_FW_TYPE_PSP_RAS,
	TA_FW_TYPE_PSP_HDCP,
	TA_FW_TYPE_PSP_DTM,
	TA_FW_TYPE_PSP_RAP,
	TA_FW_TYPE_PSP_SECUREDISPLAY,
	TA_FW_TYPE_MAX_INDEX,
};

struct ta_fw_bin_desc {
	uint32_t fw_type;
	uint32_t fw_version;
	uint32_t offset_bytes;
	uint32_t size_bytes;
};

/* version_major=2, version_minor=0 */
struct ta_firmware_header_v2_0 {
	struct common_firmware_header header;
	uint32_t ta_fw_bin_count;
	struct ta_fw_bin_desc ta_fw_bin[];
};

/* version_major=1, version_minor=0 */
struct gfx_firmware_header_v1_0 {
	struct common_firmware_header header;
	uint32_t ucode_feature_version;
	uint32_t jt_offset; /* jt location */
	uint32_t jt_size;  /* size of jt */
};

/* version_major=1, version_minor=0 */
struct mes_firmware_header_v1_0 {
	struct common_firmware_header header;
	uint32_t mes_ucode_version;
	uint32_t mes_ucode_size_bytes;
	uint32_t mes_ucode_offset_bytes;
	uint32_t mes_ucode_data_version;
	uint32_t mes_ucode_data_size_bytes;
	uint32_t mes_ucode_data_offset_bytes;
	uint32_t mes_uc_start_addr_lo;
	uint32_t mes_uc_start_addr_hi;
	uint32_t mes_data_start_addr_lo;
	uint32_t mes_data_start_addr_hi;
};

/* version_major=1, version_minor=0 */
struct rlc_firmware_header_v1_0 {
	struct common_firmware_header header;
	uint32_t ucode_feature_version;
	uint32_t save_and_restore_offset;
	uint32_t clear_state_descriptor_offset;
	uint32_t avail_scratch_ram_locations;
	uint32_t master_pkt_description_offset;
};

/* version_major=2, version_minor=0 */
struct rlc_firmware_header_v2_0 {
	struct common_firmware_header header;
	uint32_t ucode_feature_version;
	uint32_t jt_offset; /* jt location */
	uint32_t jt_size;  /* size of jt */
	uint32_t save_and_restore_offset;
	uint32_t clear_state_descriptor_offset;
	uint32_t avail_scratch_ram_locations;
	uint32_t reg_restore_list_size;
	uint32_t reg_list_format_start;
	uint32_t reg_list_format_separate_start;
	uint32_t starting_offsets_start;
	uint32_t reg_list_format_size_bytes; /* size of reg list format array in bytes */
	uint32_t reg_list_format_array_offset_bytes; /* payload offset from the start of the header */
	uint32_t reg_list_size_bytes; /* size of reg list array in bytes */
	uint32_t reg_list_array_offset_bytes; /* payload offset from the start of the header */
	uint32_t reg_list_format_separate_size_bytes; /* size of reg list format array in bytes */
	uint32_t reg_list_format_separate_array_offset_bytes; /* payload offset from the start of the header */
	uint32_t reg_list_separate_size_bytes; /* size of reg list array in bytes */
	uint32_t reg_list_separate_array_offset_bytes; /* payload offset from the start of the header */
};

/* version_major=2, version_minor=1 */
struct rlc_firmware_header_v2_1 {
	struct rlc_firmware_header_v2_0 v2_0;
	uint32_t reg_list_format_direct_reg_list_length; /* length of direct reg list format array */
	uint32_t save_restore_list_cntl_ucode_ver;
	uint32_t save_restore_list_cntl_feature_ver;
	uint32_t save_restore_list_cntl_size_bytes;
	uint32_t save_restore_list_cntl_offset_bytes;
	uint32_t save_restore_list_gpm_ucode_ver;
	uint32_t save_restore_list_gpm_feature_ver;
	uint32_t save_restore_list_gpm_size_bytes;
	uint32_t save_restore_list_gpm_offset_bytes;
	uint32_t save_restore_list_srm_ucode_ver;
	uint32_t save_restore_list_srm_feature_ver;
	uint32_t save_restore_list_srm_size_bytes;
	uint32_t save_restore_list_srm_offset_bytes;
};

/* version_major=2, version_minor=1 */
struct rlc_firmware_header_v2_2 {
	struct rlc_firmware_header_v2_1 v2_1;
	uint32_t rlc_iram_ucode_size_bytes;
	uint32_t rlc_iram_ucode_offset_bytes;
	uint32_t rlc_dram_ucode_size_bytes;
	uint32_t rlc_dram_ucode_offset_bytes;
};

/* version_major=1, version_minor=0 */
struct sdma_firmware_header_v1_0 {
	struct common_firmware_header header;
	uint32_t ucode_feature_version;
	uint32_t ucode_change_version;
	uint32_t jt_offset; /* jt location */
	uint32_t jt_size; /* size of jt */
};

/* version_major=1, version_minor=1 */
struct sdma_firmware_header_v1_1 {
	struct sdma_firmware_header_v1_0 v1_0;
	uint32_t digest_size;
};

/* gpu info payload */
struct gpu_info_firmware_v1_0 {
	uint32_t gc_num_se;
	uint32_t gc_num_cu_per_sh;
	uint32_t gc_num_sh_per_se;
	uint32_t gc_num_rb_per_se;
	uint32_t gc_num_tccs;
	uint32_t gc_num_gprs;
	uint32_t gc_num_max_gs_thds;
	uint32_t gc_gs_table_depth;
	uint32_t gc_gsprim_buff_depth;
	uint32_t gc_parameter_cache_depth;
	uint32_t gc_double_offchip_lds_buffer;
	uint32_t gc_wave_size;
	uint32_t gc_max_waves_per_simd;
	uint32_t gc_max_scratch_slots_per_cu;
	uint32_t gc_lds_size;
};

struct gpu_info_firmware_v1_1 {
	struct gpu_info_firmware_v1_0 v1_0;
	uint32_t num_sc_per_sh;
	uint32_t num_packer_per_sc;
};

/* gpu info payload
 * version_major=1, version_minor=1 */
struct gpu_info_firmware_v1_2 {
	struct gpu_info_firmware_v1_1 v1_1;
	struct gpu_info_soc_bounding_box_v1_0 soc_bounding_box;
};

/* version_major=1, version_minor=0 */
struct gpu_info_firmware_header_v1_0 {
	struct common_firmware_header header;
	uint16_t version_major; /* version */
	uint16_t version_minor; /* version */
};

/* version_major=1, version_minor=0 */
struct dmcu_firmware_header_v1_0 {
	struct common_firmware_header header;
	uint32_t intv_offset_bytes; /* interrupt vectors offset from end of header, in bytes */
	uint32_t intv_size_bytes;  /* size of interrupt vectors, in bytes */
};

/* version_major=1, version_minor=0 */
struct dmcub_firmware_header_v1_0 {
	struct common_firmware_header header;
	uint32_t inst_const_bytes; /* size of instruction region, in bytes */
	uint32_t bss_data_bytes; /* size of bss/data region, in bytes */
};

/* header is fixed size */
union amdgpu_firmware_header {
	struct common_firmware_header common;
	struct mc_firmware_header_v1_0 mc;
	struct smc_firmware_header_v1_0 smc;
	struct smc_firmware_header_v2_0 smc_v2_0;
	struct psp_firmware_header_v1_0 psp;
	struct psp_firmware_header_v1_1 psp_v1_1;
	struct psp_firmware_header_v1_3 psp_v1_3;
	struct ta_firmware_header_v1_0 ta;
	struct ta_firmware_header_v2_0 ta_v2_0;
	struct gfx_firmware_header_v1_0 gfx;
	struct rlc_firmware_header_v1_0 rlc;
	struct rlc_firmware_header_v2_0 rlc_v2_0;
	struct rlc_firmware_header_v2_1 rlc_v2_1;
	struct sdma_firmware_header_v1_0 sdma;
	struct sdma_firmware_header_v1_1 sdma_v1_1;
	struct gpu_info_firmware_header_v1_0 gpu_info;
	struct dmcu_firmware_header_v1_0 dmcu;
	struct dmcub_firmware_header_v1_0 dmcub;
	uint8_t raw[0x100];
};

#define UCODE_MAX_TA_PACKAGING ((sizeof(union amdgpu_firmware_header) - sizeof(struct common_firmware_header) - 4) / sizeof(struct ta_fw_bin_desc))

/*
 * fw loading support
 */
enum AMDGPU_UCODE_ID {
	AMDGPU_UCODE_ID_SDMA0 = 0,
	AMDGPU_UCODE_ID_SDMA1,
	AMDGPU_UCODE_ID_SDMA2,
	AMDGPU_UCODE_ID_SDMA3,
	AMDGPU_UCODE_ID_SDMA4,
	AMDGPU_UCODE_ID_SDMA5,
	AMDGPU_UCODE_ID_SDMA6,
	AMDGPU_UCODE_ID_SDMA7,
	AMDGPU_UCODE_ID_CP_CE,
	AMDGPU_UCODE_ID_CP_PFP,
	AMDGPU_UCODE_ID_CP_ME,
	AMDGPU_UCODE_ID_CP_MEC1,
	AMDGPU_UCODE_ID_CP_MEC1_JT,
	AMDGPU_UCODE_ID_CP_MEC2,
	AMDGPU_UCODE_ID_CP_MEC2_JT,
	AMDGPU_UCODE_ID_CP_MES,
	AMDGPU_UCODE_ID_CP_MES_DATA,
	AMDGPU_UCODE_ID_RLC_RESTORE_LIST_CNTL,
	AMDGPU_UCODE_ID_RLC_RESTORE_LIST_GPM_MEM,
	AMDGPU_UCODE_ID_RLC_RESTORE_LIST_SRM_MEM,
	AMDGPU_UCODE_ID_RLC_IRAM,
	AMDGPU_UCODE_ID_RLC_DRAM,
	AMDGPU_UCODE_ID_RLC_G,
	AMDGPU_UCODE_ID_STORAGE,
	AMDGPU_UCODE_ID_SMC,
	AMDGPU_UCODE_ID_UVD,
	AMDGPU_UCODE_ID_UVD1,
	AMDGPU_UCODE_ID_VCE,
	AMDGPU_UCODE_ID_VCN,
	AMDGPU_UCODE_ID_VCN1,
	AMDGPU_UCODE_ID_DMCU_ERAM,
	AMDGPU_UCODE_ID_DMCU_INTV,
	AMDGPU_UCODE_ID_VCN0_RAM,
	AMDGPU_UCODE_ID_VCN1_RAM,
	AMDGPU_UCODE_ID_DMCUB,
	AMDGPU_UCODE_ID_MAXIMUM,
};

/* engine firmware status */
enum AMDGPU_UCODE_STATUS {
	AMDGPU_UCODE_STATUS_INVALID,
	AMDGPU_UCODE_STATUS_NOT_LOADED,
	AMDGPU_UCODE_STATUS_LOADED,
};

enum amdgpu_firmware_load_type {
	AMDGPU_FW_LOAD_DIRECT = 0,
	AMDGPU_FW_LOAD_SMU,
	AMDGPU_FW_LOAD_PSP,
	AMDGPU_FW_LOAD_RLC_BACKDOOR_AUTO,
};

/* conform to smu_ucode_xfer_cz.h */
#define AMDGPU_SDMA0_UCODE_LOADED	0x00000001
#define AMDGPU_SDMA1_UCODE_LOADED	0x00000002
#define AMDGPU_CPCE_UCODE_LOADED	0x00000004
#define AMDGPU_CPPFP_UCODE_LOADED	0x00000008
#define AMDGPU_CPME_UCODE_LOADED	0x00000010
#define AMDGPU_CPMEC1_UCODE_LOADED	0x00000020
#define AMDGPU_CPMEC2_UCODE_LOADED	0x00000040
#define AMDGPU_CPRLC_UCODE_LOADED	0x00000100

/* amdgpu firmware info */
struct amdgpu_firmware_info {
	/* ucode ID */
	enum AMDGPU_UCODE_ID ucode_id;
	/* request_firmware */
	const struct firmware *fw;
	/* starting mc address */
	uint64_t mc_addr;
	/* kernel linear address */
	void *kaddr;
	/* ucode_size_bytes */
	uint32_t ucode_size;
	/* starting tmr mc address */
	uint32_t tmr_mc_addr_lo;
	uint32_t tmr_mc_addr_hi;
};

struct amdgpu_firmware {
	struct amdgpu_firmware_info ucode[AMDGPU_UCODE_ID_MAXIMUM];
	enum amdgpu_firmware_load_type load_type;
	struct amdgpu_bo *fw_buf;
	unsigned int fw_size;
	unsigned int max_ucodes;
	/* firmwares are loaded by psp instead of smu from vega10 */
	const struct amdgpu_psp_funcs *funcs;
	struct amdgpu_bo *rbuf;
	struct mutex mutex;

	/* gpu info firmware data pointer */
	const struct firmware *gpu_info_fw;

	void *fw_buf_ptr;
	uint64_t fw_buf_mc;
};

void amdgpu_ucode_print_mc_hdr(const struct common_firmware_header *hdr);
void amdgpu_ucode_print_smc_hdr(const struct common_firmware_header *hdr);
void amdgpu_ucode_print_gfx_hdr(const struct common_firmware_header *hdr);
void amdgpu_ucode_print_rlc_hdr(const struct common_firmware_header *hdr);
void amdgpu_ucode_print_sdma_hdr(const struct common_firmware_header *hdr);
void amdgpu_ucode_print_psp_hdr(const struct common_firmware_header *hdr);
void amdgpu_ucode_print_gpu_info_hdr(const struct common_firmware_header *hdr);
int amdgpu_ucode_validate(const struct firmware *fw);
bool amdgpu_ucode_hdr_version(union amdgpu_firmware_header *hdr,
				uint16_t hdr_major, uint16_t hdr_minor);

int amdgpu_ucode_init_bo(struct amdgpu_device *adev);
int amdgpu_ucode_create_bo(struct amdgpu_device *adev);
int amdgpu_ucode_sysfs_init(struct amdgpu_device *adev);
void amdgpu_ucode_free_bo(struct amdgpu_device *adev);
void amdgpu_ucode_sysfs_fini(struct amdgpu_device *adev);

enum amdgpu_firmware_load_type
amdgpu_ucode_get_load_type(struct amdgpu_device *adev, int load_type);

const char *amdgpu_ucode_name(enum AMDGPU_UCODE_ID ucode_id);

#endif
