// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2020, MIPI Alliance, Inc.
 *
 * Author: Nicolas Pitre <npitre@baylibre.com>
 *
 * I3C HCI v1.0/v1.1 Command Descriptor Handling
 */

#include <linux/bitfield.h>
#include <linux/i3c/master.h>

#include "hci.h"
#include "cmd.h"
#include "dat.h"
#include "dct.h"


/*
 * Address Assignment Command
 */

#define CMD_0_ATTR_A			FIELD_PREP(CMD_0_ATTR, 0x2)

#define CMD_A0_TOC				   W0_BIT_(31)
#define CMD_A0_ROC				   W0_BIT_(30)
#define CMD_A0_DEV_COUNT(v)		FIELD_PREP(W0_MASK(29, 26), v)
#define CMD_A0_DEV_INDEX(v)		FIELD_PREP(W0_MASK(20, 16), v)
#define CMD_A0_CMD(v)			FIELD_PREP(W0_MASK(14,  7), v)
#define CMD_A0_TID(v)			FIELD_PREP(W0_MASK( 6,  3), v)

/*
 * Immediate Data Transfer Command
 */

#define CMD_0_ATTR_I			FIELD_PREP(CMD_0_ATTR, 0x1)

#define CMD_I1_DATA_BYTE_4(v)		FIELD_PREP(W1_MASK(63, 56), v)
#define CMD_I1_DATA_BYTE_3(v)		FIELD_PREP(W1_MASK(55, 48), v)
#define CMD_I1_DATA_BYTE_2(v)		FIELD_PREP(W1_MASK(47, 40), v)
#define CMD_I1_DATA_BYTE_1(v)		FIELD_PREP(W1_MASK(39, 32), v)
#define CMD_I1_DEF_BYTE(v)		FIELD_PREP(W1_MASK(39, 32), v)
#define CMD_I0_TOC				   W0_BIT_(31)
#define CMD_I0_ROC				   W0_BIT_(30)
#define CMD_I0_RNW				   W0_BIT_(29)
#define CMD_I0_MODE(v)			FIELD_PREP(W0_MASK(28, 26), v)
#define CMD_I0_DTT(v)			FIELD_PREP(W0_MASK(25, 23), v)
#define CMD_I0_DEV_INDEX(v)		FIELD_PREP(W0_MASK(20, 16), v)
#define CMD_I0_CP				   W0_BIT_(15)
#define CMD_I0_CMD(v)			FIELD_PREP(W0_MASK(14,  7), v)
#define CMD_I0_TID(v)			FIELD_PREP(W0_MASK( 6,  3), v)

/*
 * Regular Data Transfer Command
 */

#define CMD_0_ATTR_R			FIELD_PREP(CMD_0_ATTR, 0x0)

#define CMD_R1_DATA_LENGTH(v)		FIELD_PREP(W1_MASK(63, 48), v)
#define CMD_R1_DEF_BYTE(v)		FIELD_PREP(W1_MASK(39, 32), v)
#define CMD_R0_TOC				   W0_BIT_(31)
#define CMD_R0_ROC				   W0_BIT_(30)
#define CMD_R0_RNW				   W0_BIT_(29)
#define CMD_R0_MODE(v)			FIELD_PREP(W0_MASK(28, 26), v)
#define CMD_R0_DBP				   W0_BIT_(25)
#define CMD_R0_DEV_INDEX(v)		FIELD_PREP(W0_MASK(20, 16), v)
#define CMD_R0_CP				   W0_BIT_(15)
#define CMD_R0_CMD(v)			FIELD_PREP(W0_MASK(14,  7), v)
#define CMD_R0_TID(v)			FIELD_PREP(W0_MASK( 6,  3), v)

/*
 * Combo Transfer (Write + Write/Read) Command
 */

#define CMD_0_ATTR_C			FIELD_PREP(CMD_0_ATTR, 0x3)

