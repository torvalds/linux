/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2019,2021 Advanced Micro Devices, Inc.
 *
 * Author: Rijo Thomas <Rijo-john.Thomas@amd.com>
 * Author: Devaraj Rangasamy <Devaraj.Rangasamy@amd.com>
 *
 */

/* This file describes the TEE communication interface between host and AMD
 * Secure Processor
 */

#ifndef __TEE_DEV_H__
#define __TEE_DEV_H__

#include <linux/device.h>
#include <linux/mutex.h>

#define TEE_DEFAULT_CMD_TIMEOUT		(10 * MSEC_PER_SEC)
#define TEE_DEFAULT_RING_TIMEOUT	10
#define MAX_BUFFER_SIZE			988

/**
 * struct tee_init_ring_cmd - Command to init TEE ring buffer
 * @low_addr:  bits [31:0] of the physical address of ring buffer
 * @hi_addr:   bits [63:32] of the physical address of ring buffer
 * @size:      size of ring buffer in bytes
 */
struct tee_init_ring_cmd {
	u32 low_addr;
	u32 hi_addr;
	u32 size;
};

#define MAX_RING_BUFFER_ENTRIES		32

/**
 * struct ring_buf_manager - Helper structure to manage ring buffer.
 * @ring_start:  starting address of ring buffer
 * @ring_size:   size of ring buffer in bytes
 * @ring_pa:     physical address of ring buffer
 * @wptr:        index to the last written entry in ring buffer
 */
struct ring_buf_manager {
	struct mutex mutex;	/* synchronizes access to ring buffer */
	void *ring_start;
	u32 ring_size;
	phys_addr_t ring_pa;
	u32 wptr;
};

struct psp_tee_device {
	struct device *dev;
	struct psp_device *psp;
	void __iomem *io_regs;
	struct tee_vdata *vdata;
	struct ring_buf_manager rb_mgr;
};

/**
 * enum tee_cmd_state - TEE command states for the ring buffer interface
 * @TEE_CMD_STATE_INIT:      initial state of command when sent from host
 * @TEE_CMD_STATE_PROCESS:   command being processed by TEE environment
 * @TEE_CMD_STATE_COMPLETED: command processing completed
 */
enum tee_cmd_state {
	TEE_CMD_STATE_INIT,
	TEE_CMD_STATE_PROCESS,
	TEE_CMD_STATE_COMPLETED,
};

/**
 * enum cmd_resp_state - TEE command's response status maintained by driver
 * @CMD_RESPONSE_INVALID:      initial state when no command is written to ring
 * @CMD_WAITING_FOR_RESPONSE:  driver waiting for response from TEE
 * @CMD_RESPONSE_TIMEDOUT:     failed to get response from TEE
 * @CMD_RESPONSE_COPIED:       driver has copied response from TEE
 */
enum cmd_resp_state {
	CMD_RESPONSE_INVALID,
	CMD_WAITING_FOR_RESPONSE,
	CMD_RESPONSE_TIMEDOUT,
	CMD_RESPONSE_COPIED,
};

/**
 * struct tee_ring_cmd - Structure of the command buffer in TEE ring
 * @cmd_id:      refers to &enum tee_cmd_id. Command id for the ring buffer
 *               interface
 * @cmd_state:   refers to &enum tee_cmd_state
 * @status:      status of TEE command execution
 * @res0:        reserved region
 * @pdata:       private data (currently unused)
 * @res1:        reserved region
 * @buf:         TEE command specific buffer
 * @flag:	 refers to &enum cmd_resp_state
 */
struct tee_ring_cmd {
	u32 cmd_id;
	u32 cmd_state;
	u32 status;
	u32 res0[1];
	u64 pdata;
	u32 res1[2];
	u8 buf[MAX_BUFFER_SIZE];
	u32 flag;

	/* Total size: 1024 bytes */
} __packed;

int tee_dev_init(struct psp_device *psp);
void tee_dev_destroy(struct psp_device *psp);

#endif /* __TEE_DEV_H__ */
