/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 * Author: Monk.liu@amd.com
 */
#ifndef AMDGPU_VIRT_H
#define AMDGPU_VIRT_H

#include "amdgv_sriovmsg.h"

#define AMDGPU_SRIOV_CAPS_SRIOV_VBIOS  (1 << 0) /* vBIOS is sr-iov ready */
#define AMDGPU_SRIOV_CAPS_ENABLE_IOV   (1 << 1) /* sr-iov is enabled on this GPU */
#define AMDGPU_SRIOV_CAPS_IS_VF        (1 << 2) /* this GPU is a virtual function */
#define AMDGPU_PASSTHROUGH_MODE        (1 << 3) /* thw whole GPU is pass through for VM */
#define AMDGPU_SRIOV_CAPS_RUNTIME      (1 << 4) /* is out of full access mode */
#define AMDGPU_VF_MMIO_ACCESS_PROTECT  (1 << 5) /* MMIO write access is not allowed in sriov runtime */

/* flags for indirect register access path supported by rlcg for sriov */
#define AMDGPU_RLCG_GC_WRITE_LEGACY    (0x8 << 28)
#define AMDGPU_RLCG_GC_WRITE           (0x0 << 28)
#define AMDGPU_RLCG_GC_READ            (0x1 << 28)
#define AMDGPU_RLCG_MMHUB_WRITE        (0x2 << 28)

/* error code for indirect register access path supported by rlcg for sriov */
#define AMDGPU_RLCG_VFGATE_DISABLED		0x4000000
#define AMDGPU_RLCG_WRONG_OPERATION_TYPE	0x2000000
#define AMDGPU_RLCG_REG_NOT_IN_RANGE		0x1000000

#define AMDGPU_RLCG_SCRATCH1_ADDRESS_MASK	0xFFFFF
#define AMDGPU_RLCG_SCRATCH1_ERROR_MASK	0xF000000

/* all asic after AI use this offset */
#define mmRCC_IOV_FUNC_IDENTIFIER 0xDE5
/* tonga/fiji use this offset */
#define mmBIF_IOV_FUNC_IDENTIFIER 0x1503

#define AMDGPU_VF2PF_UPDATE_MAX_RETRY_LIMIT 5

enum amdgpu_sriov_vf_mode {
	SRIOV_VF_MODE_BARE_METAL = 0,
	SRIOV_VF_MODE_ONE_VF,
	SRIOV_VF_MODE_MULTI_VF,
};

struct amdgpu_mm_table {
	struct amdgpu_bo	*bo;
	uint32_t		*cpu_addr;
	uint64_t		gpu_addr;
};

#define AMDGPU_VF_ERROR_ENTRY_SIZE    16

/* struct error_entry - amdgpu VF error information. */
struct amdgpu_vf_error_buffer {
	struct mutex lock;
	int read_count;
	int write_count;
	uint16_t code[AMDGPU_VF_ERROR_ENTRY_SIZE];
	uint16_t flags[AMDGPU_VF_ERROR_ENTRY_SIZE];
	uint64_t data[AMDGPU_VF_ERROR_ENTRY_SIZE];
};

enum idh_request;

/**
 * struct amdgpu_virt_ops - amdgpu device virt operations
 */
struct amdgpu_virt_ops {
	int (*req_full_gpu)(struct amdgpu_device *adev, bool init);
	int (*rel_full_gpu)(struct amdgpu_device *adev, bool init);
	int (*req_init_data)(struct amdgpu_device *adev);
	int (*reset_gpu)(struct amdgpu_device *adev);
	int (*wait_reset)(struct amdgpu_device *adev);
	void (*trans_msg)(struct amdgpu_device *adev, enum idh_request req,
			  u32 data1, u32 data2, u32 data3);
	void (*ras_poison_handler)(struct amdgpu_device *adev,
					enum amdgpu_ras_block block);
};

/*
 * Firmware Reserve Frame buffer
 */
struct amdgpu_virt_fw_reserve {
	struct amd_sriov_msg_pf2vf_info_header *p_pf2vf;
	struct amd_sriov_msg_vf2pf_info_header *p_vf2pf;
	unsigned int checksum_key;
};

/*
 * Legacy GIM header
 *
 * Defination between PF and VF
 * Structures forcibly aligned to 4 to keep the same style as PF.
 */
#define AMDGIM_DATAEXCHANGE_OFFSET		(64 * 1024)