#define CMD_C1_DATA_LENGTH(v)		FIELD_PREP(W1_MASK(63, 48), v)
#define CMD_C1_OFFSET(v)		FIELD_PREP(W1_MASK(47, 32), v)
#define CMD_C0_TOC				   W0_BIT_(31)
#define CMD_C0_ROC				   W0_BIT_(30)
#define CMD_C0_RNW				   W0_BIT_(29)
#define CMD_C0_MODE(v)			FIELD_PREP(W0_MASK(28, 26), v)
#define CMD_C0_16_BIT_SUBOFFSET			   W0_BIT_(25)
#define CMD_C0_FIRST_PHASE_MODE			   W0_BIT_(24)
#define CMD_C0_DATA_LENGTH_POSITION(v)	FIELD_PREP(W0_MASK(23, 22), v)
#define CMD_C0_DEV_INDEX(v)		FIELD_PREP(W0_MASK(20, 16), v)
#define CMD_C0_CP				   W0_BIT_(15)
#define CMD_C0_CMD(v)			FIELD_PREP(W0_MASK(14,  7), v)
#define CMD_C0_TID(v)			FIELD_PREP(W0_MASK( 6,  3), v)

/*
 * Internal Control Command
 */

#define CMD_0_ATTR_M			FIELD_PREP(CMD_0_ATTR, 0x7)

#define CMD_M1_VENDOR_SPECIFIC			   W1_MASK(63, 32)
#define CMD_M0_MIPI_RESERVED			   W0_MASK(31, 12)
#define CMD_M0_MIPI_CMD				   W0_MASK(11,  8)
#define CMD_M0_VENDOR_INFO_PRESENT		   W0_BIT_( 7)
#define CMD_M0_TID(v)			FIELD_PREP(W0_MASK( 6,  3), v)


/* Data Transfer Speed and Mode */
enum hci_cmd_mode {
	MODE_I3C_SDR0		= 0x0,
	MODE_I3C_SDR1		= 0x1,
	MODE_I3C_SDR2		= 0x2,
	MODE_I3C_SDR3		= 0x3,
	MODE_I3C_SDR4		= 0x4,
	MODE_I3C_HDR_TSx	= 0x5,
	MODE_I3C_HDR_DDR	= 0x6,
	MODE_I3C_HDR_BT		= 0x7,
	MODE_I3C_Fm_FmP		= 0x8,
	MODE_I2C_Fm		= 0x0,
	MODE_I2C_FmP		= 0x1,
	MODE_I2C_UD1		= 0x2,
	MODE_I2C_UD2		= 0x3,
	MODE_I2C_UD3		= 0x4,
};

static enum hci_cmd_mode get_i3c_mode(struct i3c_hci *hci)
{
	struct i3c_bus *bus = i3c_master_get_bus(&hci->master);

	if (bus->scl_rate.i3c >= 12500000)
		return MODE_I3C_SDR0;
	if (bus->scl_rate.i3c > 8000000)
		return MODE_I3C_SDR1;
	if (bus->scl_rate.i3c > 6000000)
		return MODE_I3C_SDR2;
	if (bus->scl_rate.i3c > 4000000)
		return MODE_I3C_SDR3;
	if (bus->scl_rate.i3c > 2000000)
		return MODE_I3C_SDR4;
	return MODE_I3C_Fm_FmP;
}

static enum hci_cmd_mode get_i2c_mode(struct i3c_hci *hci)
{
	struct i3c_bus *bus = i3c_master_get_bus(&hci->master);

	if (bus->scl_rate.i2c >= 1000000)
		return MODE_I2C_FmP;
	return MODE_I2C_Fm;
}

static void fill_data_bytes(struct hci_xfer *xfer, u8 *data,
			    unsigned int data_len)
{
	xfer->cmd_desc[1] = 0;
	switch (data_len) {
	case 4:
		xfer->cmd_desc[1] |= CMD_I1_DATA_BYTE_4(data[3]);
		fallthrough;
	case 3:
		xfer->cmd_desc[1] |= CMD_I1_DATA_BYTE_3(data[2]);
		fallthrough;
	case 2:
		xfer->cmd_desc[1] |= CMD_I1_DATA_BYTE_2(data[1]);
		fallthrough;
	case 1:
		xfer->cmd_desc[1] |= CMD_I1_DATA_BYTE_1(data[0]);
		fallthrough;
	case 0:
		break;
	}
	/* we consumed all the data with the cmd descriptor */
	xfer->data = NULL;
}

