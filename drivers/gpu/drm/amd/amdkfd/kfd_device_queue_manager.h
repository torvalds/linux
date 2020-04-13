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

#ifndef KFD_DEVICE_QUEUE_MANAGER_H_
#define KFD_DEVICE_QUEUE_MANAGER_H_

#include <linux/rwsem.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/sched/mm.h>
#include "kfd_priv.h"
#include "kfd_mqd_manager.h"


#define VMID_NUM 16

struct device_process_node {
	struct qcm_process_device *qpd;
	struct list_head list;
};

/**
 * struct device_queue_manager_ops
 *
 * @create_queue: Queue creation routine.
 *
 * @destroy_queue: Queue destruction routine.
 *
 * @update_queue: Queue update routine.
 *
 * @exeute_queues: Dispatches the queues list to the H/W.
 *
 * @register_process: This routine associates a specific process with device.
 *
 * @unregister_process: destroys the associations between process to device.
 *
 * @initialize: Initializes the pipelines and memory module for that device.
 *
 * @start: Initializes the resources/modules the the device needs for queues
 * execution. This function is called on device initialization and after the
 * system woke up after suspension.
 *
 * @stop: This routine stops execution of all the active queue running on the
 * H/W and basically this function called on system suspend.
 *
 * @uninitialize: Destroys all the device queue manager resources allocated in
 * initialize routine.
 *
 * @create_kernel_queue: Creates kernel queue. Used for debug queue.
 *
 * @destroy_kernel_queue: Destroys kernel queue. Used for debug queue.
 *
 * @set_cache_memory_policy: Sets memory policy (cached/ non cached) for the
 * memory apertures.
 *
 * @process_termination: Clears all process queues belongs to that device.
 *
 * @evict_process_queues: Evict all active queues of a process
 *
 * @restore_process_queues: Restore all evicted queues queues of a process
 *
 * @get_wave_state: Retrieves context save state and optionally copies the
 * control stack, if kept in the MQD, to the given userspace address.
 */

struct device_queue_manager_ops {
	int	(*create_queue)(struct device_queue_manager *dqm,
				struct queue *q,
				struct qcm_process_device *qpd);

	int	(*destroy_queue)(struct device_queue_manager *dqm,
				struct qcm_process_device *qpd,
				struct queue *q);

	int	(*update_queue)(struct device_queue_manager *dqm,
				struct queue *q);

	int	(*register_process)(struct device_queue_manager *dqm,
					struct qcm_process_device *qpd);

	int	(*unregister_process)(struct device_queue_manager *dqm,
					struct qcm_process_device *qpd);

	int	(*initialize)(struct device_queue_manager *dqm);
	int	(*start)(struct device_queue_manager *dqm);
	int	(*stop)(struct device_queue_manager *dqm);
	void	(*pre_reset)(struct device_queue_manager *dqm);
	void	(*uninitialize)(struct device_queue_manager *dqm);
	int	(*create_kernel_queue)(struct device_queue_manager *dqm,
					struct kernel_queue *kq,
					struct qcm_process_device *qpd);

	void	(*destroy_kernel_queue)(struct device_queue_manager *dqm,
					struct kernel_queue *kq,
					struct qcm_process_device *qpd);

	bool	(*set_cache_memory_policy)(struct device_queue_manager *dqm,
					   struct qcm_process_device *qpd,
					   enum cache_policy default_policy,
					   enum cache_policy alternate_policy,
					   void __user *alternate_aperture_base,
					   uint64_t alternate_aperture_size);

	int	(*set_trap_handler)(struct device_queue_manager *dqm,
				    struct qcm_process_device *qpd,
				    uint64_t tba_addr,
				    uint64_t tma_addr);

	int (*process_termination)(struct device_queue_manager *dqm,
			struct qcm_process_device *qpd);

	int (*evict_process_queues)(struct device_queue_manager *dqm,
				    struct qcm_process_device *qpd);
	int (*restore_process_queues)(struct device_queue_manager *dqm,
				      struct qcm_process_device *qpd);

	int	(*get_wave_state)(struct device_queue_manager *dqm,
				  struct queue *q,
				  void __user *ctl_stack,
				  u32 *ctl_stack_used_size,
				  u32 *save_area_used_size);
};

