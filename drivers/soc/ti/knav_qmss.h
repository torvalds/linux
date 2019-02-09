/*
 * Keystone Navigator QMSS driver internal header
 *
 * Copyright (C) 2014 Texas Instruments Incorporated - http://www.ti.com
 * Author:	Sandeep Nair <sandeep_n@ti.com>
 *		Cyril Chemparathy <cyril@ti.com>
 *		Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __KNAV_QMSS_H__
#define __KNAV_QMSS_H__

#include <linux/percpu.h>

#define THRESH_GTE	BIT(7)
#define THRESH_LT	0

#define PDSP_CTRL_PC_MASK	0xffff0000
#define PDSP_CTRL_SOFT_RESET	BIT(0)
#define PDSP_CTRL_ENABLE	BIT(1)
#define PDSP_CTRL_RUNNING	BIT(15)

#define ACC_MAX_CHANNEL		48
#define ACC_DEFAULT_PERIOD	25 /* usecs */

#define ACC_CHANNEL_INT_BASE		2

#define ACC_LIST_ENTRY_TYPE		1
#define ACC_LIST_ENTRY_WORDS		(1 << ACC_LIST_ENTRY_TYPE)
#define ACC_LIST_ENTRY_QUEUE_IDX	0
#define ACC_LIST_ENTRY_DESC_IDX	(ACC_LIST_ENTRY_WORDS - 1)

#define ACC_CMD_DISABLE_CHANNEL	0x80
#define ACC_CMD_ENABLE_CHANNEL	0x81
#define ACC_CFG_MULTI_QUEUE		BIT(21)

#define ACC_INTD_OFFSET_EOI		(0x0010)
#define ACC_INTD_OFFSET_COUNT(ch)	(0x0300 + 4 * (ch))
#define ACC_INTD_OFFSET_STATUS(ch)	(0x0200 + 4 * ((ch) / 32))

#define RANGE_MAX_IRQS			64

#define ACC_DESCS_MAX		SZ_1K
#define ACC_DESCS_MASK		(ACC_DESCS_MAX - 1)
#define DESC_SIZE_MASK		0xful
#define DESC_PTR_MASK		(~DESC_SIZE_MASK)

#define KNAV_NAME_SIZE			32

enum knav_acc_result {
	ACC_RET_IDLE,
	ACC_RET_SUCCESS,
	ACC_RET_INVALID_COMMAND,
	ACC_RET_INVALID_CHANNEL,
	ACC_RET_INACTIVE_CHANNEL,
	ACC_RET_ACTIVE_CHANNEL,
	ACC_RET_INVALID_QUEUE,
	ACC_RET_INVALID_RET,
};

struct knav_reg_config {
	u32		revision;
	u32		__pad1;
	u32		divert;
	u32		link_ram_base0;
	u32		link_ram_size0;
	u32		link_ram_base1;
	u32		__pad2[2];
	u32		starvation[0];
};

struct knav_reg_region {
	u32		base;
	u32		start_index;
	u32		size_count;
	u32		__pad;
};

struct knav_reg_pdsp_regs {
	u32		control;
	u32		status;
	u32		cycle_count;
	u32		stall_count;
};

struct knav_reg_acc_command {
	u32		command;
	u32		queue_mask;
	u32		list_dma;
	u32		queue_num;
	u32		timer_config;
};

struct knav_link_ram_block {
	dma_addr_t	 dma;
	void		*virt;
	size_t		 size;
};

struct knav_acc_info {
	u32			 pdsp_id;
	u32			 start_channel;
	u32			 list_entries;
	u32			 pacing_mode;
	u32			 timer_count;
	int			 mem_size;
	int			 list_size;
	struct knav_pdsp_info	*pdsp;
};

struct knav_acc_channel {
	u32			channel;
	u32			list_index;
	u32			open_mask;
	u32			*list_cpu[2];
	dma_addr_t		list_dma[2];
	char			name[KNAV_NAME_SIZE];
	atomic_t		retrigger_count;
};