static int hci_cmd_v1_prep_ccc(struct i3c_hci *hci,
			       struct hci_xfer *xfer,
			       u8 ccc_addr, u8 ccc_cmd, bool raw)
{
	unsigned int dat_idx = 0;
	enum hci_cmd_mode mode = get_i3c_mode(hci);
	u8 *data = xfer->data;
	unsigned int data_len = xfer->data_len;
	bool rnw = xfer->rnw;
	int ret;

	/* this should never happen */
	if (WARN_ON(raw))
		return -EINVAL;

	if (ccc_addr != I3C_BROADCAST_ADDR) {
		ret = mipi_i3c_hci_dat_v1.get_index(hci, ccc_addr);
		if (ret < 0)
			return ret;
		dat_idx = ret;
	}

	xfer->cmd_tid = hci_get_tid();

	if (!rnw && data_len <= 4) {
		/* we use an Immediate Data Transfer Command */
		xfer->cmd_desc[0] =
			CMD_0_ATTR_I |
			CMD_I0_TID(xfer->cmd_tid) |
			CMD_I0_CMD(ccc_cmd) | CMD_I0_CP |
			CMD_I0_DEV_INDEX(dat_idx) |
			CMD_I0_DTT(data_len) |
			CMD_I0_MODE(mode);
		fill_data_bytes(xfer, data, data_len);
	} else {
		/* we use a Regular Data Transfer Command */
		xfer->cmd_desc[0] =
			CMD_0_ATTR_R |
			CMD_R0_TID(xfer->cmd_tid) |
			CMD_R0_CMD(ccc_cmd) | CMD_R0_CP |
			CMD_R0_DEV_INDEX(dat_idx) |
			CMD_R0_MODE(mode) |
			(rnw ? CMD_R0_RNW : 0);
		xfer->cmd_desc[1] =
			CMD_R1_DATA_LENGTH(data_len);
	}

	return 0;
}

static void hci_cmd_v1_prep_i3c_xfer(struct i3c_hci *hci,
				     struct i3c_dev_desc *dev,
				     struct hci_xfer *xfer)
{
	struct i3c_hci_dev_data *dev_data = i3c_dev_get_master_data(dev);
	unsigned int dat_idx = dev_data->dat_idx;
	enum hci_cmd_mode mode = get_i3c_mode(hci);
	u8 *data = xfer->data;
	unsigned int data_len = xfer->data_len;
	bool rnw = xfer->rnw;

	xfer->cmd_tid = hci_get_tid();

	if (!rnw && data_len <= 4) {
		/* we use an Immediate Data Transfer Command */
		xfer->cmd_desc[0] =
			CMD_0_ATTR_I |
			CMD_I0_TID(xfer->cmd_tid) |
			CMD_I0_DEV_INDEX(dat_idx) |
			CMD_I0_DTT(data_len) |
			CMD_I0_MODE(mode);
		fill_data_bytes(xfer, data, data_len);
	} else {
		/* we use a Regular Data Transfer Command */
		xfer->cmd_desc[0] =
			CMD_0_ATTR_R |
			CMD_R0_TID(xfer->cmd_tid) |
			CMD_R0_DEV_INDEX(dat_idx) |
			CMD_R0_MODE(mode) |
			(rnw ? CMD_R0_RNW : 0);
		xfer->cmd_desc[1] =
			CMD_R1_DATA_LENGTH(data_len);
	}
}

static void hci_cmd_v1_prep_i2c_xfer(struct i3c_hci *hci,
				     struct i2c_dev_desc *dev,
				     struct hci_xfer *xfer)
{
	struct i3c_hci_dev_data *dev_data = i2c_dev_get_master_data(dev);
	unsigned int dat_idx = dev_data->dat_idx;
	enum hci_cmd_mode mode = get_i2c_mode(hci);
	u8 *data = xfer->data;
	unsigned int data_len = xfer->data_len;
	bool rnw = xfer->rnw;

	xfer->cmd_tid = hci_get_tid();

