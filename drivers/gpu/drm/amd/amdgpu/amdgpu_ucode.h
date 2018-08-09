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

/* version_major=1, version_minor=0 */
struct psp_firmware_header_v1_0 {
	struct common_firmware_header header;
	uint32_t ucode_feature_version;
	uint32_t sos_offset_bytes;
	uint32_t sos_size_bytes;
};

/* version_major=1, version_minor=0 */
struct gfx_firmware_header_v1_0 {
	struct common_firmware_header header;
	uint32_t ucode_feature_version;
	uint32_t jt_offset; /* jt location */
	uint32_t jt_size;  /* size of jt */
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

/* version_major=1, version_minor=0 */
struct gpu_info_firmware_header_v1_0 {
	struct common_firmware_header header;
	uint16_t version_major; /* version */
	uint16_t version_minor; /* version */
};

/* header is fixed size */
union amdgpu_firmware_header {
	struct common_firmware_header common;
	struct mc_firmware_header_v1_0 mc;
	struct smc_firmware_header_v1_0 smc;
	struct psp_firmware_header_v1_0 psp;
	struct gfx_firmware_header_v1_0 gfx;
	struct rlc_firmware_header_v1_0 rlc;
	struct rlc_firmware_header_v2_0 rlc_v2_0;
	struct rlc_firmware_header_v2_1 rlc_v2_1;
	struct sdma_firmware_header_v1_0 sdma;
	struct sdma_firmware_header_v1_1 sdma_v1_1;
	struct gpu_info_firmware_header_v1_0 gpu_info;
	uint8_t raw[0x100];
};

/*
 * fw loading support
 */
enum AMDGPU_UCODE_ID {
	AMDGPU_UCODE_ID_SDMA0 = 0,
	AMDGPU_UCODE_ID_SDMA1,
	AMDGPU_UCODE_ID_CP_CE,
	AMDGPU_UCODE_ID_CP_PFP,
	AMDGPU_UCODE_ID_CP_ME,
	AMDGPU_UCODE_ID_CP_MEC1,
	AMDGPU_UCODE_ID_CP_MEC1_JT,
	AMDGPU_UCODE_ID_CP_MEC2,
	AMDGPU_UCODE_ID_CP_MEC2_JT,
	AMDGPU_UCODE_ID_RLC_G,
	AMDGPU_UCODE_ID_RLC_RESTORE_LIST_CNTL,
	AMDGPU_UCODE_ID_RLC_RESTORE_LIST_GPM_MEM,
	AMDGPU_UCODE_ID_RLC_RESTORE_LIST_SRM_MEM,
	AMDGPU_UCODE_ID_STORAGE,
	AMDGPU_UCODE_ID_SMC,
	AMDGPU_UCODE_ID_UVD,
	AMDGPU_UCODE_ID_VCE,
	AMDGPU_UCODE_ID_MAXIMUM,
};

/* engine firmware status */
enum AMDGPU_UCODE_STATUS {
	AMDGPU_UCODE_STATUS_INVALID,
	AMDGPU_UCODE_STATUS_NOT_LOADED,
	AMDGPU_UCODE_STATUS_LOADED,
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

void amdgpu_ucode_print_mc_hdr(const struct common_firmware_header *hdr);
void amdgpu_ucode_print_smc_hdr(const struct common_firmware_header *hdr);
void amdgpu_ucode_print_gfx_hdr(const struct common_firmware_header *hdr);
void amdgpu_ucode_print_rlc_hdr(const struct common_firmware_header *hdr);
void amdgpu_ucode_print_sdma_hdr(const struct common_firmware_header *hdr);
void amdgpu_ucode_print_gpu_info_hdr(const struct common_firmware_header *hdr);
int amdgpu_ucode_validate(const struct firmware *fw);
bool amdgpu_ucode_hdr_version(union amdgpu_firmware_header *hdr,
				uint16_t hdr_major, uint16_t hdr_minor);
int amdgpu_ucode_init_bo(struct amdgpu_device *adev);
int amdgpu_ucode_fini_bo(struct amdgpu_device *adev);

enum amdgpu_firmware_load_type
amdgpu_ucode_get_load_type(struct amdgpu_device *adev, int load_type);

#endif
