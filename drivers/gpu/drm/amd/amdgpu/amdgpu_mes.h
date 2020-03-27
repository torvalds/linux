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

#include "amdgpu_irq.h"
#include "kgd_kfd_interface.h"
#include "amdgpu_gfx.h"
#include <linux/sched/mm.h>

#define AMDGPU_MES_MAX_COMPUTE_PIPES        8
#define AMDGPU_MES_MAX_GFX_PIPES            2
#define AMDGPU_MES_MAX_SDMA_PIPES           2

enum amdgpu_mes_priority_level {
	AMDGPU_MES_PRIORITY_LEVEL_LOW       = 0,
	AMDGPU_MES_PRIORITY_LEVEL_NORMAL    = 1,
	AMDGPU_MES_PRIORITY_LEVEL_MEDIUM    = 2,
	AMDGPU_MES_PRIORITY_LEVEL_HIGH      = 3,
	AMDGPU_MES_PRIORITY_LEVEL_REALTIME  = 4,
	AMDGPU_MES_PRIORITY_NUM_LEVELS
};

#define AMDGPU_MES_PROC_CTX_SIZE 0x1000 /* one page area */
#define AMDGPU_MES_GANG_CTX_SIZE 0x1000 /* one page area */

struct amdgpu_mes_funcs;

enum admgpu_mes_pipe {
	AMDGPU_MES_SCHED_PIPE = 0,
	AMDGPU_MES_KIQ_PIPE,
	AMDGPU_MAX_MES_PIPES = 2,
};

struct amdgpu_mes {
	struct amdgpu_device            *adev;

	struct mutex                    mutex;

	struct idr                      pasid_idr;
	struct idr                      gang_id_idr;
	struct idr                      queue_id_idr;
	struct ida                      doorbell_ida;

	spinlock_t                      queue_id_lock;

	uint32_t                        total_max_queue;
	uint32_t                        doorbell_id_offset;
	uint32_t                        max_doorbell_slices;

	uint64_t                        default_process_quantum;
	uint64_t                        default_gang_quantum;

	struct amdgpu_ring              ring;

	const struct firmware           *fw[AMDGPU_MAX_MES_PIPES];

	/* mes ucode */
	struct amdgpu_bo		*ucode_fw_obj[AMDGPU_MAX_MES_PIPES];
	uint64_t			ucode_fw_gpu_addr[AMDGPU_MAX_MES_PIPES];
	uint32_t			*ucode_fw_ptr[AMDGPU_MAX_MES_PIPES];
	uint32_t                        ucode_fw_version[AMDGPU_MAX_MES_PIPES];
	uint64_t                        uc_start_addr[AMDGPU_MAX_MES_PIPES];

	/* mes ucode data */
	struct amdgpu_bo		*data_fw_obj[AMDGPU_MAX_MES_PIPES];
	uint64_t			data_fw_gpu_addr[AMDGPU_MAX_MES_PIPES];
	uint32_t			*data_fw_ptr[AMDGPU_MAX_MES_PIPES];
	uint32_t                        data_fw_version[AMDGPU_MAX_MES_PIPES];
	uint64_t                        data_start_addr[AMDGPU_MAX_MES_PIPES];

	/* eop gpu obj */
	struct amdgpu_bo		*eop_gpu_obj[AMDGPU_MAX_MES_PIPES];
	uint64_t                        eop_gpu_addr[AMDGPU_MAX_MES_PIPES];

	void                            *mqd_backup[AMDGPU_MAX_MES_PIPES];
	struct amdgpu_irq_src	        irq[AMDGPU_MAX_MES_PIPES];

	uint32_t                        vmid_mask_gfxhub;
	uint32_t                        vmid_mask_mmhub;
	uint32_t                        compute_hqd_mask[AMDGPU_MES_MAX_COMPUTE_PIPES];
	uint32_t                        gfx_hqd_mask[AMDGPU_MES_MAX_GFX_PIPES];
	uint32_t                        sdma_hqd_mask[AMDGPU_MES_MAX_SDMA_PIPES];
	uint32_t                        agreegated_doorbells[AMDGPU_MES_PRIORITY_NUM_LEVELS];
	uint32_t                        sch_ctx_offs;
	uint64_t			sch_ctx_gpu_addr;
	uint64_t			*sch_ctx_ptr;
	uint32_t			query_status_fence_offs;
	uint64_t			query_status_fence_gpu_addr;
	uint64_t			*query_status_fence_ptr;

	/* initialize kiq pipe */
	int                             (*kiq_hw_init)(struct amdgpu_device *adev);

	/* ip specific functions */
	const struct amdgpu_mes_funcs   *funcs;
};

struct amdgpu_mes_process {
	int			pasid;
	struct			amdgpu_vm *vm;
	uint64_t		pd_gpu_addr;
	struct amdgpu_bo 	*proc_ctx_bo;
	uint64_t 		proc_ctx_gpu_addr;
	void 			*proc_ctx_cpu_ptr;
	uint64_t 		process_quantum;
	struct 			list_head gang_list;
	uint32_t 		doorbell_index;
	unsigned long 		*doorbell_bitmap;
	struct mutex		doorbell_lock;
};

struct amdgpu_mes_gang {
	int 				gang_id;
	int 				priority;
	int 				inprocess_gang_priority;
	int 				global_priority_level;
	struct list_head 		list;
	struct amdgpu_mes_process 	*process;
	struct amdgpu_bo 		*gang_ctx_bo;
	uint64_t 			gang_ctx_gpu_addr;
	void 				*gang_ctx_cpu_ptr;
	uint64_t 			gang_quantum;
	struct list_head 		queue_list;
};

struct amdgpu_mes_queue {
	struct list_head 		list;
	struct amdgpu_mes_gang 		*gang;
	int 				queue_id;
	uint64_t 			doorbell_off;
	struct amdgpu_bo		*mqd_obj;
	void				*mqd_cpu_ptr;
	uint64_t 			mqd_gpu_addr;
	uint64_t 			wptr_gpu_addr;
	int 				queue_type;
	int 				paging;
	struct amdgpu_ring 		*ring;
};

struct amdgpu_mes_gang_properties {
	uint32_t 	priority;
	uint32_t 	gang_quantum;
	uint32_t 	inprocess_gang_priority;
	uint32_t 	priority_level;
	int 		global_priority_level;
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


#define amdgpu_mes_kiq_hw_init(adev) (adev)->mes.kiq_hw_init((adev))

int amdgpu_mes_init(struct amdgpu_device *adev);
void amdgpu_mes_fini(struct amdgpu_device *adev);

int amdgpu_mes_create_process(struct amdgpu_device *adev, int pasid,
			      struct amdgpu_vm *vm);
void amdgpu_mes_destroy_process(struct amdgpu_device *adev, int pasid);

int amdgpu_mes_add_gang(struct amdgpu_device *adev, int pasid,
			struct amdgpu_mes_gang_properties *gprops,
			int *gang_id);

#endif /* __AMDGPU_MES_H__ */