struct device_queue_manager_asic_ops {
	int	(*update_qpd)(struct device_queue_manager *dqm,
					struct qcm_process_device *qpd);
	bool	(*set_cache_memory_policy)(struct device_queue_manager *dqm,
					   struct qcm_process_device *qpd,
					   enum cache_policy default_policy,
					   enum cache_policy alternate_policy,
					   void __user *alternate_aperture_base,
					   uint64_t alternate_aperture_size);
	void	(*init_sdma_vm)(struct device_queue_manager *dqm,
				struct queue *q,
				struct qcm_process_device *qpd);
	struct mqd_manager *	(*mqd_manager_init)(enum KFD_MQD_TYPE type,
				 struct kfd_dev *dev);
};

/**
 * struct device_queue_manager
 *
 * This struct is a base class for the kfd queues scheduler in the
 * device level. The device base class should expose the basic operations
 * for queue creation and queue destruction. This base class hides the
 * scheduling mode of the driver and the specific implementation of the
 * concrete device. This class is the only class in the queues scheduler
 * that configures the H/W.
 *
 */

struct device_queue_manager {
	struct device_queue_manager_ops ops;
	struct device_queue_manager_asic_ops asic_ops;

	struct mqd_manager	*mqd_mgrs[KFD_MQD_TYPE_MAX];
	struct packet_manager	packets;
	struct kfd_dev		*dev;
	struct mutex		lock_hidden; /* use dqm_lock/unlock(dqm) */
	struct list_head	queues;
	unsigned int		saved_flags;
	unsigned int		processes_count;
	unsigned int		active_queue_count;
	unsigned int		active_cp_queue_count;
	unsigned int		total_queue_count;
	unsigned int		next_pipe_to_allocate;
	unsigned int		*allocated_queues;
	uint64_t		sdma_bitmap;
	uint64_t		xgmi_sdma_bitmap;
	/* the pasid mapping for each kfd vmid */
	uint16_t		vmid_pasid[VMID_NUM];
	uint64_t		pipelines_addr;
	uint64_t		fence_gpu_addr;
	unsigned int		*fence_addr;
	struct kfd_mem_obj	*fence_mem;
	bool			active_runlist;
	int			sched_policy;

	/* hw exception  */
	bool			is_hws_hang;
	bool			is_resetting;
	struct work_struct	hw_exception_work;
	struct kfd_mem_obj	hiq_sdma_mqd;
	bool			sched_running;
};

void device_queue_manager_init_cik(
		struct device_queue_manager_asic_ops *asic_ops);
void device_queue_manager_init_cik_hawaii(
		struct device_queue_manager_asic_ops *asic_ops);
void device_queue_manager_init_vi(
		struct device_queue_manager_asic_ops *asic_ops);
void device_queue_manager_init_vi_tonga(
		struct device_queue_manager_asic_ops *asic_ops);
void device_queue_manager_init_v9(
		struct device_queue_manager_asic_ops *asic_ops);
void device_queue_manager_init_v10_navi10(
		struct device_queue_manager_asic_ops *asic_ops);
void program_sh_mem_settings(struct device_queue_manager *dqm,
					struct qcm_process_device *qpd);
unsigned int get_cp_queues_num(struct device_queue_manager *dqm);
unsigned int get_queues_per_pipe(struct device_queue_manager *dqm);
unsigned int get_pipes_per_mec(struct device_queue_manager *dqm);
unsigned int get_num_sdma_queues(struct device_queue_manager *dqm);
unsigned int get_num_xgmi_sdma_queues(struct device_queue_manager *dqm);

static inline unsigned int get_sh_mem_bases_32(struct kfd_process_device *pdd)
{
	return (pdd->lds_base >> 16) & 0xFF;
}

static inline unsigned int
get_sh_mem_bases_nybble_64(struct kfd_process_device *pdd)
{
	return (pdd->lds_base >> 60) & 0x0E;
}

/* The DQM lock can be taken in MMU notifiers. Make sure no reclaim-FS
 * happens while holding this lock anywhere to prevent deadlocks when
 * an MMU notifier runs in reclaim-FS context.
 */
static inline void dqm_lock(struct device_queue_manager *dqm)
{
	mutex_lock(&dqm->lock_hidden);
	dqm->saved_flags = memalloc_nofs_save();
}
static inline void dqm_unlock(struct device_queue_manager *dqm)
{
	memalloc_nofs_restore(dqm->saved_flags);
	mutex_unlock(&dqm->lock_hidden);
}

#endif /* KFD_DEVICE_QUEUE_MANAGER_H_ */
