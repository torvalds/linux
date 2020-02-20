/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_SDMA_H__
#define __AMDGPU_SDMA_H__

/* max number of IP instances */
#define AMDGPU_MAX_SDMA_INSTANCES		8

enum amdgpu_sdma_irq {
	AMDGPU_SDMA_IRQ_INSTANCE0  = 0,
	AMDGPU_SDMA_IRQ_INSTANCE1,
	AMDGPU_SDMA_IRQ_INSTANCE2,
	AMDGPU_SDMA_IRQ_INSTANCE3,
	AMDGPU_SDMA_IRQ_INSTANCE4,
	AMDGPU_SDMA_IRQ_INSTANCE5,
	AMDGPU_SDMA_IRQ_INSTANCE6,
	AMDGPU_SDMA_IRQ_INSTANCE7,
	AMDGPU_SDMA_IRQ_LAST
};

struct amdgpu_sdma_instance {
	/* SDMA firmware */
	const struct firmware	*fw;
	uint32_t		fw_version;
	uint32_t		feature_version;

	struct amdgpu_ring	ring;
	struct amdgpu_ring	page;
	bool			burst_nop;
};

struct amdgpu_sdma_ras_funcs {
	int (*ras_late_init)(struct amdgpu_device *adev,
			void *ras_ih_info);
	void (*ras_fini)(struct amdgpu_device *adev);
	int (*query_ras_error_count)(struct amdgpu_device *adev,
			uint32_t instance, void *ras_error_status);
};

struct amdgpu_sdma {
	struct amdgpu_sdma_instance instance[AMDGPU_MAX_SDMA_INSTANCES];
	struct drm_gpu_scheduler    *sdma_sched[AMDGPU_MAX_SDMA_INSTANCES];
	uint32_t		    num_sdma_sched;
	struct amdgpu_irq_src	trap_irq;
	struct amdgpu_irq_src	illegal_inst_irq;
	struct amdgpu_irq_src	ecc_irq;
	int			num_instances;
	uint32_t                    srbm_soft_reset;
	bool			has_page_queue;
	struct ras_common_if	*ras_if;
	const struct amdgpu_sdma_ras_funcs	*funcs;
};

/*
 * Provided by hw blocks that can move/clear data.  e.g., gfx or sdma
 * But currently, we use sdma to move data.
 */
struct amdgpu_buffer_funcs {
	/* maximum bytes in a single operation */
	uint32_t	copy_max_bytes;

	/* number of dw to reserve per operation */
	unsigned	copy_num_dw;

	/* used for buffer migration */
	void (*emit_copy_buffer)(struct amdgpu_ib *ib,
				 /* src addr in bytes */
				 uint64_t src_offset,
				 /* dst addr in bytes */
				 uint64_t dst_offset,
				 /* number of byte to transfer */
				 uint32_t byte_count);

	/* maximum bytes in a single operation */
	uint32_t	fill_max_bytes;

	/* number of dw to reserve per operation */
	unsigned	fill_num_dw;

	/* used for buffer clearing */
	void (*emit_fill_buffer)(struct amdgpu_ib *ib,
				 /* value to write to memory */
				 uint32_t src_data,
				 /* dst addr in bytes */
				 uint64_t dst_offset,
				 /* number of byte to fill */
				 uint32_t byte_count);
};

#define amdgpu_emit_copy_buffer(adev, ib, s, d, b) (adev)->mman.buffer_funcs->emit_copy_buffer((ib),  (s), (d), (b))
#define amdgpu_emit_fill_buffer(adev, ib, s, d, b) (adev)->mman.buffer_funcs->emit_fill_buffer((ib), (s), (d), (b))

struct amdgpu_sdma_instance *
amdgpu_sdma_get_instance_from_ring(struct amdgpu_ring *ring);
int amdgpu_sdma_get_index_from_ring(struct amdgpu_ring *ring, uint32_t *index);
uint64_t amdgpu_sdma_get_csa_mc_addr(struct amdgpu_ring *ring, unsigned vmid);
int amdgpu_sdma_ras_late_init(struct amdgpu_device *adev,
			      void *ras_ih_info);
void amdgpu_sdma_ras_fini(struct amdgpu_device *adev);
int amdgpu_sdma_process_ras_data_cb(struct amdgpu_device *adev,
		void *err_data,
		struct amdgpu_iv_entry *entry);
int amdgpu_sdma_process_ecc_irq(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry);
#endif
