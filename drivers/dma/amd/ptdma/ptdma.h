/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AMD Passthru DMA device driver
 * -- Based on the CCP driver
 *
 * Copyright (C) 2016,2021 Advanced Micro Devices, Inc.
 *
 * Author: Sanjay R Mehta <sanju.mehta@amd.com>
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 * Author: Gary R Hook <gary.hook@amd.com>
 */

#ifndef __PT_DEV_H__
#define __PT_DEV_H__

#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/dmapool.h>

#include "../../virt-dma.h"

#define MAX_PT_NAME_LEN			16
#define MAX_DMAPOOL_NAME_LEN		32

#define MAX_HW_QUEUES			1
#define MAX_CMD_QLEN			100

#define PT_ENGINE_PASSTHRU		5

/* Register Mappings */
#define IRQ_MASK_REG			0x040
#define IRQ_STATUS_REG			0x200

#define CMD_Q_ERROR(__qs)		((__qs) & 0x0000003f)

#define CMD_QUEUE_PRIO_OFFSET		0x00
#define CMD_REQID_CONFIG_OFFSET		0x04
#define CMD_TIMEOUT_OFFSET		0x08
#define CMD_PT_VERSION			0x10

#define CMD_Q_CONTROL_BASE		0x0000
#define CMD_Q_TAIL_LO_BASE		0x0004
#define CMD_Q_HEAD_LO_BASE		0x0008
#define CMD_Q_INT_ENABLE_BASE		0x000C
#define CMD_Q_INTERRUPT_STATUS_BASE	0x0010

#define CMD_Q_STATUS_BASE		0x0100
#define CMD_Q_INT_STATUS_BASE		0x0104
#define CMD_Q_DMA_STATUS_BASE		0x0108
#define CMD_Q_DMA_READ_STATUS_BASE	0x010C
#define CMD_Q_DMA_WRITE_STATUS_BASE	0x0110
#define CMD_Q_ABORT_BASE		0x0114
#define CMD_Q_AX_CACHE_BASE		0x0118

#define CMD_CONFIG_OFFSET		0x1120
#define CMD_CLK_GATE_CTL_OFFSET		0x6004

#define CMD_DESC_DW0_VAL		0x500012

/* Address offset for virtual queue registers */
#define CMD_Q_STATUS_INCR		0x1000

/* Bit masks */
#define CMD_CONFIG_REQID		0
#define CMD_TIMEOUT_DISABLE		0
#define CMD_CLK_DYN_GATING_DIS		0
#define CMD_CLK_SW_GATE_MODE		0
#define CMD_CLK_GATE_CTL		0
#define CMD_QUEUE_PRIO			GENMASK(2, 1)
#define CMD_CONFIG_VHB_EN		BIT(0)
#define CMD_CLK_DYN_GATING_EN		BIT(0)
#define CMD_CLK_HW_GATE_MODE		BIT(0)
#define CMD_CLK_GATE_ON_DELAY		BIT(12)
#define CMD_CLK_GATE_OFF_DELAY		BIT(12)

#define CMD_CLK_GATE_CONFIG		(CMD_CLK_GATE_CTL | \
					CMD_CLK_HW_GATE_MODE | \
					CMD_CLK_GATE_ON_DELAY | \
					CMD_CLK_DYN_GATING_EN | \
					CMD_CLK_GATE_OFF_DELAY)

#define CMD_Q_LEN			32
#define CMD_Q_RUN			BIT(0)
#define CMD_Q_HALT			BIT(1)
#define CMD_Q_MEM_LOCATION		BIT(2)
#define CMD_Q_SIZE_MASK			GENMASK(4, 0)
#define CMD_Q_SIZE			GENMASK(7, 3)
#define CMD_Q_SHIFT			GENMASK(1, 0)
#define QUEUE_SIZE_VAL			((ffs(CMD_Q_LEN) - 2) & \
								  CMD_Q_SIZE_MASK)
#define Q_PTR_MASK			(2 << (QUEUE_SIZE_VAL + 5) - 1)
#define Q_DESC_SIZE			sizeof(struct ptdma_desc)
#define Q_SIZE(n)			(CMD_Q_LEN * (n))

#define INT_COMPLETION			BIT(0)
#define INT_ERROR			BIT(1)
#define INT_QUEUE_STOPPED		BIT(2)
#define INT_EMPTY_QUEUE			BIT(3)
#define SUPPORTED_INTERRUPTS		(INT_COMPLETION | INT_ERROR)

/****** Local Storage Block ******/
#define LSB_START			0
#define LSB_END				127
#define LSB_COUNT			(LSB_END - LSB_START + 1)

#define PT_DMAPOOL_MAX_SIZE		64
#define PT_DMAPOOL_ALIGN		BIT(5)

#define PT_PASSTHRU_BLOCKSIZE		512

struct pt_device;

struct pt_tasklet_data {
	struct completion completion;
	struct pt_cmd *cmd;
};

/*
 * struct pt_passthru_engine - pass-through operation
 *   without performing DMA mapping
 * @mask: mask to be applied to data
 * @mask_len: length in bytes of mask
 * @src_dma: data to be used for this operation
 * @dst_dma: data produced by this operation
 * @src_len: length in bytes of data used for this operation
 *
 * Variables required to be set when calling pt_enqueue_cmd():
 *   - bit_mod, byte_swap, src, dst, src_len
 *   - mask, mask_len if bit_mod is not PT_PASSTHRU_BITWISE_NOOP
 */
struct pt_passthru_engine {
	dma_addr_t mask;
	u32 mask_len;		/* In bytes */

