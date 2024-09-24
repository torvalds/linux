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
 */

#ifndef __AMDGPU_VCN_H__
#define __AMDGPU_VCN_H__

#include "amdgpu_ras.h"

#define AMDGPU_VCN_STACK_SIZE		(128*1024)
#define AMDGPU_VCN_CONTEXT_SIZE 	(512*1024)

#define AMDGPU_VCN_FIRMWARE_OFFSET	256
#define AMDGPU_VCN_MAX_ENC_RINGS	3

#define AMDGPU_MAX_VCN_INSTANCES	4
#define AMDGPU_MAX_VCN_ENC_RINGS  (AMDGPU_VCN_MAX_ENC_RINGS * AMDGPU_MAX_VCN_INSTANCES)

#define AMDGPU_VCN_HARVEST_VCN0 (1 << 0)
#define AMDGPU_VCN_HARVEST_VCN1 (1 << 1)

#define VCN_DEC_KMD_CMD 		0x80000000
#define VCN_DEC_CMD_FENCE		0x00000000
#define VCN_DEC_CMD_TRAP		0x00000001
#define VCN_DEC_CMD_WRITE_REG		0x00000004
#define VCN_DEC_CMD_REG_READ_COND_WAIT	0x00000006
#define VCN_DEC_CMD_PACKET_START	0x0000000a
#define VCN_DEC_CMD_PACKET_END		0x0000000b

#define VCN_DEC_SW_CMD_NO_OP		0x00000000
#define VCN_DEC_SW_CMD_END		0x00000001
#define VCN_DEC_SW_CMD_IB		0x00000002
#define VCN_DEC_SW_CMD_FENCE		0x00000003
#define VCN_DEC_SW_CMD_TRAP		0x00000004
#define VCN_DEC_SW_CMD_IB_AUTO		0x00000005
#define VCN_DEC_SW_CMD_SEMAPHORE	0x00000006
#define VCN_DEC_SW_CMD_PREEMPT_FENCE	0x00000009
#define VCN_DEC_SW_CMD_REG_WRITE	0x0000000b
#define VCN_DEC_SW_CMD_REG_WAIT		0x0000000c

#define VCN_ENC_CMD_NO_OP		0x00000000
#define VCN_ENC_CMD_END 		0x00000001
#define VCN_ENC_CMD_IB			0x00000002
#define VCN_ENC_CMD_FENCE		0x00000003
#define VCN_ENC_CMD_TRAP		0x00000004
#define VCN_ENC_CMD_REG_WRITE		0x0000000b
#define VCN_ENC_CMD_REG_WAIT		0x0000000c

#define VCN_AON_SOC_ADDRESS_2_0 	0x1f800
#define VCN1_AON_SOC_ADDRESS_3_0 	0x48000
#define VCN_VID_IP_ADDRESS_2_0		0x0
#define VCN_AON_IP_ADDRESS_2_0		0x30000

#define mmUVD_RBC_XX_IB_REG_CHECK 					0x026b
#define mmUVD_RBC_XX_IB_REG_CHECK_BASE_IDX 				1
#define mmUVD_REG_XX_MASK 						0x026c
#define mmUVD_REG_XX_MASK_BASE_IDX 					1

/* 1 second timeout */
#define VCN_IDLE_TIMEOUT	msecs_to_jiffies(1000)

