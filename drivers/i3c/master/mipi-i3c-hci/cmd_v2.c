// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2020, MIPI Alliance, Inc.
 *
 * Author: Nicolas Pitre <npitre@baylibre.com>
 *
 * I3C HCI v2.0 Command Descriptor Handling
 *
 * Note: The I3C HCI v2.0 spec is still in flux. The code here will change.
 */

#include <linux/bitfield.h>
#include <linux/i3c/master.h>

#include "hci.h"
#include "cmd.h"
#include "xfer_mode_rate.h"


/*
 * Unified Data Transfer Command
 */

#define CMD_0_ATTR_U			FIELD_PREP(CMD_0_ATTR, 0x4)

#define CMD_U3_HDR_TSP_ML_CTRL(v)	FIELD_PREP(W3_MASK(107, 104), v)
#define CMD_U3_IDB4(v)			FIELD_PREP(W3_MASK(103,  96), v)
#define CMD_U3_HDR_CMD(v)		FIELD_PREP(W3_MASK(103,  96), v)
#define CMD_U2_IDB3(v)			FIELD_PREP(W2_MASK( 95,  88), v)
#define CMD_U2_HDR_BT(v)		FIELD_PREP(W2_MASK( 95,  88), v)
#define CMD_U2_IDB2(v)			FIELD_PREP(W2_MASK( 87,  80), v)
#define CMD_U2_BT_CMD2(v)		FIELD_PREP(W2_MASK( 87,  80), v)
#define CMD_U2_IDB1(v)			FIELD_PREP(W2_MASK( 79,  72), v)
#define CMD_U2_BT_CMD1(v)		FIELD_PREP(W2_MASK( 79,  72), v)
#define CMD_U2_IDB0(v)			FIELD_PREP(W2_MASK( 71,  64), v)
#define CMD_U2_BT_CMD0(v)		FIELD_PREP(W2_MASK( 71,  64), v)
#define CMD_U1_ERR_HANDLING(v)		FIELD_PREP(W1_MASK( 63,  62), v)
#define CMD_U1_ADD_FUNC(v)		FIELD_PREP(W1_MASK( 61,  56), v)
#define CMD_U1_COMBO_XFER			   W1_BIT_( 55)
#define CMD_U1_DATA_LENGTH(v)		FIELD_PREP(W1_MASK( 53,  32), v)
#define CMD_U0_TOC				   W0_BIT_( 31)
#define CMD_U0_ROC				   W0_BIT_( 30)
#define CMD_U0_MAY_YIELD			   W0_BIT_( 29)
#define CMD_U0_NACK_RCNT(v)		FIELD_PREP(W0_MASK( 28,  27), v)
#define CMD_U0_IDB_COUNT(v)		FIELD_PREP(W0_MASK( 26,  24), v)
#define CMD_U0_MODE_INDEX(v)		FIELD_PREP(W0_MASK( 22,  18), v)
#define CMD_U0_XFER_RATE(v)		FIELD_PREP(W0_MASK( 17,  15), v)
#define CMD_U0_DEV_ADDRESS(v)		FIELD_PREP(W0_MASK( 14,   8), v)
#define CMD_U0_RnW				   W0_BIT_(  7)
#define CMD_U0_TID(v)			FIELD_PREP(W0_MASK(  6,   3), v)

/*
 * Address Assignment Command
 */

#define CMD_0_ATTR_A			FIELD_PREP(CMD_0_ATTR, 0x2)

#define CMD_A1_DATA_LENGTH(v)		FIELD_PREP(W1_MASK( 53,  32), v)
#define CMD_A0_TOC				   W0_BIT_( 31)
#define CMD_A0_ROC				   W0_BIT_( 30)
#define CMD_A0_XFER_RATE(v)		FIELD_PREP(W0_MASK( 17,  15), v)
#define CMD_A0_ASSIGN_ADDRESS(v)	FIELD_PREP(W0_MASK( 14,   8), v)
#define CMD_A0_TID(v)			FIELD_PREP(W0_MASK(  6,   3), v)


static unsigned int get_i3c_rate_idx(struct i3c_hci *hci)
{
	struct i3c_bus *bus = i3c_master_get_bus(&hci->master);

	if (bus->scl_rate.i3c >= 12000000)
		return XFERRATE_I3C_SDR0;
	if (bus->scl_rate.i3c > 8000000)
		return XFERRATE_I3C_SDR1;
	if (bus->scl_rate.i3c > 6000000)
		return XFERRATE_I3C_SDR2;
	if (bus->scl_rate.i3c > 4000000)
		return XFERRATE_I3C_SDR3;
	if (bus->scl_rate.i3c > 2000000)
		return XFERRATE_I3C_SDR4;
	return XFERRATE_I3C_SDR_FM_FMP;
}