struct knav_pdsp_info {
	const char					*name;
	struct knav_reg_pdsp_regs  __iomem		*regs;
	union {
		void __iomem				*command;
		struct knav_reg_acc_command __iomem	*acc_command;
		u32 __iomem				*qos_command;
	};
	void __iomem					*intd;
	u32 __iomem					*iram;
	u32						id;
	struct list_head				list;
	bool						loaded;
	bool						started;
};

struct knav_qmgr_info {
	unsigned			start_queue;
	unsigned			num_queues;
	struct knav_reg_config __iomem	*reg_config;
	struct knav_reg_region __iomem	*reg_region;
	struct knav_reg_queue __iomem	*reg_push, *reg_pop, *reg_peek;
	void __iomem			*reg_status;
	struct list_head		list;
};

#define KNAV_NUM_LINKRAM	2

/**
 * struct knav_queue_stats:	queue statistics
 * pushes:			number of push operations
 * pops:			number of pop operations
 * push_errors:			number of push errors
 * pop_errors:			number of pop errors
 * notifies:			notifier counts
 */
struct knav_queue_stats {
	unsigned int pushes;
	unsigned int pops;
	unsigned int push_errors;
	unsigned int pop_errors;
	unsigned int notifies;
};

/**
 * struct knav_reg_queue:	queue registers
 * @entry_count:		valid entries in the queue
 * @byte_count:			total byte count in thhe queue
 * @packet_size:		packet size for the queue
 * @ptr_size_thresh:		packet pointer size threshold
 */
struct knav_reg_queue {
	u32		entry_count;
	u32		byte_count;
	u32		packet_size;
	u32		ptr_size_thresh;
};

/**
 * struct knav_region:		qmss region info
 * @dma_start, dma_end:		start and end dma address
 * @virt_start, virt_end:	start and end virtual address
 * @desc_size:			descriptor size
 * @used_desc:			consumed descriptors
 * @id:				region number
 * @num_desc:			total descriptors
 * @link_index:			index of the first descriptor
 * @name:			region name
 * @list:			instance in the device's region list
 * @pools:			list of descriptor pools in the region
 */
struct knav_region {
	dma_addr_t		dma_start, dma_end;
	void			*virt_start, *virt_end;
	unsigned		desc_size;
	unsigned		used_desc;
	unsigned		id;
	unsigned		num_desc;
	unsigned		link_index;
	const char		*name;
	struct list_head	list;
	struct list_head	pools;
};

/**
 * struct knav_pool:		qmss pools
 * @dev:			device pointer
 * @region:			qmss region info
 * @queue:			queue registers
 * @kdev:			qmss device pointer
 * @region_offset:		offset from the base
 * @num_desc:			total descriptors
 * @desc_size:			descriptor size
 * @region_id:			region number
 * @name:			pool name
 * @list:			list head
 * @region_inst:		instance in the region's pool list
 */
struct knav_pool {
	struct device			*dev;
	struct knav_region		*region;
	struct knav_queue		*queue;
	struct knav_device		*kdev;
	int				region_offset;
	int				num_desc;
	int				desc_size;
	int				region_id;
	const char			*name;
	struct list_head		list;
	struct list_head		region_inst;
};

/**
 * struct knav_queue_inst:		qmss queue instace properties
 * @descs:				descriptor pointer
 * @desc_head, desc_tail, desc_count:	descriptor counters
 * @acc:				accumulator channel pointer
 * @kdev:				qmss device pointer
 * @range:				range info
 * @qmgr:				queue manager info
 * @id:					queue instace id
 * @irq_num:				irq line number
 * @notify_needed:			notifier needed based on queue type
 * @num_notifiers:			total notifiers
 * @handles:				list head
 * @name:				queue instance name
 * @irq_name:				irq line name
 */
struct knav_queue_inst {
	u32				*descs;
	atomic_t			desc_head, desc_tail, desc_count;
	struct knav_acc_channel	*acc;
	struct knav_device		*kdev;
	struct knav_range_info		*range;
	struct knav_qmgr_info		*qmgr;
	u32				id;
	int				irq_num;
	int				notify_needed;
	atomic_t			num_notifiers;
	struct list_head		handles;
	const char			*name;
	const char			*irq_name;
};

