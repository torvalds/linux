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
#include "kfd_priv.h"
#include "kfd_mqd_manager.h"

#define QUEUE_PREEMPT_DEFAULT_TIMEOUT_MS	(500)
#define QUEUES_PER_PIPE				(8)
#define PIPE_PER_ME_CP_SCHEDULING		(3)
#define CIK_VMID_NUM				(8)
#define KFD_VMID_START_OFFSET			(8)
#define VMID_PER_DEVICE				CIK_VMID_NUM
#define KFD_DQM_FIRST_PIPE			(0)

struct device_process_node {
	struct qcm_process_device *qpd;
	struct list_head list;
};

/**
 * struct device_queue_manager
 *
 * @create_queue: Queue creation routine.
 *
 * @destroy_queue: Queue destruction routine.
 *
 * @update_queue: Queue update routine.
 *
 * @get_mqd_manager: Returns the mqd manager according to the mqd type.
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
 * This struct is a base class for the kfd queues scheduler in the
 * device level. The device base class should expose the basic operations
 * for queue creation and queue destruction. This base class hides the
 * scheduling mode of the driver and the specific implementation of the
 * concrete device. This class is the only class in the queues scheduler
 * that configures the H/W.
 */

struct device_queue_manager {
	int	(*create_queue)(struct device_queue_manager *dqm,
				struct queue *q,
				struct qcm_process_device *qpd,
				int *allocate_vmid);
	int	(*destroy_queue)(struct device_queue_manager *dqm,
				struct qcm_process_device *qpd,
				struct queue *q);
	int	(*update_queue)(struct device_queue_manager *dqm,
				struct queue *q);

	struct mqd_manager * (*get_mqd_manager)
					(struct device_queue_manager *dqm,
					enum KFD_MQD_TYPE type);

	int	(*register_process)(struct device_queue_manager *dqm,
					struct qcm_process_device *qpd);
	int	(*unregister_process)(struct device_queue_manager *dqm,
					struct qcm_process_device *qpd);
	int	(*initialize)(struct device_queue_manager *dqm);
	int	(*start)(struct device_queue_manager *dqm);
	int	(*stop)(struct device_queue_manager *dqm);
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


	struct mqd_manager	*mqds[KFD_MQD_TYPE_MAX];
	struct packet_manager	packets;
	struct kfd_dev		*dev;
	struct mutex		lock;
	struct list_head	queues;
	unsigned int		processes_count;
	unsigned int		queue_count;
	unsigned int		next_pipe_to_allocate;
	unsigned int		*allocated_queues;
	unsigned int		vmid_bitmap;
	uint64_t		pipelines_addr;
	struct kfd_mem_obj	*pipeline_mem;
	uint64_t		fence_gpu_addr;
	unsigned int		*fence_addr;
	struct kfd_mem_obj	*fence_mem;
	bool			active_runlist;
};



#endif /* KFD_DEVICE_QUEUE_MANAGER_H_ */