#define AMDGIM_GET_STRUCTURE_RESERVED_SIZE(total, u8, u16, u32, u64) \
		(total - (((u8)+3) / 4 + ((u16)+1) / 2 + (u32) + (u64)*2))

enum AMDGIM_FEATURE_FLAG {
	/* GIM supports feature of Error log collecting */
	AMDGIM_FEATURE_ERROR_LOG_COLLECT = 0x1,
	/* GIM supports feature of loading uCodes */
	AMDGIM_FEATURE_GIM_LOAD_UCODES   = 0x2,
	/* VRAM LOST by GIM */
	AMDGIM_FEATURE_GIM_FLR_VRAMLOST = 0x4,
	/* MM bandwidth */
	AMDGIM_FEATURE_GIM_MM_BW_MGR = 0x8,
	/* PP ONE VF MODE in GIM */
	AMDGIM_FEATURE_PP_ONE_VF = (1 << 4),
	/* Indirect Reg Access enabled */
	AMDGIM_FEATURE_INDIRECT_REG_ACCESS = (1 << 5),
	/* AV1 Support MODE*/
	AMDGIM_FEATURE_AV1_SUPPORT = (1 << 6),
	/* VCN RB decouple */
	AMDGIM_FEATURE_VCN_RB_DECOUPLE = (1 << 7),
	/* MES info */
	AMDGIM_FEATURE_MES_INFO_ENABLE = (1 << 8),
};

enum AMDGIM_REG_ACCESS_FLAG {
	/* Use PSP to program IH_RB_CNTL */
	AMDGIM_FEATURE_IH_REG_PSP_EN     = (1 << 0),
	/* Use RLC to program MMHUB regs */
	AMDGIM_FEATURE_MMHUB_REG_RLC_EN  = (1 << 1),
	/* Use RLC to program GC regs */
	AMDGIM_FEATURE_GC_REG_RLC_EN     = (1 << 2),
};

struct amdgim_pf2vf_info_v1 {
	/* header contains size and version */
	struct amd_sriov_msg_pf2vf_info_header header;
	/* max_width * max_height */
	unsigned int uvd_enc_max_pixels_count;
	/* 16x16 pixels/sec, codec independent */
	unsigned int uvd_enc_max_bandwidth;
	/* max_width * max_height */
	unsigned int vce_enc_max_pixels_count;
	/* 16x16 pixels/sec, codec independent */
	unsigned int vce_enc_max_bandwidth;
	/* MEC FW position in kb from the start of visible frame buffer */
	unsigned int mecfw_kboffset;
	/* The features flags of the GIM driver supports. */
	unsigned int feature_flags;
	/* use private key from mailbox 2 to create chueksum */
	unsigned int checksum;
} __aligned(4);

struct amdgim_vf2pf_info_v1 {
	/* header contains size and version */
	struct amd_sriov_msg_vf2pf_info_header header;
	/* driver version */
	char driver_version[64];
	/* driver certification, 1=WHQL, 0=None */
	unsigned int driver_cert;
	/* guest OS type and version: need a define */
	unsigned int os_info;
	/* in the unit of 1M */
	unsigned int fb_usage;
	/* guest gfx engine usage percentage */
	unsigned int gfx_usage;
	/* guest gfx engine health percentage */
	unsigned int gfx_health;
	/* guest compute engine usage percentage */
	unsigned int compute_usage;
	/* guest compute engine health percentage */
	unsigned int compute_health;
	/* guest vce engine usage percentage. 0xffff means N/A. */
	unsigned int vce_enc_usage;
	/* guest vce engine health percentage. 0xffff means N/A. */
	unsigned int vce_enc_health;
	/* guest uvd engine usage percentage. 0xffff means N/A. */
	unsigned int uvd_enc_usage;
	/* guest uvd engine usage percentage. 0xffff means N/A. */
	unsigned int uvd_enc_health;
	unsigned int checksum;
} __aligned(4);

