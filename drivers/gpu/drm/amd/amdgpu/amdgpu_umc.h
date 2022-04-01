/*
 * Copyright (C) 2019  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef __AMDGPU_UMC_H__
#define __AMDGPU_UMC_H__

/*
 * (addr / 256) * 4096, the higher 26 bits in ErrorAddr
 * is the index of 4KB block
 */
#define ADDR_OF_4KB_BLOCK(addr)			(((addr) & ~0xffULL) << 4)
/*
 * (addr / 256) * 8192, the higher 26 bits in ErrorAddr
 * is the index of 8KB block
 */
#define ADDR_OF_8KB_BLOCK(addr)			(((addr) & ~0xffULL) << 5)
/* channel index is the index of 256B block */
#define ADDR_OF_256B_BLOCK(channel_index)	((channel_index) << 8)
/* offset in 256B block */
#define OFFSET_IN_256B_BLOCK(addr)		((addr) & 0xffULL)

#define LOOP_UMC_INST(umc_inst) for ((umc_inst) = 0; (umc_inst) < adev->umc.umc_inst_num; (umc_inst)++)
#define LOOP_UMC_CH_INST(ch_inst) for ((ch_inst) = 0; (ch_inst) < adev->umc.channel_inst_num; (ch_inst)++)
#define LOOP_UMC_INST_AND_CH(umc_inst, ch_inst) LOOP_UMC_INST((umc_inst)) LOOP_UMC_CH_INST((ch_inst))

struct amdgpu_umc_ras_funcs {
	void (*err_cnt_init)(struct amdgpu_device *adev);
	int (*ras_late_init)(struct amdgpu_device *adev);
	void (*ras_fini)(struct amdgpu_device *adev);
	void (*query_ras_error_count)(struct amdgpu_device *adev,
				      void *ras_error_status);
	void (*query_ras_error_address)(struct amdgpu_device *adev,
					void *ras_error_status);
	bool (*query_ras_poison_mode)(struct amdgpu_device *adev);
	void (*ecc_info_query_ras_error_count)(struct amdgpu_device *adev,
				      void *ras_error_status);
	void (*ecc_info_query_ras_error_address)(struct amdgpu_device *adev,
					void *ras_error_status);
};

struct amdgpu_umc_funcs {
	void (*init_registers)(struct amdgpu_device *adev);
};

struct amdgpu_umc {
	/* max error count in one ras query call */
	uint32_t max_ras_err_cnt_per_query;
	/* number of umc channel instance with memory map register access */
	uint32_t channel_inst_num;
	/* number of umc instance with memory map register access */
	uint32_t umc_inst_num;
	/* UMC regiser per channel offset */
	uint32_t channel_offs;
	/* channel index table of interleaved memory */
	const uint32_t *channel_idx_tbl;
	struct ras_common_if *ras_if;

	const struct amdgpu_umc_funcs *funcs;
	const struct amdgpu_umc_ras_funcs *ras_funcs;
};

int amdgpu_umc_ras_late_init(struct amdgpu_device *adev);
void amdgpu_umc_ras_fini(struct amdgpu_device *adev);
int amdgpu_umc_poison_handler(struct amdgpu_device *adev,
		void *ras_error_status,
		bool reset);
int amdgpu_umc_process_ecc_irq(struct amdgpu_device *adev,
		struct amdgpu_irq_src *source,
		struct amdgpu_iv_entry *entry);
#endif
