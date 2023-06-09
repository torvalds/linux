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

#define AMDGPU_MES_API_VERSION_SHIFT	12
#define AMDGPU_MES_FEAT_VERSION_SHIFT	24

#define AMDGPU_MES_VERSION_MASK		0x00000fff
#define AMDGPU_MES_API_VERSION_MASK	0x00fff000
#define AMDGPU_MES_FEAT_VERSION_MASK	0xff000000

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

	struct mutex                    mutex_hidden;

	struct idr                      pasid_idr;
	struct idr                      gang_id_idr;
	struct idr                      queue_id_idr;
	struct ida                      doorbell_ida;

	spinlock_t                      queue_id_lock;

	uint32_t			sched_version;
	uint32_t			kiq_version;

	uint32_t                        total_max_queue;
	uint32_t                        doorbell_id_offset;
	uint32_t                        max_doorbell_slices;

	uint64_t                        default_process_quantum;
	uint64_t                        default_gang_quantum;

	struct amdgpu_ring              ring;
	spinlock_t                      ring_lock;

	const struct firmware           *fw[AMDGPU_MAX_MES_PIPES];

	/* mes ucode */
	struct amdgpu_bo		*ucode_fw_obj[AMDGPU_MAX_MES_PIPES];
	uint64_t			ucode_fw_gpu_addr[AMDGPU_MAX_MES_PIPES];
	uint32_t			*ucode_fw_ptr[AMDGPU_MAX_MES_PIPES];
	uint64_t                        uc_start_addr[AMDGPU_MAX_MES_PIPES];

	/* mes ucode data */
	struct amdgpu_bo		*data_fw_obj[AMDGPU_MAX_MES_PIPES];
	uint64_t			data_fw_gpu_addr[AMDGPU_MAX_MES_PIPES];
	uint32_t			*data_fw_ptr[AMDGPU_MAX_MES_PIPES];
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
	uint32_t                        aggregated_doorbells[AMDGPU_MES_PRIORITY_NUM_LEVELS];
	uint32_t                        sch_ctx_offs;
	uint64_t			sch_ctx_gpu_addr;
	uint64_t			*sch_ctx_ptr;
	uint32_t			query_status_fence_offs;
	uint64_t			query_status_fence_gpu_addr;
	uint64_t			*query_status_fence_ptr;
	uint32_t                        read_val_offs;
	uint64_t			read_val_gpu_addr;
	uint32_t			*read_val_ptr;

	uint32_t			saved_flags;

	/* initialize kiq pipe */
	int                             (*kiq_hw_init)(struct amdgpu_device *adev);
	int                             (*kiq_hw_fini)(struct amdgpu_device *adev);

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

struct amdgpu_mes_queue_properties {
	int 			queue_type;
	uint64_t                hqd_base_gpu_addr;
	uint64_t                rptr_gpu_addr;
	uint64_t                wptr_gpu_addr;
	uint64_t                wptr_mc_addr;
	uint32_t                queue_size;
	uint64_t                eop_gpu_addr;
	uint32_t                hqd_pipe_priority;
	uint32_t                hqd_queue_priority;
	bool 			paging;
	struct amdgpu_ring 	*ring;
	/* out */
	uint64_t       		doorbell_off;
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
	uint64_t	wptr_mc_addr;
	uint32_t	queue_type;
	uint32_t	paging;
	uint32_t        gws_base;
	uint32_t        gws_size;
	uint64_t	tba_addr;
	uint64_t	tma_addr;
	uint32_t	is_kfd_process;
	uint32_t	is_aql_queue;
	uint32_t	queue_size;
};

struct mes_remove_queue_input {
	uint32_t	doorbell_offset;
	uint64_t	gang_context_addr;
};

struct mes_unmap_legacy_queue_input {
	enum amdgpu_unmap_queues_action    action;
	uint32_t                           queue_type;
	uint32_t                           doorbell_offset;
	uint32_t                           pipe_id;
	uint32_t                           queue_id;
	uint64_t                           trail_fence_addr;
	uint64_t                           trail_fence_data;
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

enum mes_misc_opcode {
	MES_MISC_OP_WRITE_REG,
	MES_MISC_OP_READ_REG,
	MES_MISC_OP_WRM_REG_WAIT,
	MES_MISC_OP_WRM_REG_WR_WAIT,
};

struct mes_misc_op_input {
	enum mes_misc_opcode op;

