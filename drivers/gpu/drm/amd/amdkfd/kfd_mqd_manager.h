/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#ifndef KFD_MQD_MANAGER_H_
#define KFD_MQD_MANAGER_H_

#include "kfd_priv.h"

#define KFD_MAX_NUM_SE 8

/**
 * struct mqd_manager
 *
 * @init_mqd: Allocates the mqd buffer on local gpu memory and initialize it.
 *
 * @load_mqd: Loads the mqd to a concrete hqd slot. Used only for no cp
 * scheduling mode.
 *
 * @update_mqd: Handles a update call for the MQD
 *
 * @destroy_mqd: Destroys the HQD slot and by that preempt the relevant queue.
 * Used only for no cp scheduling.
 *
 * @free_mqd: Releases the mqd buffer from local gpu memory.
 *
 * @is_occupied: Checks if the relevant HQD slot is occupied.
 *
 * @get_wave_state: Retrieves context save state and optionally copies the
 * control stack, if kept in the MQD, to the given userspace address.
 *
 * @mqd_mutex: Mqd manager mutex.
 *
 * @dev: The kfd device structure coupled with this module.
 *
 * MQD stands for Memory Queue Descriptor which represents the current queue
 * state in the memory and initiate the HQD (Hardware Queue Descriptor) state.
 * This structure is actually a base class for the different types of MQDs
 * structures for the variant ASICs that should be supported in the future.
 * This base class is also contains all the MQD specific operations.
 * Another important thing to mention is that each queue has a MQD that keeps
 * his state (or context) after each preemption or reassignment.
 * Basically there are a instances of the mqd manager class per MQD type per
 * ASIC. Currently the kfd driver supports only Kaveri so there are instances
 * per KFD_MQD_TYPE for each device.
 *
 */
extern int pipe_priority_map[];
struct mqd_manager {
	struct kfd_mem_obj*	(*allocate_mqd)(struct kfd_dev *kfd,
		struct queue_properties *q);

	void	(*init_mqd)(struct mqd_manager *mm, void **mqd,
			struct kfd_mem_obj *mqd_mem_obj, uint64_t *gart_addr,
			struct queue_properties *q);

	int	(*load_mqd)(struct mqd_manager *mm, void *mqd,
				uint32_t pipe_id, uint32_t queue_id,
				struct queue_properties *p,
				struct mm_struct *mms);

	void	(*update_mqd)(struct mqd_manager *mm, void *mqd,
				struct queue_properties *q);

	int	(*destroy_mqd)(struct mqd_manager *mm, void *mqd,
				enum kfd_preempt_type type,
				unsigned int timeout, uint32_t pipe_id,
				uint32_t queue_id);

	void	(*free_mqd)(struct mqd_manager *mm, void *mqd,
				struct kfd_mem_obj *mqd_mem_obj);

	bool	(*is_occupied)(struct mqd_manager *mm, void *mqd,
				uint64_t queue_address,	uint32_t pipe_id,
				uint32_t queue_id);

	int	(*get_wave_state)(struct mqd_manager *mm, void *mqd,
				  void __user *ctl_stack,
				  u32 *ctl_stack_used_size,
				  u32 *save_area_used_size);

#if defined(CONFIG_DEBUG_FS)
	int	(*debugfs_show_mqd)(struct seq_file *m, void *data);
#endif
	uint32_t (*read_doorbell_id)(void *mqd);

	struct mutex	mqd_mutex;
	struct kfd_dev	*dev;
	uint32_t mqd_size;
};

struct kfd_mem_obj *allocate_hiq_mqd(struct kfd_dev *dev,
				struct queue_properties *q);

struct kfd_mem_obj *allocate_sdma_mqd(struct kfd_dev *dev,
					struct queue_properties *q);
void free_mqd_hiq_sdma(struct mqd_manager *mm, void *mqd,
				struct kfd_mem_obj *mqd_mem_obj);

void mqd_symmetrically_map_cu_mask(struct mqd_manager *mm,
		const uint32_t *cu_mask, uint32_t cu_mask_count,
		uint32_t *se_mask);

#endif /* KFD_MQD_MANAGER_H_ */