struct amdgim_vf2pf_info_v2 {
	/* header contains size and version */
	struct amd_sriov_msg_vf2pf_info_header header;
	uint32_t checksum;
	/* driver version */
	uint8_t driver_version[64];
	/* driver certification, 1=WHQL, 0=None */
	uint32_t driver_cert;
	/* guest OS type and version: need a define */
	uint32_t os_info;
	/* in the unit of 1M */
	uint32_t fb_usage;
	/* guest gfx engine usage percentage */
	uint32_t gfx_usage;
	/* guest gfx engine health percentage */
	uint32_t gfx_health;
	/* guest compute engine usage percentage */
	uint32_t compute_usage;
	/* guest compute engine health percentage */
	uint32_t compute_health;
	/* guest vce engine usage percentage. 0xffff means N/A. */
	uint32_t vce_enc_usage;
	/* guest vce engine health percentage. 0xffff means N/A. */
	uint32_t vce_enc_health;
	/* guest uvd engine usage percentage. 0xffff means N/A. */
	uint32_t uvd_enc_usage;
	/* guest uvd engine usage percentage. 0xffff means N/A. */
	uint32_t uvd_enc_health;
	uint32_t reserved[AMDGIM_GET_STRUCTURE_RESERVED_SIZE(256, 64, 0, (12 + sizeof(struct amd_sriov_msg_vf2pf_info_header)/sizeof(uint32_t)), 0)];
} __aligned(4);

struct amdgpu_virt_ras_err_handler_data {
	/* point to bad page records array */
	struct eeprom_table_record *bps;
	/* point to reserved bo array */
	struct amdgpu_bo **bps_bo;
	/* the count of entries */
	int count;
	/* last reserved entry's index + 1 */
	int last_reserved;
};

/* GPU virtualization */
struct amdgpu_virt {
	uint32_t			caps;
	struct amdgpu_bo		*csa_obj;
	void				*csa_cpu_addr;
	bool chained_ib_support;
	uint32_t			reg_val_offs;
	struct amdgpu_irq_src		ack_irq;
	struct amdgpu_irq_src		rcv_irq;
	struct work_struct		flr_work;
	struct amdgpu_mm_table		mm_table;
	const struct amdgpu_virt_ops	*ops;
	struct amdgpu_vf_error_buffer	vf_errors;
	struct amdgpu_virt_fw_reserve	fw_reserve;
	uint32_t gim_feature;
	uint32_t reg_access_mode;
	int req_init_data_ver;
	bool tdr_debug;
	struct amdgpu_virt_ras_err_handler_data *virt_eh_data;
	bool ras_init_done;
	uint32_t reg_access;

	/* vf2pf message */
	struct delayed_work vf2pf_work;
	uint32_t vf2pf_update_interval_ms;
	int vf2pf_update_retry_cnt;

	/* multimedia bandwidth config */
	bool     is_mm_bw_enabled;
	uint32_t decode_max_dimension_pixels;
	uint32_t decode_max_frame_pixels;
	uint32_t encode_max_dimension_pixels;
	uint32_t encode_max_frame_pixels;

	/* the ucode id to signal the autoload */
	uint32_t autoload_ucode_id;

	struct mutex rlcg_reg_lock;
};

struct amdgpu_video_codec_info;

#define amdgpu_sriov_enabled(adev) \
((adev)->virt.caps & AMDGPU_SRIOV_CAPS_ENABLE_IOV)

#define amdgpu_sriov_vf(adev) \
((adev)->virt.caps & AMDGPU_SRIOV_CAPS_IS_VF)

#define amdgpu_sriov_bios(adev) \
((adev)->virt.caps & AMDGPU_SRIOV_CAPS_SRIOV_VBIOS)

#define amdgpu_sriov_runtime(adev) \
((adev)->virt.caps & AMDGPU_SRIOV_CAPS_RUNTIME)

#define amdgpu_sriov_fullaccess(adev) \
(amdgpu_sriov_vf((adev)) && !amdgpu_sriov_runtime((adev)))

#define amdgpu_sriov_reg_indirect_en(adev) \
(amdgpu_sriov_vf((adev)) && \
	((adev)->virt.gim_feature & (AMDGIM_FEATURE_INDIRECT_REG_ACCESS)))

#define amdgpu_sriov_reg_indirect_ih(adev) \
(amdgpu_sriov_vf((adev)) && \
	((adev)->virt.reg_access & (AMDGIM_FEATURE_IH_REG_PSP_EN)))

#define amdgpu_sriov_reg_indirect_mmhub(adev) \
(amdgpu_sriov_vf((adev)) && \
	((adev)->virt.reg_access & (AMDGIM_FEATURE_MMHUB_REG_RLC_EN)))

#define amdgpu_sriov_reg_indirect_gc(adev) \
(amdgpu_sriov_vf((adev)) && \
	((adev)->virt.reg_access & (AMDGIM_FEATURE_GC_REG_RLC_EN)))

