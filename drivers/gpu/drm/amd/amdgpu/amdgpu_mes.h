/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_MES_H__
#define __AMDGPU_MES_H__

struct amdgpu_mes_funcs;

struct amdgpu_mes {
	struct amdgpu_adev *adev;

	const struct firmware           *fw;

	/* mes ucode */
	struct amdgpu_bo		*ucode_fw_obj;
	uint64_t			ucode_fw_gpu_addr;
	uint32_t			*ucode_fw_ptr;
	uint32_t                        ucode_fw_version;
	uint64_t                        uc_start_addr;

	/* mes ucode data */
	struct amdgpu_bo		*data_fw_obj;
	uint64_t			data_fw_gpu_addr;
	uint32_t			*data_fw_ptr;
	uint32_t                        data_fw_version;
	uint64_t                        data_start_addr;

	/* ip specific functions */
	struct amdgpu_mes_funcs *funcs;
};

struct mes_add_queue_input {
	uint32_t	process_id;
	uint64_t	page_table_base_addr;
	uint64_t	process_va_start;
	uint64_t	process_va_end;
	uint64_t	process_quantum;
	uint64_t	process_context_addr;
	uint64_t	gang_quantum;
	uint64_t	gang_context_addr;
	uint32_t	inprocess_gang_priority;
	uint32_t	gang_global_priority_level;
	uint32_t	doorbell_offset;
	uint64_t	mqd_addr;
	uint64_t	wptr_addr;
	uint32_t	queue_type;
	uint32_t	paging;
};

struct mes_remove_queue_input {
	uint32_t	doorbell_offset;
	uint64_t	gang_context_addr;
};

struct mes_suspend_gang_input {
	bool		suspend_all_gangs;
	uint64_t	gang_context_addr;
	uint64_t	suspend_fence_addr;
	uint32_t	suspend_fence_value;
};

struct mes_resume_gang_input {
	bool		resume_all_gangs;
	uint64_t	gang_context_addr;
};

struct amdgpu_mes_funcs {
	int (*add_hw_queue)(struct amdgpu_mes *mes,
			    struct mes_add_queue_input *input);

	int (*remove_hw_queue)(struct amdgpu_mes *mes,
			       struct mes_remove_queue_input *input);

	int (*suspend_gang)(struct amdgpu_mes *mes,
			    struct mes_suspend_gang_input *input);

	int (*resume_gang)(struct amdgpu_mes *mes,
			   struct mes_resume_gang_input *input);
};

#endif /* __AMDGPU_MES_H__ */