	dma_addr_t src_dma, dst_dma;
	u64 src_len;		/* In bytes */
};

/*
 * struct pt_cmd - PTDMA operation request
 * @entry: list element
 * @work: work element used for callbacks
 * @pt: PT device to be run on
 * @ret: operation return code
 * @flags: cmd processing flags
 * @engine: PTDMA operation to perform (passthru)
 * @engine_error: PT engine return code
 * @passthru: engine specific structures, refer to specific engine struct below
 * @callback: operation completion callback function
 * @data: parameter value to be supplied to the callback function
 *
 * Variables required to be set when calling pt_enqueue_cmd():
 *   - engine, callback
 *   - See the operation structures below for what is required for each
 *     operation.
 */
struct pt_cmd {
	struct list_head entry;
	struct work_struct work;
	struct pt_device *pt;
	int ret;
	u32 engine;
	u32 engine_error;
	struct pt_passthru_engine passthru;
	/* Completion callback support */
	void (*pt_cmd_callback)(void *data, int err);
	void *data;
};

struct pt_dma_desc {
	struct virt_dma_desc vd;
	struct pt_device *pt;
	enum dma_status status;
	size_t len;
	bool issued_to_hw;
	struct pt_cmd pt_cmd;
};

struct pt_dma_chan {
	struct virt_dma_chan vc;
	struct pt_device *pt;
	u32 id;
};

struct pt_cmd_queue {
	struct pt_device *pt;

	/* Queue dma pool */
	struct dma_pool *dma_pool;

	/* Queue base address (not necessarily aligned)*/
	struct ptdma_desc *qbase;

	/* Aligned queue start address (per requirement) */
	spinlock_t q_lock ____cacheline_aligned;
	unsigned int qidx;

	unsigned int qsize;
	dma_addr_t qbase_dma;
	dma_addr_t qdma_tail;

	unsigned int active;
	unsigned int suspended;

	/* Interrupt flag */
	bool int_en;

	/* Register addresses for queue */
	void __iomem *reg_control;
	u32 qcontrol; /* Cached control register */

	/* Status values from job */
	u32 int_status;
	u32 q_status;
	u32 q_int_status;
	u32 cmd_error;
	/* Queue Statistics */
	unsigned long total_pt_ops;
} ____cacheline_aligned;

struct pt_device {
	struct list_head entry;

	unsigned int ord;
	char name[MAX_PT_NAME_LEN];

	struct device *dev;

	/* Bus specific device information */
	struct pt_msix *pt_msix;

	struct pt_dev_vdata *dev_vdata;

	unsigned int pt_irq;

	/* I/O area used for device communication */
	void __iomem *io_regs;

	spinlock_t cmd_lock ____cacheline_aligned;
	unsigned int cmd_count;
	struct list_head cmd;

	/*
	 * The command queue. This represent the queue available on the
	 * PTDMA that are available for processing cmds
	 */
	struct pt_cmd_queue cmd_q;

	/* Support for the DMA Engine capabilities */
	struct dma_device dma_dev;
	struct pt_dma_chan *pt_dma_chan;
	struct kmem_cache *dma_desc_cache;

	wait_queue_head_t lsb_queue;

	/* Device Statistics */
	unsigned long total_interrupts;

	struct pt_tasklet_data tdata;
	int ver;
};

/*
 * descriptor for PTDMA commands
 * 8 32-bit words:
 * word 0: function; engine; control bits
 * word 1: length of source data
 * word 2: low 32 bits of source pointer
 * word 3: upper 16 bits of source pointer; source memory type
 * word 4: low 32 bits of destination pointer
 * word 5: upper 16 bits of destination pointer; destination memory type
 * word 6: reserved 32 bits
 * word 7: reserved 32 bits
 */

#define DWORD0_SOC	BIT(0)
#define DWORD0_IOC	BIT(1)

struct dword3 {
	unsigned int  src_hi:16;
	unsigned int  src_mem:2;
	unsigned int  lsb_cxt_id:8;
	unsigned int  rsvd1:5;
	unsigned int  fixed:1;
};

struct dword5 {
	unsigned int  dst_hi:16;
	unsigned int  dst_mem:2;
	unsigned int  rsvd1:13;
	unsigned int  fixed:1;
};

struct ptdma_desc {
	u32 dw0;
	u32 length;
	u32 src_lo;
	struct dword3 dw3;
	u32 dst_lo;
	struct dword5 dw5;
	__le32 rsvd1;
	__le32 rsvd2;
};

/* Structure to hold PT device data */
struct pt_dev_vdata {
	const unsigned int bar;
};

int pt_dmaengine_register(struct pt_device *pt);
void pt_dmaengine_unregister(struct pt_device *pt);

void ptdma_debugfs_setup(struct pt_device *pt);
int pt_core_init(struct pt_device *pt);
void pt_core_destroy(struct pt_device *pt);

int pt_core_perform_passthru(struct pt_cmd_queue *cmd_q,
			     struct pt_passthru_engine *pt_engine);

void pt_check_status_trans(struct pt_device *pt, struct pt_cmd_queue *cmd_q);
void pt_start_queue(struct pt_cmd_queue *cmd_q);
void pt_stop_queue(struct pt_cmd_queue *cmd_q);

static inline void pt_core_disable_queue_interrupts(struct pt_device *pt)
{
	iowrite32(0, pt->cmd_q.reg_control + 0x000C);
}

static inline void pt_core_enable_queue_interrupts(struct pt_device *pt)
{
	iowrite32(SUPPORTED_INTERRUPTS, pt->cmd_q.reg_control + 0x000C);
}
#endif