	if (!rnw && data_len <= 4) {
		/* we use an Immediate Data Transfer Command */
		xfer->cmd_desc[0] =
			CMD_0_ATTR_I |
			CMD_I0_TID(xfer->cmd_tid) |
			CMD_I0_DEV_INDEX(dat_idx) |
			CMD_I0_DTT(data_len) |
			CMD_I0_MODE(mode);
		fill_data_bytes(xfer, data, data_len);
	} else {
		/* we use a Regular Data Transfer Command */
		xfer->cmd_desc[0] =
			CMD_0_ATTR_R |
			CMD_R0_TID(xfer->cmd_tid) |
			CMD_R0_DEV_INDEX(dat_idx) |
			CMD_R0_MODE(mode) |
			(rnw ? CMD_R0_RNW : 0);
		xfer->cmd_desc[1] =
			CMD_R1_DATA_LENGTH(data_len);
	}
}

static int hci_cmd_v1_daa(struct i3c_hci *hci)
{
	struct hci_xfer *xfer;
	int ret, dat_idx = -1;
	u8 next_addr = 0;
	u64 pid;
	unsigned int dcr, bcr;
	DECLARE_COMPLETION_ONSTACK(done);

	xfer = hci_alloc_xfer(1);
	if (!xfer)
		return -ENOMEM;

	/*
	 * Simple for now: we allocate a temporary DAT entry, do a single
	 * DAA, register the device which will allocate its own DAT entry
	 * via the core callback, then free the temporary DAT entry.
	 * Loop until there is no more devices to assign an address to.
	 * Yes, there is room for improvements.
	 */
	for (;;) {
		ret = mipi_i3c_hci_dat_v1.alloc_entry(hci);
		if (ret < 0)
			break;
		dat_idx = ret;
		ret = i3c_master_get_free_addr(&hci->master, next_addr);
		if (ret < 0)
			break;
		next_addr = ret;

		DBG("next_addr = 0x%02x, DAA using DAT %d", next_addr, dat_idx);
		mipi_i3c_hci_dat_v1.set_dynamic_addr(hci, dat_idx, next_addr);
		mipi_i3c_hci_dct_index_reset(hci);

		xfer->cmd_tid = hci_get_tid();
		xfer->cmd_desc[0] =
			CMD_0_ATTR_A |
			CMD_A0_TID(xfer->cmd_tid) |
			CMD_A0_CMD(I3C_CCC_ENTDAA) |
			CMD_A0_DEV_INDEX(dat_idx) |
			CMD_A0_DEV_COUNT(1) |
			CMD_A0_ROC | CMD_A0_TOC;
		xfer->cmd_desc[1] = 0;
		xfer->completion = &done;
		hci->io->queue_xfer(hci, xfer, 1);
		if (!wait_for_completion_timeout(&done, HZ) &&
		    hci->io->dequeue_xfer(hci, xfer, 1)) {
			ret = -ETIME;
			break;
		}
		if ((RESP_STATUS(xfer->response) == RESP_ERR_ADDR_HEADER ||
		     RESP_STATUS(xfer->response) == RESP_ERR_NACK) &&
		    RESP_DATA_LENGTH(xfer->response) == 1) {
			ret = 0;  /* no more devices to be assigned */
			break;
		}
		if (RESP_STATUS(xfer->response) != RESP_SUCCESS) {
			ret = -EIO;
			break;
		}

		i3c_hci_dct_get_val(hci, 0, &pid, &dcr, &bcr);
		DBG("assigned address %#x to device PID=0x%llx DCR=%#x BCR=%#x",
		    next_addr, pid, dcr, bcr);

		mipi_i3c_hci_dat_v1.free_entry(hci, dat_idx);
		dat_idx = -1;

		/*
		 * TODO: Extend the subsystem layer to allow for registering
		 * new device and provide BCR/DCR/PID at the same time.
		 */
		ret = i3c_master_add_i3c_dev_locked(&hci->master, next_addr);
		if (ret)
			break;
	}

	if (dat_idx >= 0)
		mipi_i3c_hci_dat_v1.free_entry(hci, dat_idx);
	hci_free_xfer(xfer, 1);
	return ret;
}

const struct hci_cmd_ops mipi_i3c_hci_cmd_v1 = {
	.prep_ccc		= hci_cmd_v1_prep_ccc,
	.prep_i3c_xfer		= hci_cmd_v1_prep_i3c_xfer,
	.prep_i2c_xfer		= hci_cmd_v1_prep_i2c_xfer,
	.perform_daa		= hci_cmd_v1_daa,
};
