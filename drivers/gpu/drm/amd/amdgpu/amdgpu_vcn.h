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

#define AMDGPU_VCN_STACK_SIZE		(128*1024)
#define AMDGPU_VCN_CONTEXT_SIZE 	(512*1024)

#define AMDGPU_VCN_FIRMWARE_OFFSET	256
#define AMDGPU_VCN_MAX_ENC_RINGS	3

#define AMDGPU_MAX_VCN_INSTANCES	2

#define AMDGPU_VCN_HARVEST_VCN0 (1 << 0)
#define AMDGPU_VCN_HARVEST_VCN1 (1 << 1)

#define VCN_DEC_CMD_FENCE		0x00000000
#define VCN_DEC_CMD_TRAP		0x00000001
#define VCN_DEC_CMD_WRITE_REG		0x00000004
#define VCN_DEC_CMD_REG_READ_COND_WAIT	0x00000006
#define VCN_DEC_CMD_PACKET_START	0x0000000a
#define VCN_DEC_CMD_PACKET_END		0x0000000b

#define VCN_ENC_CMD_NO_OP		0x00000000
#define VCN_ENC_CMD_END 		0x00000001
#define VCN_ENC_CMD_IB			0x00000002
#define VCN_ENC_CMD_FENCE		0x00000003
#define VCN_ENC_CMD_TRAP		0x00000004
#define VCN_ENC_CMD_REG_WRITE		0x0000000b
#define VCN_ENC_CMD_REG_WAIT		0x0000000c

#define VCN_VID_SOC_ADDRESS_2_0 	0x1fa00
#define VCN_AON_SOC_ADDRESS_2_0 	0x1f800
#define VCN_VID_IP_ADDRESS_2_0		0x0
#define VCN_AON_IP_ADDRESS_2_0		0x30000

#define RREG32_SOC15_DPG_MODE(ip, inst, reg, mask, sram_sel) 				\
	({	WREG32_SOC15(ip, inst, mmUVD_DPG_LMA_MASK, mask); 			\
		WREG32_SOC15(ip, inst, mmUVD_DPG_LMA_CTL, 				\
			UVD_DPG_LMA_CTL__MASK_EN_MASK | 				\
			((adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg) 	\
			<< UVD_DPG_LMA_CTL__READ_WRITE_ADDR__SHIFT) | 			\
			(sram_sel << UVD_DPG_LMA_CTL__SRAM_SEL__SHIFT)); 		\
		RREG32_SOC15(ip, inst, mmUVD_DPG_LMA_DATA); 				\
	})

#define WREG32_SOC15_DPG_MODE(ip, inst, reg, value, mask, sram_sel) 			\
	do { 										\
		WREG32_SOC15(ip, inst, mmUVD_DPG_LMA_DATA, value); 			\
		WREG32_SOC15(ip, inst, mmUVD_DPG_LMA_MASK, mask); 			\
		WREG32_SOC15(ip, inst, mmUVD_DPG_LMA_CTL, 				\
			UVD_DPG_LMA_CTL__READ_WRITE_MASK | 				\
			((adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg) 	\
			<< UVD_DPG_LMA_CTL__READ_WRITE_ADDR__SHIFT) | 			\
			(sram_sel << UVD_DPG_LMA_CTL__SRAM_SEL__SHIFT)); 		\
	} while (0)

#define SOC15_DPG_MODE_OFFSET_2_0(ip, inst, reg) 						\
	({											\
		uint32_t internal_reg_offset, addr;						\
		bool video_range, aon_range;							\
												\
		addr = (adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg);		\
		addr <<= 2; 									\
		video_range = ((((0xFFFFF & addr) >= (VCN_VID_SOC_ADDRESS_2_0)) && 		\
				((0xFFFFF & addr) < ((VCN_VID_SOC_ADDRESS_2_0 + 0x2600)))));	\
		aon_range   = ((((0xFFFFF & addr) >= (VCN_AON_SOC_ADDRESS_2_0)) && 		\
				((0xFFFFF & addr) < ((VCN_AON_SOC_ADDRESS_2_0 + 0x600)))));	\
		if (video_range) 								\
			internal_reg_offset = ((0xFFFFF & addr) - (VCN_VID_SOC_ADDRESS_2_0) + 	\
				(VCN_VID_IP_ADDRESS_2_0));					\
		else if (aon_range)								\
			internal_reg_offset = ((0xFFFFF & addr) - (VCN_AON_SOC_ADDRESS_2_0) + 	\
				(VCN_AON_IP_ADDRESS_2_0));					\
		else										\
			internal_reg_offset = (0xFFFFF & addr);					\
												\
		internal_reg_offset >>= 2;							\
	})

