/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2015-2018, Intel Corporation.
 */

#ifndef __KCS_BMC_H__
#define __KCS_BMC_H__

#include <linux/miscdevice.h>

/* Different phases of the KCS BMC module.
 *  KCS_PHASE_IDLE:
 *            BMC should not be expecting nor sending any data.
 *  KCS_PHASE_WRITE_START:
 *            BMC is receiving a WRITE_START command from system software.
 *  KCS_PHASE_WRITE_DATA:
 *            BMC is receiving a data byte from system software.
 *  KCS_PHASE_WRITE_END_CMD:
 *            BMC is waiting a last data byte from system software.
 *  KCS_PHASE_WRITE_DONE:
 *            BMC has received the whole request from system software.
 *  KCS_PHASE_WAIT_READ:
 *            BMC is waiting the response from the upper IPMI service.
 *  KCS_PHASE_READ:
 *            BMC is transferring the response to system software.
 *  KCS_PHASE_ABORT_ERROR1:
 *            BMC is waiting error status request from system software.
 *  KCS_PHASE_ABORT_ERROR2:
 *            BMC is waiting for idle status afer error from system software.
 *  KCS_PHASE_ERROR:
 *            BMC has detected a protocol violation at the interface level.
 */
enum kcs_phases {
	KCS_PHASE_IDLE,

	KCS_PHASE_WRITE_START,
	KCS_PHASE_WRITE_DATA,
	KCS_PHASE_WRITE_END_CMD,
	KCS_PHASE_WRITE_DONE,

	KCS_PHASE_WAIT_READ,
	KCS_PHASE_READ,

	KCS_PHASE_ABORT_ERROR1,
	KCS_PHASE_ABORT_ERROR2,
	KCS_PHASE_ERROR
};

/* IPMI 2.0 - Table 9-4, KCS Interface Status Codes */
enum kcs_errors {
	KCS_NO_ERROR                = 0x00,
	KCS_ABORTED_BY_COMMAND      = 0x01,
	KCS_ILLEGAL_CONTROL_CODE    = 0x02,
	KCS_LENGTH_ERROR            = 0x06,
	KCS_UNSPECIFIED_ERROR       = 0xFF
};

/* IPMI 2.0 - 9.5, KCS Interface Registers
 * @idr: Input Data Register
 * @odr: Output Data Register
 * @str: Status Register
 */
struct kcs_ioreg {
	u32 idr;
	u32 odr;
	u32 str;
};

struct kcs_bmc {
	spinlock_t lock;

	u32 channel;
	int running;

	/* Setup by BMC KCS controller driver */
	struct kcs_ioreg ioreg;
	u8 (*io_inputb)(struct kcs_bmc *kcs_bmc, u32 reg);
	void (*io_outputb)(struct kcs_bmc *kcs_bmc, u32 reg, u8 b);

	enum kcs_phases phase;
	enum kcs_errors error;

	wait_queue_head_t queue;
	bool data_in_avail;
	int  data_in_idx;
	u8  *data_in;

	int  data_out_idx;
	int  data_out_len;
	u8  *data_out;

	struct mutex mutex;
	u8 *kbuffer;

	struct miscdevice miscdev;

	unsigned long priv[];
};

static inline void *kcs_bmc_priv(struct kcs_bmc *kcs_bmc)
{
	return kcs_bmc->priv;
}

int kcs_bmc_handle_event(struct kcs_bmc *kcs_bmc);
struct kcs_bmc *kcs_bmc_alloc(struct device *dev, int sizeof_priv,
					u32 channel);
#endif /* __KCS_BMC_H__ */
