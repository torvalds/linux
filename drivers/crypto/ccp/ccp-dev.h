/*
 * AMD Cryptographic Coprocessor (CCP) driver
 *
 * Copyright (C) 2013 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __CCP_DEV_H__
#define __CCP_DEV_H__

#include <linux/device.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/dmapool.h>
#include <linux/hw_random.h>


#define MAX_DMAPOOL_NAME_LEN		32

#define MAX_HW_QUEUES			5
#define MAX_CMD_QLEN			100

#define TRNG_RETRIES			10

#define CACHE_NONE			0x00
#define CACHE_WB_NO_ALLOC		0xb7


/****** Register Mappings ******/
#define Q_MASK_REG			0x000
#define TRNG_OUT_REG			0x00c
#define IRQ_MASK_REG			0x040
#define IRQ_STATUS_REG			0x200

#define DEL_CMD_Q_JOB			0x124
#define DEL_Q_ACTIVE			0x00000200
#define DEL_Q_ID_SHIFT			6

#define CMD_REQ0			0x180
#define CMD_REQ_INCR			0x04

#define CMD_Q_STATUS_BASE		0x210
#define CMD_Q_INT_STATUS_BASE		0x214
#define CMD_Q_STATUS_INCR		0x20

#define CMD_Q_CACHE_BASE		0x228
#define CMD_Q_CACHE_INC			0x20

#define CMD_Q_ERROR(__qs)		((__qs) & 0x0000003f);
#define CMD_Q_DEPTH(__qs)		(((__qs) >> 12) & 0x0000000f);

/****** REQ0 Related Values ******/
#define REQ0_WAIT_FOR_WRITE		0x00000004
#define REQ0_INT_ON_COMPLETE		0x00000002
#define REQ0_STOP_ON_COMPLETE		0x00000001

#define REQ0_CMD_Q_SHIFT		9
#define REQ0_JOBID_SHIFT		3

/****** REQ1 Related Values ******/
#define REQ1_PROTECT_SHIFT		27
#define REQ1_ENGINE_SHIFT		23
#define REQ1_KEY_KSB_SHIFT		2

#define REQ1_EOM			0x00000002
#define REQ1_INIT			0x00000001

/* AES Related Values */
#define REQ1_AES_TYPE_SHIFT		21
#define REQ1_AES_MODE_SHIFT		18
#define REQ1_AES_ACTION_SHIFT		17
#define REQ1_AES_CFB_SIZE_SHIFT		10

/* XTS-AES Related Values */
#define REQ1_XTS_AES_SIZE_SHIFT		10

/* SHA Related Values */
#define REQ1_SHA_TYPE_SHIFT		21

/* RSA Related Values */
#define REQ1_RSA_MOD_SIZE_SHIFT		10

/* Pass-Through Related Values */
#define REQ1_PT_BW_SHIFT		12
#define REQ1_PT_BS_SHIFT		10

/* ECC Related Values */
#define REQ1_ECC_AFFINE_CONVERT		0x00200000
#define REQ1_ECC_FUNCTION_SHIFT		18

/****** REQ4 Related Values ******/
#define REQ4_KSB_SHIFT			18
#define REQ4_MEMTYPE_SHIFT		16

/****** REQ6 Related Values ******/
#define REQ6_MEMTYPE_SHIFT		16


/****** Key Storage Block ******/
#define KSB_START			77
#define KSB_END				127
#define KSB_COUNT			(KSB_END - KSB_START + 1)
#define CCP_KSB_BITS			256
#define CCP_KSB_BYTES			32

#define CCP_JOBID_MASK			0x0000003f

#define CCP_DMAPOOL_MAX_SIZE		64
#define CCP_DMAPOOL_ALIGN		(1 << 5)

#define CCP_REVERSE_BUF_SIZE		64

#define CCP_AES_KEY_KSB_COUNT		1
#define CCP_AES_CTX_KSB_COUNT		1

#define CCP_XTS_AES_KEY_KSB_COUNT	1
#define CCP_XTS_AES_CTX_KSB_COUNT	1

#define CCP_SHA_KSB_COUNT		1

#define CCP_RSA_MAX_WIDTH		4096

#define CCP_PASSTHRU_BLOCKSIZE		256
#define CCP_PASSTHRU_MASKSIZE		32
#define CCP_PASSTHRU_KSB_COUNT		1