	union {
		struct {
			uint32_t                  reg_offset;
			uint64_t                  buffer_addr;
		} read_reg;

		struct {
			uint32_t                  reg_offset;
			uint32_t                  reg_value;
		} write_reg;

		struct {
			uint32_t                   ref;
			uint32_t                   mask;
			uint32_t                   reg0;
			uint32_t                   reg1;
		} wrm_reg;
	};
};

struct amdgpu_mes_funcs {
	int (*add_hw_queue)(struct amdgpu_mes *mes,
			    struct mes_add_queue_input *input);

	int (*remove_hw_queue)(struct amdgpu_mes *mes,
			       struct mes_remove_queue_input *input);

	int (*unmap_legacy_queue)(struct amdgpu_mes *mes,
				  struct mes_unmap_legacy_queue_input *input);

	int (*suspend_gang)(struct amdgpu_mes *mes,
			    struct mes_suspend_gang_input *input);

	int (*resume_gang)(struct amdgpu_mes *mes,
			   struct mes_resume_gang_input *input);

	int (*misc_op)(struct amdgpu_mes *mes,
		       struct mes_misc_op_input *input);
};

#define amdgpu_mes_kiq_hw_init(adev) (adev)->mes.kiq_hw_init((adev))
#define amdgpu_mes_kiq_hw_fini(adev) (adev)->mes.kiq_hw_fini((adev))

int amdgpu_mes_ctx_get_offs(struct amdgpu_ring *ring, unsigned int id_offs);

int amdgpu_mes_init_microcode(struct amdgpu_device *adev, int pipe);
int amdgpu_mes_init(struct amdgpu_device *adev);
void amdgpu_mes_fini(struct amdgpu_device *adev);

int amdgpu_mes_create_process(struct amdgpu_device *adev, int pasid,
			      struct amdgpu_vm *vm);
void amdgpu_mes_destroy_process(struct amdgpu_device *adev, int pasid);

int amdgpu_mes_add_gang(struct amdgpu_device *adev, int pasid,
			struct amdgpu_mes_gang_properties *gprops,
			int *gang_id);
int amdgpu_mes_remove_gang(struct amdgpu_device *adev, int gang_id);

int amdgpu_mes_suspend(struct amdgpu_device *adev);
int amdgpu_mes_resume(struct amdgpu_device *adev);

int amdgpu_mes_add_hw_queue(struct amdgpu_device *adev, int gang_id,
			    struct amdgpu_mes_queue_properties *qprops,
			    int *queue_id);
int amdgpu_mes_remove_hw_queue(struct amdgpu_device *adev, int queue_id);

int amdgpu_mes_unmap_legacy_queue(struct amdgpu_device *adev,
				  struct amdgpu_ring *ring,
				  enum amdgpu_unmap_queues_action action,
				  u64 gpu_addr, u64 seq);

uint32_t amdgpu_mes_rreg(struct amdgpu_device *adev, uint32_t reg);
int amdgpu_mes_wreg(struct amdgpu_device *adev,
		    uint32_t reg, uint32_t val);
int amdgpu_mes_reg_wait(struct amdgpu_device *adev, uint32_t reg,
			uint32_t val, uint32_t mask);
int amdgpu_mes_reg_write_reg_wait(struct amdgpu_device *adev,
				  uint32_t reg0, uint32_t reg1,
				  uint32_t ref, uint32_t mask);

int amdgpu_mes_add_ring(struct amdgpu_device *adev, int gang_id,
			int queue_type, int idx,
			struct amdgpu_mes_ctx_data *ctx_data,
			struct amdgpu_ring **out);
void amdgpu_mes_remove_ring(struct amdgpu_device *adev,
			    struct amdgpu_ring *ring);

uint32_t amdgpu_mes_get_aggregated_doorbell_index(struct amdgpu_device *adev,
						   enum amdgpu_mes_priority_level prio);

int amdgpu_mes_ctx_alloc_meta_data(struct amdgpu_device *adev,
				   struct amdgpu_mes_ctx_data *ctx_data);
void amdgpu_mes_ctx_free_meta_data(struct amdgpu_mes_ctx_data *ctx_data);
int amdgpu_mes_ctx_map_meta_data(struct amdgpu_device *adev,
				 struct amdgpu_vm *vm,
				 struct amdgpu_mes_ctx_data *ctx_data);
int amdgpu_mes_ctx_unmap_meta_data(struct amdgpu_device *adev,
				   struct amdgpu_mes_ctx_data *ctx_data);

int amdgpu_mes_self_test(struct amdgpu_device *adev);

int amdgpu_mes_alloc_process_doorbells(struct amdgpu_device *adev,
					unsigned int *doorbell_index);
void amdgpu_mes_free_process_doorbells(struct amdgpu_device *adev,
					unsigned int doorbell_index);
unsigned int amdgpu_mes_get_doorbell_dw_offset_in_bar(
					struct amdgpu_device *adev,
					uint32_t doorbell_index,
					unsigned int doorbell_id);
int amdgpu_mes_doorbell_process_slice(struct amdgpu_device *adev);

/*
 * MES lock can be taken in MMU notifiers.
 *
 * A bit more detail about why to set no-FS reclaim with MES lock:
 *
 * The purpose of the MMU notifier is to stop GPU access to memory so
 * that the Linux VM subsystem can move pages around safely. This is
 * done by preempting user mode queues for the affected process. When
 * MES is used, MES lock needs to be taken to preempt the queues.
 *
 * The MMU notifier callback entry point in the driver is
 * amdgpu_mn_invalidate_range_start_hsa. The relevant call chain from
 * there is:
 * amdgpu_amdkfd_evict_userptr -> kgd2kfd_quiesce_mm ->
 * kfd_process_evict_queues -> pdd->dev->dqm->ops.evict_process_queues
 *
 * The last part of the chain is a function pointer where we take the
 * MES lock.
 *
 * The problem with taking locks in the MMU notifier is, that MMU
 * notifiers can be called in reclaim-FS context. That's where the
 * kernel frees up pages to make room for new page allocations under
 * memory pressure. While we are running in reclaim-FS context, we must
 * not trigger another memory reclaim operation because that would
 * recursively reenter the reclaim code and cause a deadlock. The
 * memalloc_nofs_save/restore calls guarantee that.
 *
 * In addition we also need to avoid lock dependencies on other locks taken
 * under the MES lock, for example reservation locks. Here is a possible
 * scenario of a deadlock:
 * Thread A: takes and holds reservation lock | triggers reclaim-FS |
 * MMU notifier | blocks trying to take MES lock
 * Thread B: takes and holds MES lock | blocks trying to take reservation lock
 *
 * In this scenario Thread B gets involved in a deadlock even without
 * triggering a reclaim-FS operation itself.
 * To fix this and break the lock dependency chain you'd need to either:
 * 1. protect reservation locks with memalloc_nofs_save/restore, or
 * 2. avoid taking reservation locks under the MES lock.
 *
 * Reservation locks are taken all over the kernel in different subsystems, we
 * have no control over them and their lock dependencies.So the only workable
 * solution is to avoid taking other locks under the MES lock.
 * As a result, make sure no reclaim-FS happens while holding this lock anywhere
 * to prevent deadlocks when an MMU notifier runs in reclaim-FS context.
 */
static inline void amdgpu_mes_lock(struct amdgpu_mes *mes)
{
	mutex_lock(&mes->mutex_hidden);
	mes->saved_flags = memalloc_noreclaim_save();
}

static inline void amdgpu_mes_unlock(struct amdgpu_mes *mes)
{
	memalloc_noreclaim_restore(mes->saved_flags);
	mutex_unlock(&mes->mutex_hidden);
}
#endif /* __AMDGPU_MES_H__ */
