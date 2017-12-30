/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more details.
 ***********************************************************************/
#ifndef __MAILBOX_H__
#define __MAILBOX_H__

/* Macros for Mail Box Communication */

#define OCTEON_MBOX_DATA_MAX		32

#define OCTEON_VF_ACTIVE		0x1
#define OCTEON_VF_FLR_REQUEST		0x2
#define OCTEON_PF_CHANGED_VF_MACADDR	0x4

/*Macro for Read acknowldgement*/
#define OCTEON_PFVFACK			0xffffffffffffffffULL
#define OCTEON_PFVFSIG			0x1122334455667788ULL
#define OCTEON_PFVFERR			0xDEADDEADDEADDEADULL

#define LIO_MBOX_WRITE_WAIT_CNT         1000
#define LIO_MBOX_WRITE_WAIT_TIME        msecs_to_jiffies(1)

enum octeon_mbox_cmd_status {
	OCTEON_MBOX_STATUS_SUCCESS = 0,
	OCTEON_MBOX_STATUS_FAILED = 1,
	OCTEON_MBOX_STATUS_BUSY = 2
};

enum octeon_mbox_message_type {
	OCTEON_MBOX_REQUEST = 0,
	OCTEON_MBOX_RESPONSE = 1
};

union octeon_mbox_message {
	u64 u64;
	struct {
		u16 type : 1;
		u16 resp_needed : 1;
		u16 cmd : 6;
		u16 len : 8;
		u8 params[6];
	} s;
};

typedef void (*octeon_mbox_callback_t)(void *, void *, void *);

struct octeon_mbox_cmd {
	union octeon_mbox_message msg;
	u64 data[OCTEON_MBOX_DATA_MAX];
	u32 q_no;
	u32 recv_len;
	u32 recv_status;
	octeon_mbox_callback_t fn;
	void *fn_arg;
};

enum octeon_mbox_state {
	OCTEON_MBOX_STATE_IDLE = 1,
	OCTEON_MBOX_STATE_REQUEST_RECEIVING = 2,
	OCTEON_MBOX_STATE_REQUEST_RECEIVED = 4,
	OCTEON_MBOX_STATE_RESPONSE_PENDING = 8,
	OCTEON_MBOX_STATE_RESPONSE_RECEIVING = 16,
	OCTEON_MBOX_STATE_RESPONSE_RECEIVED = 32,
	OCTEON_MBOX_STATE_ERROR = 64
};

struct octeon_mbox {
	/** A spinlock to protect access to this q_mbox. */
	spinlock_t lock;

	struct octeon_device *oct_dev;

	u32 q_no;

	enum octeon_mbox_state state;

	struct cavium_wk mbox_poll_wk;

	/** SLI_MAC_PF_MBOX_INT for PF, SLI_PKT_MBOX_INT for VF. */
	void *mbox_int_reg;

	/** SLI_PKT_PF_VF_MBOX_SIG(0) for PF, SLI_PKT_PF_VF_MBOX_SIG(1) for VF.
	 */
	void *mbox_write_reg;

	/** SLI_PKT_PF_VF_MBOX_SIG(1) for PF, SLI_PKT_PF_VF_MBOX_SIG(0) for VF.
	 */
	void *mbox_read_reg;

	struct octeon_mbox_cmd mbox_req;

	struct octeon_mbox_cmd mbox_resp;

};

int octeon_mbox_read(struct octeon_mbox *mbox);
int octeon_mbox_write(struct octeon_device *oct,
		      struct octeon_mbox_cmd *mbox_cmd);
int octeon_mbox_process_message(struct octeon_mbox *mbox);

#endif