#define CCP_ECC_MODULUS_BYTES		48      /* 384-bits */
#define CCP_ECC_MAX_OPERANDS		6
#define CCP_ECC_MAX_OUTPUTS		3
#define CCP_ECC_SRC_BUF_SIZE		448
#define CCP_ECC_DST_BUF_SIZE		192
#define CCP_ECC_OPERAND_SIZE		64
#define CCP_ECC_OUTPUT_SIZE		64
#define CCP_ECC_RESULT_OFFSET		60
#define CCP_ECC_RESULT_SUCCESS		0x0001


struct ccp_device;
struct ccp_cmd;

struct ccp_cmd_queue {
	struct ccp_device *ccp;

	/* Queue identifier */
	u32 id;

	/* Queue dma pool */
	struct dma_pool *dma_pool;

	/* Queue reserved KSB regions */
	u32 ksb_key;
	u32 ksb_ctx;

	/* Queue processing thread */
	struct task_struct *kthread;
	unsigned int active;
	unsigned int suspended;

	/* Number of free command slots available */
	unsigned int free_slots;

	/* Interrupt masks */
	u32 int_ok;
	u32 int_err;

	/* Register addresses for queue */
	void __iomem *reg_status;
	void __iomem *reg_int_status;

	/* Status values from job */
	u32 int_status;
	u32 q_status;
	u32 q_int_status;
	u32 cmd_error;

	/* Interrupt wait queue */
	wait_queue_head_t int_queue;
	unsigned int int_rcvd;
} ____cacheline_aligned;

struct ccp_device {
	struct device *dev;

	/*
	 * Bus specific device information
	 */
	void *dev_specific;
	int (*get_irq)(struct ccp_device *ccp);
	void (*free_irq)(struct ccp_device *ccp);
	unsigned int irq;

	/*
	 * I/O area used for device communication. The register mapping
	 * starts at an offset into the mapped bar.
	 *   The CMD_REQx registers and the Delete_Cmd_Queue_Job register
	 *   need to be protected while a command queue thread is accessing
	 *   them.
	 */
	struct mutex req_mutex ____cacheline_aligned;
	void __iomem *io_map;
	void __iomem *io_regs;

	/*
	 * Master lists that all cmds are queued on. Because there can be
	 * more than one CCP command queue that can process a cmd a separate
	 * backlog list is neeeded so that the backlog completion call
	 * completes before the cmd is available for execution.
	 */
	spinlock_t cmd_lock ____cacheline_aligned;
	unsigned int cmd_count;
	struct list_head cmd;
	struct list_head backlog;

	/*
	 * The command queues. These represent the queues available on the
	 * CCP that are available for processing cmds
	 */
	struct ccp_cmd_queue cmd_q[MAX_HW_QUEUES];
	unsigned int cmd_q_count;

	/*
	 * Support for the CCP True RNG
	 */
	struct hwrng hwrng;
	unsigned int hwrng_retries;

	/*
	 * A counter used to generate job-ids for cmds submitted to the CCP
	 */
	atomic_t current_id ____cacheline_aligned;

	/*
	 * The CCP uses key storage blocks (KSB) to maintain context for certain
	 * operations. To prevent multiple cmds from using the same KSB range
	 * a command queue reserves a KSB range for the duration of the cmd.
	 * Each queue, will however, reserve 2 KSB blocks for operations that
	 * only require single KSB entries (eg. AES context/iv and key) in order
	 * to avoid allocation contention.  This will reserve at most 10 KSB
	 * entries, leaving 40 KSB entries available for dynamic allocation.
	 */
	struct mutex ksb_mutex ____cacheline_aligned;
	DECLARE_BITMAP(ksb, KSB_COUNT);
	wait_queue_head_t ksb_queue;
	unsigned int ksb_avail;
	unsigned int ksb_count;
	u32 ksb_start;

	/* Suspend support */
	unsigned int suspending;
	wait_queue_head_t suspend_queue;

	/* DMA caching attribute support */
	unsigned int axcache;
};


int ccp_pci_init(void);
void ccp_pci_exit(void);

int ccp_platform_init(void);
void ccp_platform_exit(void);

struct ccp_device *ccp_alloc_struct(struct device *dev);
int ccp_init(struct ccp_device *ccp);
void ccp_destroy(struct ccp_device *ccp);
bool ccp_queues_suspended(struct ccp_device *ccp);

irqreturn_t ccp_irq_handler(int irq, void *data);

int ccp_run_cmd(struct ccp_cmd_queue *cmd_q, struct ccp_cmd *cmd);

#endif