#define amdgpu_sriov_rlcg_error_report_enabled(adev) \
        (amdgpu_sriov_reg_indirect_mmhub(adev) || amdgpu_sriov_reg_indirect_gc(adev))

#define amdgpu_passthrough(adev) \
((adev)->virt.caps & AMDGPU_PASSTHROUGH_MODE)

#define amdgpu_sriov_vf_mmio_access_protection(adev) \
((adev)->virt.caps & AMDGPU_VF_MMIO_ACCESS_PROTECT)

static inline bool is_virtual_machine(void)
{
#if defined(CONFIG_X86)
	return boot_cpu_has(X86_FEATURE_HYPERVISOR);
#elif defined(CONFIG_ARM64)
	return !is_kernel_in_hyp_mode();
#else
	return false;
#endif
}

#define amdgpu_sriov_is_pp_one_vf(adev) \
	((adev)->virt.gim_feature & AMDGIM_FEATURE_PP_ONE_VF)
#define amdgpu_sriov_is_debug(adev) \
	((!amdgpu_in_reset(adev)) && adev->virt.tdr_debug)
#define amdgpu_sriov_is_normal(adev) \
	((!amdgpu_in_reset(adev)) && (!adev->virt.tdr_debug))
#define amdgpu_sriov_is_av1_support(adev) \
	((adev)->virt.gim_feature & AMDGIM_FEATURE_AV1_SUPPORT)
#define amdgpu_sriov_is_vcn_rb_decouple(adev) \
	((adev)->virt.gim_feature & AMDGIM_FEATURE_VCN_RB_DECOUPLE)
#define amdgpu_sriov_is_mes_info_enable(adev) \
	((adev)->virt.gim_feature & AMDGIM_FEATURE_MES_INFO_ENABLE)
bool amdgpu_virt_mmio_blocked(struct amdgpu_device *adev);
void amdgpu_virt_init_setting(struct amdgpu_device *adev);
int amdgpu_virt_request_full_gpu(struct amdgpu_device *adev, bool init);
int amdgpu_virt_release_full_gpu(struct amdgpu_device *adev, bool init);
int amdgpu_virt_reset_gpu(struct amdgpu_device *adev);
void amdgpu_virt_request_init_data(struct amdgpu_device *adev);
int amdgpu_virt_wait_reset(struct amdgpu_device *adev);
int amdgpu_virt_alloc_mm_table(struct amdgpu_device *adev);
void amdgpu_virt_free_mm_table(struct amdgpu_device *adev);
void amdgpu_virt_release_ras_err_handler_data(struct amdgpu_device *adev);
void amdgpu_virt_init_data_exchange(struct amdgpu_device *adev);
void amdgpu_virt_exchange_data(struct amdgpu_device *adev);
void amdgpu_virt_fini_data_exchange(struct amdgpu_device *adev);
void amdgpu_detect_virtualization(struct amdgpu_device *adev);

bool amdgpu_virt_can_access_debugfs(struct amdgpu_device *adev);
int amdgpu_virt_enable_access_debugfs(struct amdgpu_device *adev);
void amdgpu_virt_disable_access_debugfs(struct amdgpu_device *adev);

enum amdgpu_sriov_vf_mode amdgpu_virt_get_sriov_vf_mode(struct amdgpu_device *adev);

void amdgpu_virt_update_sriov_video_codec(struct amdgpu_device *adev,
			struct amdgpu_video_codec_info *encode, uint32_t encode_array_size,
			struct amdgpu_video_codec_info *decode, uint32_t decode_array_size);
void amdgpu_sriov_wreg(struct amdgpu_device *adev,
		       u32 offset, u32 value,
		       u32 acc_flags, u32 hwip, u32 xcc_id);
u32 amdgpu_sriov_rreg(struct amdgpu_device *adev,
		      u32 offset, u32 acc_flags, u32 hwip, u32 xcc_id);
bool amdgpu_virt_fw_load_skip_check(struct amdgpu_device *adev,
			uint32_t ucode_id);
void amdgpu_virt_post_reset(struct amdgpu_device *adev);
bool amdgpu_sriov_xnack_support(struct amdgpu_device *adev);
bool amdgpu_virt_get_rlcg_reg_access_flag(struct amdgpu_device *adev,
					  u32 acc_flags, u32 hwip,
					  bool write, u32 *rlcg_flag);
u32 amdgpu_virt_rlcg_reg_rw(struct amdgpu_device *adev, u32 offset, u32 v, u32 flag, u32 xcc_id);
#endif