/**
 * struct knav_queue:			qmss queue properties
 * @reg_push, reg_pop, reg_peek:	push, pop queue registers
 * @inst:				qmss queue instace properties
 * @notifier_fn:			notifier function
 * @notifier_fn_arg:			notifier function argument
 * @notifier_enabled:			notier enabled for a give queue
 * @rcu:				rcu head
 * @flags:				queue flags
 * @list:				list head
 */
struct knav_queue {
	struct knav_reg_queue __iomem	*reg_push, *reg_pop, *reg_peek;
	struct knav_queue_inst		*inst;
	struct knav_queue_stats __percpu	*stats;
	knav_queue_notify_fn		notifier_fn;
	void				*notifier_fn_arg;
	atomic_t			notifier_enabled;
	struct rcu_head			rcu;
	unsigned			flags;
	struct list_head		list;
};

enum qmss_version {
	QMSS,
	QMSS_66AK2G,
};

struct knav_device {
	struct device				*dev;
	unsigned				base_id;
	unsigned				num_queues;
	unsigned				num_queues_in_use;
	unsigned				inst_shift;
	struct knav_link_ram_block		link_rams[KNAV_NUM_LINKRAM];
	void					*instances;
	struct list_head			regions;
	struct list_head			queue_ranges;
	struct list_head			pools;
	struct list_head			pdsps;
	struct list_head			qmgrs;
	enum qmss_version			version;
};

struct knav_range_ops {
	int	(*init_range)(struct knav_range_info *range);
	int	(*free_range)(struct knav_range_info *range);
	int	(*init_queue)(struct knav_range_info *range,
			      struct knav_queue_inst *inst);
	int	(*open_queue)(struct knav_range_info *range,
			      struct knav_queue_inst *inst, unsigned flags);
	int	(*close_queue)(struct knav_range_info *range,
			       struct knav_queue_inst *inst);
	int	(*set_notify)(struct knav_range_info *range,
			      struct knav_queue_inst *inst, bool enabled);
};

struct knav_irq_info {
	int		irq;
	struct cpumask	*cpu_mask;
};

struct knav_range_info {
	const char			*name;
	struct knav_device		*kdev;
	unsigned			queue_base;
	unsigned			num_queues;
	void				*queue_base_inst;
	unsigned			flags;
	struct list_head		list;
	struct knav_range_ops		*ops;
	struct knav_acc_info		acc_info;
	struct knav_acc_channel	*acc;
	unsigned			num_irqs;
	struct knav_irq_info		irqs[RANGE_MAX_IRQS];
};

#define RANGE_RESERVED		BIT(0)
#define RANGE_HAS_IRQ		BIT(1)
#define RANGE_HAS_ACCUMULATOR	BIT(2)
#define RANGE_MULTI_QUEUE	BIT(3)

#define for_each_region(kdev, region)				\
	list_for_each_entry(region, &kdev->regions, list)

#define first_region(kdev)					\
	list_first_entry_or_null(&kdev->regions, \
				 struct knav_region, list)

#define for_each_queue_range(kdev, range)			\
	list_for_each_entry(range, &kdev->queue_ranges, list)

#define first_queue_range(kdev)					\
	list_first_entry_or_null(&kdev->queue_ranges, \
				 struct knav_range_info, list)

#define for_each_pool(kdev, pool)				\
	list_for_each_entry(pool, &kdev->pools, list)

#define for_each_pdsp(kdev, pdsp)				\
	list_for_each_entry(pdsp, &kdev->pdsps, list)

#define for_each_qmgr(kdev, qmgr)				\
	list_for_each_entry(qmgr, &kdev->qmgrs, list)

static inline struct knav_pdsp_info *
knav_find_pdsp(struct knav_device *kdev, unsigned pdsp_id)
{
	struct knav_pdsp_info *pdsp;

	for_each_pdsp(kdev, pdsp)
		if (pdsp_id == pdsp->id)
			return pdsp;
	return NULL;
}

extern int knav_init_acc_range(struct knav_device *kdev,
					struct device_node *node,
					struct knav_range_info *range);
extern void knav_queue_notify(struct knav_queue_inst *inst);

#endif /* __KNAV_QMSS_H__ */