static unsigned int get_i2c_rate_idx(struct i3c_hci *hci)
{
	struct i3c_bus *bus = i3c_master_get_bus(&hci->master);

	if (bus->scl_rate.i2c >= 1000000)
		return XFERRATE_I2C_FMP;
	return XFERRATE_I2C_FM;
}

static void hci_cmd_v2_prep_private_xfer(struct i3c_hci *hci,
					 struct hci_xfer *xfer,
					 u8 addr, unsigned int mode,
					 unsigned int rate)
{
	u8 *data = xfer->data;
	unsigned int data_len = xfer->data_len;
	bool rnw = xfer->rnw;

	xfer->cmd_tid = hci_get_tid();

	if (!rnw && data_len <= 5) {
		xfer->cmd_desc[0] =
			CMD_0_ATTR_U |
			CMD_U0_TID(xfer->cmd_tid) |
			CMD_U0_DEV_ADDRESS(addr) |
			CMD_U0_XFER_RATE(rate) |
			CMD_U0_MODE_INDEX(mode) |
			CMD_U0_IDB_COUNT(data_len);
		xfer->cmd_desc[1] =
			CMD_U1_DATA_LENGTH(0);
		xfer->cmd_desc[2] = 0;
		xfer->cmd_desc[3] = 0;
		switch (data_len) {
		case 5:
			xfer->cmd_desc[3] |= CMD_U3_IDB4(data[4]);
			fallthrough;
		case 4:
			xfer->cmd_desc[2] |= CMD_U2_IDB3(data[3]);
			fallthrough;
		case 3:
			xfer->cmd_desc[2] |= CMD_U2_IDB2(data[2]);
			fallthrough;
		case 2:
			xfer->cmd_desc[2] |= CMD_U2_IDB1(data[1]);
			fallthrough;
		case 1:
			xfer->cmd_desc[2] |= CMD_U2_IDB0(data[0]);
			fallthrough;
		case 0:
			break;
		}
		/* we consumed all the data with the cmd descriptor */
		xfer->data = NULL;
	} else {
		xfer->cmd_desc[0] =
			CMD_0_ATTR_U |
			CMD_U0_TID(xfer->cmd_tid) |
			(rnw ? CMD_U0_RnW : 0) |
			CMD_U0_DEV_ADDRESS(addr) |
			CMD_U0_XFER_RATE(rate) |
			CMD_U0_MODE_INDEX(mode);
		xfer->cmd_desc[1] =
			CMD_U1_DATA_LENGTH(data_len);
		xfer->cmd_desc[2] = 0;
		xfer->cmd_desc[3] = 0;
	}
}

static int hci_cmd_v2_prep_ccc(struct i3c_hci *hci, struct hci_xfer *xfer,
			       u8 ccc_addr, u8 ccc_cmd, bool raw)
{
	unsigned int mode = XFERMODE_IDX_I3C_SDR;
	unsigned int rate = get_i3c_rate_idx(hci);
	u8 *data = xfer->data;
	unsigned int data_len = xfer->data_len;
	bool rnw = xfer->rnw;

	if (raw && ccc_addr != I3C_BROADCAST_ADDR) {
		hci_cmd_v2_prep_private_xfer(hci, xfer, ccc_addr, mode, rate);
		return 0;
	}

	xfer->cmd_tid = hci_get_tid();

	if (!rnw && data_len <= 4) {
		xfer->cmd_desc[0] =
			CMD_0_ATTR_U |
			CMD_U0_TID(xfer->cmd_tid) |
			CMD_U0_DEV_ADDRESS(ccc_addr) |
			CMD_U0_XFER_RATE(rate) |
			CMD_U0_MODE_INDEX(mode) |
			CMD_U0_IDB_COUNT(data_len + (!raw ? 0 : 1));
		xfer->cmd_desc[1] =
			CMD_U1_DATA_LENGTH(0);
		xfer->cmd_desc[2] =
			CMD_U2_IDB0(ccc_cmd);
		xfer->cmd_desc[3] = 0;
		switch (data_len) {
		case 4:
			xfer->cmd_desc[3] |= CMD_U3_IDB4(data[3]);
			fallthrough;
		case 3:
			xfer->cmd_desc[2] |= CMD_U2_IDB3(data[2]);
			fallthrough;
		case 2:
			xfer->cmd_desc[2] |= CMD_U2_IDB2(data[1]);
			fallthrough;
		case 1:
			xfer->cmd_desc[2] |= CMD_U2_IDB1(data[0]);
			fallthrough;
		case 0:
			break;
		}
		/* we consumed all the data with the cmd descriptor */
		xfer->data = NULL;
	} else {
		xfer->cmd_desc[0] =
			CMD_0_ATTR_U |
			CMD_U0_TID(xfer->cmd_tid) |
			(rnw ? CMD_U0_RnW : 0) |
			CMD_U0_DEV_ADDRESS(ccc_addr) |
			CMD_U0_XFER_RATE(rate) |
			CMD_U0_MODE_INDEX(mode) |
			CMD_U0_IDB_COUNT(!raw ? 0 : 1);
		xfer->cmd_desc[1] =
			CMD_U1_DATA_LENGTH(data_len);
		xfer->cmd_desc[2] =
			CMD_U2_IDB0(ccc_cmd);
		xfer->cmd_desc[3] = 0;
	}

	return 0;
}