#define RREG32_SOC15_DPG_MODE_2_0(offset, mask_en) 						\
	({ 											\
		WREG32_SOC15(VCN, 0, mmUVD_DPG_LMA_CTL, 					\
			(0x0 << UVD_DPG_LMA_CTL__READ_WRITE__SHIFT | 				\
			mask_en << UVD_DPG_LMA_CTL__MASK_EN__SHIFT | 				\
			offset << UVD_DPG_LMA_CTL__READ_WRITE_ADDR__SHIFT)); 			\
		RREG32_SOC15(VCN, 0, mmUVD_DPG_LMA_DATA); 					\
	})

#define WREG32_SOC15_DPG_MODE_2_0(offset, value, mask_en, indirect)				\
	do { 											\
		if (!indirect) { 								\
			WREG32_SOC15(VCN, 0, mmUVD_DPG_LMA_DATA, value); 			\
			WREG32_SOC15(VCN, 0, mmUVD_DPG_LMA_CTL, 				\
				(0x1 << UVD_DPG_LMA_CTL__READ_WRITE__SHIFT | 			\
				 mask_en << UVD_DPG_LMA_CTL__MASK_EN__SHIFT | 			\
				 offset << UVD_DPG_LMA_CTL__READ_WRITE_ADDR__SHIFT)); 		\
		} else { 									\
			*adev->vcn.dpg_sram_curr_addr++ = offset; 				\
			*adev->vcn.dpg_sram_curr_addr++ = value; 				\
		} 										\
	} while (0)

enum engine_status_constants {
	UVD_PGFSM_STATUS__UVDM_UVDU_PWR_ON = 0x2AAAA0,
	UVD_PGFSM_STATUS__UVDM_UVDU_PWR_ON_2_0 = 0xAAAA0,
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
	unsigned	jpeg_pitch;
};

struct amdgpu_vcn_inst {
	struct amdgpu_bo	*vcpu_bo;
	void			*cpu_addr;
	uint64_t		gpu_addr;
	void			*saved_bo;
	struct amdgpu_ring	ring_dec;
	struct amdgpu_ring	ring_enc[AMDGPU_VCN_MAX_ENC_RINGS];
	struct amdgpu_ring	ring_jpeg;
	struct amdgpu_irq_src	irq;
	struct amdgpu_vcn_reg	external;
};

struct amdgpu_vcn {
	unsigned		fw_version;
	struct delayed_work	idle_work;
	const struct firmware	*fw;	/* VCN firmware */
	unsigned		num_enc_rings;
	enum amd_powergating_state cur_state;
	struct dpg_pause_state pause_state;

	bool			indirect_sram;
	struct amdgpu_bo	*dpg_sram_bo;
	void			*dpg_sram_cpu_addr;
	uint64_t		dpg_sram_gpu_addr;
	uint32_t		*dpg_sram_curr_addr;

	uint8_t	num_vcn_inst;
	struct amdgpu_vcn_inst	inst[AMDGPU_MAX_VCN_INSTANCES];
	struct amdgpu_vcn_reg	internal;

	unsigned	harvest_config;
	int (*pause_dpg_mode)(struct amdgpu_device *adev,
		struct dpg_pause_state *new_state);
};

int amdgpu_vcn_sw_init(struct amdgpu_device *adev);
int amdgpu_vcn_sw_fini(struct amdgpu_device *adev);
int amdgpu_vcn_suspend(struct amdgpu_device *adev);
int amdgpu_vcn_resume(struct amdgpu_device *adev);
void amdgpu_vcn_ring_begin_use(struct amdgpu_ring *ring);
void amdgpu_vcn_ring_end_use(struct amdgpu_ring *ring);

int amdgpu_vcn_dec_ring_test_ring(struct amdgpu_ring *ring);
int amdgpu_vcn_dec_ring_test_ib(struct amdgpu_ring *ring, long timeout);

int amdgpu_vcn_enc_ring_test_ring(struct amdgpu_ring *ring);
int amdgpu_vcn_enc_ring_test_ib(struct amdgpu_ring *ring, long timeout);

int amdgpu_vcn_jpeg_ring_test_ring(struct amdgpu_ring *ring);
int amdgpu_vcn_jpeg_ring_test_ib(struct amdgpu_ring *ring, long timeout);

#endif
