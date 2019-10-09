/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 */

#include <linux/debugfs.h>

#define PCIE_DMA_TABLE_NUM		24

#define PCIE_DMA_TRX_TYPE_NUM		3

#define PCIE_DMA_DATA_SND_TABLE_OFFSET		0x0
#define PCIE_DMA_DATA_RCV_ACK_TABLE_OFFSET	0x8
#define PCIE_DMA_DATA_FREE_ACK_TABLE_OFFSET	0x10

enum dma_dir {
	DMA_FROM_BUS,
	DMA_TO_BUS,
};

/**
 * The Channel Control Register for read and write.
 */
union chan_ctrl_lo {
	struct {
		u32	cb		:1;	// 0
		u32	tcb		:1;	// 1
		u32	llp		:1;	// 2
		u32	lie		:1;	// 3
		u32	rie		:1;	// 4
		u32	cs		:2;	// 5:6
		u32	rsvd1		:1;	// 7
		u32	ccs		:1;	// 8
		u32	llen		:1;	// 9
		u32	b_64s		:1;	// 10
		u32	b_64d		:1;	// 11
		u32	pf		:5;	// 12:16
		u32	rsvd2		:7;	// 17:23
		u32	sn		:1;	// 24
		u32	ro		:1;	// 25
		u32	td		:1;	// 26
		u32	tc		:3;	// 27:29
		u32	at		:2;	// 30:31
	};
	u32 asdword;
};

/**
 * The Channel Control Register high part for read and write.
 */
union chan_ctrl_hi {
	struct {
		u32	vfenb		:1;	// 0
		u32	vfunc		:8;	// 1-8
		u32	rsvd0		:23;	// 9-31
	};
	u32 asdword;
};

/**
 * The Channel Weight Register.
 */
union weight {
	struct {
		u32	weight0		:5;	// 0:4
		u32	weight1		:5;	// 5:9
		u32	weight2		:5;	// 10:14
		u32	weight3		:5;	// 15:19
		u32	rsvd		:12;	// 20:31
	};
	u32 asdword;
};

/**
 * The Doorbell Register for read and write.
 */
union db {
	struct {
		u32	chnl		:3;	// 0
		u32	reserved0	:28;	// 3:30
		u32	stop		:1;	// 31
	};
	u32 asdword;
};

/**
 * The Context Registers for read and write.
 */
struct ctx_regs {
	union chan_ctrl_lo		ctrllo;
	union chan_ctrl_hi		ctrlhi;
	u32				xfersize;
	u32				sarptrlo;
	u32				sarptrhi;
	u32				darptrlo;
	u32				darptrhi;
};

/**
 * The Enable Register for read and write.
 */
union enb {
	struct {
		u32	enb		:1;	// 0
		u32	reserved0	:31;	// 1:31
	};
	u32 asdword;
};

/**
 * The Interrupt Status Register for read and write.
 */
union int_status {
	struct {
		u32	donesta		:8;
		u32	rsvd0		:8;
		u32	abortsta	:8;
		u32	rsvd1		:8;
	};
	u32 asdword;
};

/**
 * The Interrupt Clear Register for read and write.
 */
union int_clear {
	struct {
		u32	doneclr		:8;
		u32	rsvd0		:8;
		u32	abortclr	:8;
		u32	rsvd1		:8;
	};
	u32 asdword;
};

struct dma_table {
	u32				*descs;
	int				chn;
	phys_addr_t			phys_descs;
	u32				dir;
	u32				type;
	struct list_head		tbl_node;
	union enb			wr_enb;
	struct ctx_regs			ctx_reg;
	union weight			wr_weilo;
	union weight			wr_weihi;
	union db			start;
};

struct dma_trx_obj {
	struct device			*dev;
	int				loop_count;
	int				loop_count_threshold;
	void				*mem_base;
	phys_addr_t			mem_start;
	size_t				mem_size;
	int				dma_free;
	unsigned long			local_write_available;
	unsigned long			local_read_available;
	unsigned long			remote_write_available;
	spinlock_t			tbl_list_lock; /* lock dma table */
	struct list_head		tbl_list;
	struct work_struct		dma_trx_work;
	wait_queue_head_t		event_queue;
	struct workqueue_struct		*dma_trx_wq;
	struct dma_table		*table[PCIE_DMA_TABLE_NUM];
	struct dma_table		*cur;
	struct task_struct		*scan_thread;
	struct hrtimer			scan_timer;
	int				busno;
	void				*priv;
	struct completion		done;
	unsigned long			irq_num;
	struct dentry			*pcie_root;
};

#ifdef CONFIG_PCIE_DW_ROCKCHIP
void rk_pcie_start_dma_1808(struct dma_trx_obj *obj);
#else
static inline void rk_pcie_start_dma_1808(struct dma_trx_obj *obj)
{
}
#endif

#ifdef CONFIG_PCIE_ROCKCHIP
void rk_pcie_start_dma_3399(struct dma_trx_obj *obj);
#else
static inline void rk_pcie_start_dma_3399(struct dma_trx_obj *obj)
{
}
#endif

#ifdef CONFIG_ROCKCHIP_PCIE_DMA_OBJ
struct dma_trx_obj *rk_pcie_dma_obj_probe(struct device *dev);
#else
static inline struct dma_trx_obj *rk_pcie_dma_obj_probe(struct device *dev)
{
	return NULL;
}
#endif