static void hci_cmd_v2_prep_i3c_xfer(struct i3c_hci *hci,
				     struct i3c_dev_desc *dev,
				     struct hci_xfer *xfer)
{
	unsigned int mode = XFERMODE_IDX_I3C_SDR;
	unsigned int rate = get_i3c_rate_idx(hci);
	u8 addr = dev->info.dyn_addr;

	hci_cmd_v2_prep_private_xfer(hci, xfer, addr, mode, rate);
}

static void hci_cmd_v2_prep_i2c_xfer(struct i3c_hci *hci,
				     struct i2c_dev_desc *dev,
				     struct hci_xfer *xfer)
{
	unsigned int mode = XFERMODE_IDX_I2C;
	unsigned int rate = get_i2c_rate_idx(hci);
	u8 addr = dev->addr;

	hci_cmd_v2_prep_private_xfer(hci, xfer, addr, mode, rate);
}

static int hci_cmd_v2_daa(struct i3c_hci *hci)
{
	struct hci_xfer *xfer;
	int ret;
	u8 next_addr = 0;
	u32 device_id[2];
	u64 pid;
	unsigned int dcr, bcr;
	DECLARE_COMPLETION_ONSTACK(done);

	xfer = hci_alloc_xfer(2);
	if (!xfer)
		return -ENOMEM;

	xfer[0].data = &device_id;
	xfer[0].data_len = 8;
	xfer[0].rnw = true;
	xfer[0].cmd_desc[1] = CMD_A1_DATA_LENGTH(8);
	xfer[1].completion = &done;

	for (;;) {
		ret = i3c_master_get_free_addr(&hci->master, next_addr);
		if (ret < 0)
			break;
		next_addr = ret;
		dev_dbg(&hci->master.dev, "next_addr = 0x%02x", next_addr);
		xfer[0].cmd_tid = hci_get_tid();
		xfer[0].cmd_desc[0] =
			CMD_0_ATTR_A |
			CMD_A0_TID(xfer[0].cmd_tid) |
			CMD_A0_ROC;
		xfer[1].cmd_tid = hci_get_tid();
		xfer[1].cmd_desc[0] =
			CMD_0_ATTR_A |
			CMD_A0_TID(xfer[1].cmd_tid) |
			CMD_A0_ASSIGN_ADDRESS(next_addr) |
			CMD_A0_ROC |
			CMD_A0_TOC;
		hci->io->queue_xfer(hci, xfer, 2);
		if (!wait_for_completion_timeout(&done, HZ) &&
		    hci->io->dequeue_xfer(hci, xfer, 2)) {
			ret = -ETIME;
			break;
		}
		if (RESP_STATUS(xfer[0].response) != RESP_SUCCESS) {
			ret = 0;  /* no more devices to be assigned */
			break;
		}
		if (RESP_STATUS(xfer[1].response) != RESP_SUCCESS) {
			ret = -EIO;
			break;
		}

		pid = FIELD_GET(W1_MASK(47, 32), device_id[1]);
		pid = (pid << 32) | device_id[0];
		bcr = FIELD_GET(W1_MASK(55, 48), device_id[1]);
		dcr = FIELD_GET(W1_MASK(63, 56), device_id[1]);
		dev_dbg(&hci->master.dev,
			"assigned address %#x to device PID=0x%llx DCR=%#x BCR=%#x",
			next_addr, pid, dcr, bcr);
		/*
		 * TODO: Extend the subsystem layer to allow for registering
		 * new device and provide BCR/DCR/PID at the same time.
		 */
		ret = i3c_master_add_i3c_dev_locked(&hci->master, next_addr);
		if (ret)
			break;
	}

	hci_free_xfer(xfer, 2);
	return ret;
}

const struct hci_cmd_ops mipi_i3c_hci_cmd_v2 = {
	.prep_ccc		= hci_cmd_v2_prep_ccc,
	.prep_i3c_xfer		= hci_cmd_v2_prep_i3c_xfer,
	.prep_i2c_xfer		= hci_cmd_v2_prep_i2c_xfer,
	.perform_daa		= hci_cmd_v2_daa,
};