#define RREG32_SOC15_DPG_MODE_1_0(ip, inst_idx, reg, mask, sram_sel) 			\
	({	WREG32_SOC15(ip, inst_idx, mmUVD_DPG_LMA_MASK, mask); 			\
		WREG32_SOC15(ip, inst_idx, mmUVD_DPG_LMA_CTL, 				\
			UVD_DPG_LMA_CTL__MASK_EN_MASK | 				\
			((adev->reg_offset[ip##_HWIP][inst_idx][reg##_BASE_IDX] + reg) 	\
			<< UVD_DPG_LMA_CTL__READ_WRITE_ADDR__SHIFT) | 			\
			(sram_sel << UVD_DPG_LMA_CTL__SRAM_SEL__SHIFT)); 		\
		RREG32_SOC15(ip, inst_idx, mmUVD_DPG_LMA_DATA); 			\
	})

#define WREG32_SOC15_DPG_MODE_1_0(ip, inst_idx, reg, value, mask, sram_sel) 		\
	do { 										\
		WREG32_SOC15(ip, inst_idx, mmUVD_DPG_LMA_DATA, value); 			\
		WREG32_SOC15(ip, inst_idx, mmUVD_DPG_LMA_MASK, mask); 			\
		WREG32_SOC15(ip, inst_idx, mmUVD_DPG_LMA_CTL, 				\
			UVD_DPG_LMA_CTL__READ_WRITE_MASK | 				\
			((adev->reg_offset[ip##_HWIP][inst_idx][reg##_BASE_IDX] + reg) 	\
			<< UVD_DPG_LMA_CTL__READ_WRITE_ADDR__SHIFT) | 			\
			(sram_sel << UVD_DPG_LMA_CTL__SRAM_SEL__SHIFT)); 		\
	} while (0)

#define SOC15_DPG_MODE_OFFSET(ip, inst_idx, reg) 						\
	({											\
		uint32_t internal_reg_offset, addr;						\
		bool video_range, video1_range, aon_range, aon1_range;				\
												\
		addr = (adev->reg_offset[ip##_HWIP][inst_idx][reg##_BASE_IDX] + reg);		\
		addr <<= 2; 									\
		video_range = ((((0xFFFFF & addr) >= (VCN_VID_SOC_ADDRESS_2_0)) && 		\
				((0xFFFFF & addr) < ((VCN_VID_SOC_ADDRESS_2_0 + 0x2600)))));	\
		video1_range = ((((0xFFFFF & addr) >= (VCN1_VID_SOC_ADDRESS_3_0)) && 		\
				((0xFFFFF & addr) < ((VCN1_VID_SOC_ADDRESS_3_0 + 0x2600)))));	\
		aon_range   = ((((0xFFFFF & addr) >= (VCN_AON_SOC_ADDRESS_2_0)) && 		\
				((0xFFFFF & addr) < ((VCN_AON_SOC_ADDRESS_2_0 + 0x600)))));	\
		aon1_range   = ((((0xFFFFF & addr) >= (VCN1_AON_SOC_ADDRESS_3_0)) && 		\
				((0xFFFFF & addr) < ((VCN1_AON_SOC_ADDRESS_3_0 + 0x600)))));	\
		if (video_range) 								\
			internal_reg_offset = ((0xFFFFF & addr) - (VCN_VID_SOC_ADDRESS_2_0) + 	\
				(VCN_VID_IP_ADDRESS_2_0));					\
		else if (aon_range)								\
			internal_reg_offset = ((0xFFFFF & addr) - (VCN_AON_SOC_ADDRESS_2_0) + 	\
				(VCN_AON_IP_ADDRESS_2_0));					\
		else if (video1_range) 								\
			internal_reg_offset = ((0xFFFFF & addr) - (VCN1_VID_SOC_ADDRESS_3_0) + 	\
				(VCN_VID_IP_ADDRESS_2_0));					\
		else if (aon1_range)								\
			internal_reg_offset = ((0xFFFFF & addr) - (VCN1_AON_SOC_ADDRESS_3_0) + 	\
				(VCN_AON_IP_ADDRESS_2_0));					\
		else										\
			internal_reg_offset = (0xFFFFF & addr);					\
												\
		internal_reg_offset >>= 2;							\
	})

#define RREG32_SOC15_DPG_MODE(inst_idx, offset, mask_en) 					\
	({											\
		WREG32_SOC15(VCN, inst_idx, mmUVD_DPG_LMA_CTL, 					\
			(0x0 << UVD_DPG_LMA_CTL__READ_WRITE__SHIFT |				\
			mask_en << UVD_DPG_LMA_CTL__MASK_EN__SHIFT |				\
			offset << UVD_DPG_LMA_CTL__READ_WRITE_ADDR__SHIFT));			\
		RREG32_SOC15(VCN, inst_idx, mmUVD_DPG_LMA_DATA);				\
	})

#define WREG32_SOC15_DPG_MODE(inst_idx, offset, value, mask_en, indirect)             \
	do {                                                                          \
		if (!indirect) {                                                      \
			WREG32_SOC15(VCN, GET_INST(VCN, inst_idx),                    \
				     mmUVD_DPG_LMA_DATA, value);                      \
			WREG32_SOC15(                                                 \
				VCN, GET_INST(VCN, inst_idx),                         \
				mmUVD_DPG_LMA_CTL,                                    \
				(0x1 << UVD_DPG_LMA_CTL__READ_WRITE__SHIFT |          \
				 mask_en << UVD_DPG_LMA_CTL__MASK_EN__SHIFT |         \
				 offset << UVD_DPG_LMA_CTL__READ_WRITE_ADDR__SHIFT)); \
		} else {                                                              \
			*adev->vcn.inst[inst_idx].dpg_sram_curr_addr++ =              \
				offset;                                               \
			*adev->vcn.inst[inst_idx].dpg_sram_curr_addr++ =              \
				value;                                                \
		}                                                                     \
	} while (0)

#define SOC24_DPG_MODE_OFFSET(ip, inst_idx, reg)						\
	({											\
		uint32_t internal_reg_offset, addr;						\
		bool video_range, aon_range;				\
												\
		addr = (adev->reg_offset[ip##_HWIP][inst_idx][reg##_BASE_IDX] + reg);		\
		addr <<= 2;									\
		video_range = ((((0xFFFFF & addr) >= (VCN_VID_SOC_ADDRESS)) &&			\
				((0xFFFFF & addr) < ((VCN_VID_SOC_ADDRESS + 0x2600)))));	\
		aon_range   = ((((0xFFFFF & addr) >= (VCN_AON_SOC_ADDRESS)) &&			\
				((0xFFFFF & addr) < ((VCN_AON_SOC_ADDRESS + 0x600)))));		\
		if (video_range)								\
			internal_reg_offset = ((0xFFFFF & addr) - (VCN_VID_SOC_ADDRESS) +	\
				(VCN_VID_IP_ADDRESS));						\
		else if (aon_range)								\
			internal_reg_offset = ((0xFFFFF & addr) - (VCN_AON_SOC_ADDRESS) +	\
				(VCN_AON_IP_ADDRESS));						\
		else										\
			internal_reg_offset = (0xFFFFF & addr);					\
												\
		internal_reg_offset >>= 2;							\
	})

#define WREG32_SOC24_DPG_MODE(inst_idx, offset, value, mask_en, indirect)		\
	do {										\
		if (!indirect) {							\
			WREG32_SOC15(VCN, GET_INST(VCN, inst_idx),			\
				     regUVD_DPG_LMA_DATA, value);			\
			WREG32_SOC15(							\
				VCN, GET_INST(VCN, inst_idx),				\
				regUVD_DPG_LMA_CTL,					\
				(0x1 << UVD_DPG_LMA_CTL__READ_WRITE__SHIFT |		\
				 mask_en << UVD_DPG_LMA_CTL__MASK_EN__SHIFT |		\
				 offset << UVD_DPG_LMA_CTL__READ_WRITE_ADDR__SHIFT));	\
		} else {								\
			*adev->vcn.inst[inst_idx].dpg_sram_curr_addr++ =		\
				offset;							\
			*adev->vcn.inst[inst_idx].dpg_sram_curr_addr++ =		\
				value;							\
		}									\
	} while (0)

#define AMDGPU_FW_SHARED_FLAG_0_UNIFIED_QUEUE (1 << 2)
#define AMDGPU_FW_SHARED_FLAG_0_DRM_KEY_INJECT (1 << 4)
#define AMDGPU_VCN_FW_SHARED_FLAG_0_RB	(1 << 6)
#define AMDGPU_VCN_MULTI_QUEUE_FLAG	(1 << 8)
#define AMDGPU_VCN_SW_RING_FLAG		(1 << 9)
#define AMDGPU_VCN_FW_LOGGING_FLAG	(1 << 10)
#define AMDGPU_VCN_SMU_VERSION_INFO_FLAG (1 << 11)
#define AMDGPU_VCN_SMU_DPM_INTERFACE_FLAG (1 << 11)
#define AMDGPU_VCN_VF_RB_SETUP_FLAG (1 << 14)
#define AMDGPU_VCN_VF_RB_DECOUPLE_FLAG (1 << 15)

#define MAX_NUM_VCN_RB_SETUP 4

#define AMDGPU_VCN_IB_FLAG_DECODE_BUFFER	0x00000001
#define AMDGPU_VCN_CMD_FLAG_MSG_BUFFER		0x00000001

#define VCN_CODEC_DISABLE_MASK_AV1  (1 << 0)
#define VCN_CODEC_DISABLE_MASK_VP9  (1 << 1)
#define VCN_CODEC_DISABLE_MASK_HEVC (1 << 2)
#define VCN_CODEC_DISABLE_MASK_H264 (1 << 3)

#define AMDGPU_VCN_SMU_DPM_INTERFACE_DGPU (0)
#define AMDGPU_VCN_SMU_DPM_INTERFACE_APU (1)

#define AMDGPU_DRM_KEY_INJECT_WORKAROUND_VCNFW_ASD_HANDSHAKING 2

enum fw_queue_mode {
	FW_QUEUE_RING_RESET = 1,
	FW_QUEUE_DPG_HOLD_OFF = 2,
};

enum engine_status_constants {
	UVD_PGFSM_STATUS__UVDM_UVDU_PWR_ON = 0x2AAAA0,
	UVD_PGFSM_STATUS__UVDM_UVDU_PWR_ON_2_0 = 0xAAAA0,
	UVD_PGFSM_STATUS__UVDM_UVDU_UVDLM_PWR_ON_3_0 = 0x2A2A8AA0,
	UVD_PGFSM_CONFIG__UVDM_UVDU_PWR_ON = 0x00000002,
	UVD_STATUS__UVD_BUSY = 0x00000004,
	GB_ADDR_CONFIG_DEFAULT = 0x26010011,
	UVD_STATUS__IDLE = 0x2,
	UVD_STATUS__BUSY = 0x5,
	UVD_POWER_STATUS__UVD_POWER_STATUS_TILES_OFF = 0x1,
	UVD_STATUS__RBC_BUSY = 0x1,
	UVD_PGFSM_STATUS_UVDJ_PWR_ON = 0,
};

enum internal_dpg_state {
	VCN_DPG_STATE__UNPAUSE = 0,
	VCN_DPG_STATE__PAUSE,
};

struct dpg_pause_state {
	enum internal_dpg_state fw_based;
	enum internal_dpg_state jpeg;
};

struct amdgpu_vcn_reg{
	unsigned	data0;
	unsigned	data1;
	unsigned	cmd;
	unsigned	nop;
	unsigned	context_id;
	unsigned	ib_vmid;
	unsigned	ib_bar_low;
	unsigned	ib_bar_high;
	unsigned	ib_size;
	unsigned	gp_scratch8;
	unsigned	scratch9;
};

struct amdgpu_vcn_fw_shared {
	void        *cpu_addr;
	uint64_t    gpu_addr;
	uint32_t    mem_size;
	uint32_t    log_offset;
};

struct amdgpu_vcn_inst {
	struct amdgpu_bo	*vcpu_bo;
	void			*cpu_addr;
	uint64_t		gpu_addr;
	void			*saved_bo;
	struct amdgpu_ring	ring_dec;
	struct amdgpu_ring	ring_enc[AMDGPU_VCN_MAX_ENC_RINGS];
	atomic_t		sched_score;
	struct amdgpu_irq_src	irq;
	struct amdgpu_irq_src	ras_poison_irq;
	struct amdgpu_vcn_reg	external;
	struct amdgpu_bo	*dpg_sram_bo;
	struct dpg_pause_state	pause_state;
	void			*dpg_sram_cpu_addr;
	uint64_t		dpg_sram_gpu_addr;
	uint32_t		*dpg_sram_curr_addr;
	atomic_t		dpg_enc_submission_cnt;
	struct amdgpu_vcn_fw_shared fw_shared;
	uint8_t			aid_id;
};

struct amdgpu_vcn_ras {
	struct amdgpu_ras_block_object ras_block;
};

struct amdgpu_vcn {
	unsigned		fw_version;
	struct delayed_work	idle_work;
	const struct firmware	*fw[AMDGPU_MAX_VCN_INSTANCES];	/* VCN firmware */
	unsigned		num_enc_rings;
	enum amd_powergating_state cur_state;
	bool			indirect_sram;

	uint8_t	num_vcn_inst;
	struct amdgpu_vcn_inst	 inst[AMDGPU_MAX_VCN_INSTANCES];
	uint8_t			 vcn_config[AMDGPU_MAX_VCN_INSTANCES];
	uint32_t		 vcn_codec_disable_mask[AMDGPU_MAX_VCN_INSTANCES];
	struct amdgpu_vcn_reg	 internal;
	struct mutex		 vcn_pg_lock;
	struct mutex		vcn1_jpeg1_workaround;
	atomic_t		 total_submission_cnt;

	unsigned	harvest_config;
	int (*pause_dpg_mode)(struct amdgpu_device *adev,
		int inst_idx, struct dpg_pause_state *new_state);

	struct ras_common_if    *ras_if;
	struct amdgpu_vcn_ras   *ras;

	uint16_t inst_mask;
	uint8_t	num_inst_per_aid;
	bool using_unified_queue;

	/* IP reg dump */
	uint32_t		*ip_dump;
};

struct amdgpu_fw_shared_rb_ptrs_struct {
	/* to WA DPG R/W ptr issues.*/
	uint32_t  rptr;
	uint32_t  wptr;
};

struct amdgpu_fw_shared_multi_queue {
	uint8_t decode_queue_mode;
	uint8_t encode_generalpurpose_queue_mode;
	uint8_t encode_lowlatency_queue_mode;
	uint8_t encode_realtime_queue_mode;
	uint8_t padding[4];
};

struct amdgpu_fw_shared_sw_ring {
	uint8_t is_enabled;
	uint8_t padding[3];
};

struct amdgpu_fw_shared_unified_queue_struct {
	uint8_t is_enabled;
	uint8_t queue_mode;
	uint8_t queue_status;
	uint8_t padding[5];
};

struct amdgpu_fw_shared_fw_logging {
	uint8_t is_enabled;
	uint32_t addr_lo;
	uint32_t addr_hi;
	uint32_t size;
};

struct amdgpu_fw_shared_smu_interface_info {
	uint8_t smu_interface_type;
	uint8_t padding[3];
};

struct amdgpu_fw_shared {
	uint32_t present_flag_0;
	uint8_t pad[44];
	struct amdgpu_fw_shared_rb_ptrs_struct rb;
	uint8_t pad1[1];
	struct amdgpu_fw_shared_multi_queue multi_queue;
	struct amdgpu_fw_shared_sw_ring sw_ring;
	struct amdgpu_fw_shared_fw_logging fw_log;
	struct amdgpu_fw_shared_smu_interface_info smu_interface_info;
};

struct amdgpu_vcn_rb_setup_info {
	uint32_t  rb_addr_lo;
	uint32_t  rb_addr_hi;
	uint32_t  rb_size;
};

struct amdgpu_fw_shared_rb_setup {
	uint32_t is_rb_enabled_flags;

	union {
		struct {
			uint32_t rb_addr_lo;
			uint32_t rb_addr_hi;
			uint32_t  rb_size;
			uint32_t  rb4_addr_lo;
			uint32_t  rb4_addr_hi;
			uint32_t  rb4_size;
			uint32_t  reserved[6];
		};

		struct {
			struct amdgpu_vcn_rb_setup_info rb_info[MAX_NUM_VCN_RB_SETUP];
		};
	};
};

struct amdgpu_fw_shared_drm_key_wa {
	uint8_t  method;
	uint8_t  reserved[3];
};

struct amdgpu_fw_shared_queue_decouple {
	uint8_t  is_enabled;
	uint8_t  reserved[7];
};

struct amdgpu_vcn4_fw_shared {
	uint32_t present_flag_0;
	uint8_t pad[12];
	struct amdgpu_fw_shared_unified_queue_struct sq;
	uint8_t pad1[8];
	struct amdgpu_fw_shared_fw_logging fw_log;
	uint8_t pad2[20];
	struct amdgpu_fw_shared_rb_setup rb_setup;
	struct amdgpu_fw_shared_smu_interface_info smu_dpm_interface;
	struct amdgpu_fw_shared_drm_key_wa drm_key_wa;
	uint8_t pad3[9];
	struct amdgpu_fw_shared_queue_decouple decouple;
};

struct amdgpu_vcn_fwlog {
	uint32_t rptr;
	uint32_t wptr;
	uint32_t buffer_size;
	uint32_t header_size;
	uint8_t wrapped;
};

struct amdgpu_vcn_decode_buffer {
	uint32_t valid_buf_flag;
	uint32_t msg_buffer_address_hi;
	uint32_t msg_buffer_address_lo;
	uint32_t pad[30];
};

struct amdgpu_vcn_rb_metadata {
	uint32_t size;
	uint32_t present_flag_0;

	uint8_t version;
	uint8_t ring_id;
	uint8_t pad[26];
};

struct amdgpu_vcn5_fw_shared {
	uint32_t present_flag_0;
	uint8_t pad[12];
	struct amdgpu_fw_shared_unified_queue_struct sq;
	uint8_t pad1[8];
	struct amdgpu_fw_shared_fw_logging fw_log;
	uint8_t pad2[20];
	struct amdgpu_fw_shared_rb_setup rb_setup;
	struct amdgpu_fw_shared_smu_interface_info smu_dpm_interface;
	struct amdgpu_fw_shared_drm_key_wa drm_key_wa;
	uint8_t pad3[9];
};

#define VCN_BLOCK_ENCODE_DISABLE_MASK 0x80
#define VCN_BLOCK_DECODE_DISABLE_MASK 0x40
#define VCN_BLOCK_QUEUE_DISABLE_MASK 0xC0

enum vcn_ring_type {
	VCN_ENCODE_RING,
	VCN_DECODE_RING,
	VCN_UNIFIED_RING,
};

int amdgpu_vcn_early_init(struct amdgpu_device *adev);
int amdgpu_vcn_sw_init(struct amdgpu_device *adev);
int amdgpu_vcn_sw_fini(struct amdgpu_device *adev);
int amdgpu_vcn_suspend(struct amdgpu_device *adev);
int amdgpu_vcn_resume(struct amdgpu_device *adev);
void amdgpu_vcn_ring_begin_use(struct amdgpu_ring *ring);
void amdgpu_vcn_ring_end_use(struct amdgpu_ring *ring);

bool amdgpu_vcn_is_disabled_vcn(struct amdgpu_device *adev,
				enum vcn_ring_type type, uint32_t vcn_instance);

int amdgpu_vcn_dec_ring_test_ring(struct amdgpu_ring *ring);
int amdgpu_vcn_dec_ring_test_ib(struct amdgpu_ring *ring, long timeout);
int amdgpu_vcn_dec_sw_ring_test_ring(struct amdgpu_ring *ring);
int amdgpu_vcn_dec_sw_ring_test_ib(struct amdgpu_ring *ring, long timeout);
int amdgpu_vcn_unified_ring_test_ib(struct amdgpu_ring *ring, long timeout);

int amdgpu_vcn_enc_ring_test_ring(struct amdgpu_ring *ring);
int amdgpu_vcn_enc_ring_test_ib(struct amdgpu_ring *ring, long timeout);

enum amdgpu_ring_priority_level amdgpu_vcn_get_enc_ring_prio(int ring);

void amdgpu_vcn_setup_ucode(struct amdgpu_device *adev);

void amdgpu_vcn_fwlog_init(struct amdgpu_vcn_inst *vcn);
void amdgpu_debugfs_vcn_fwlog_init(struct amdgpu_device *adev,
                                   uint8_t i, struct amdgpu_vcn_inst *vcn);

int amdgpu_vcn_process_poison_irq(struct amdgpu_device *adev,
			struct amdgpu_irq_src *source,
			struct amdgpu_iv_entry *entry);
int amdgpu_vcn_ras_late_init(struct amdgpu_device *adev,
			struct ras_common_if *ras_block);
int amdgpu_vcn_ras_sw_init(struct amdgpu_device *adev);

int amdgpu_vcn_psp_update_sram(struct amdgpu_device *adev, int inst_idx,
			       enum AMDGPU_UCODE_ID ucode_id);

#endif
