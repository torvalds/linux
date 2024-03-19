/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2020, MIPI Alliance, Inc.
 *
 * Author: Nicolas Pitre <npitre@baylibre.com>
 *
 * Common command/response related stuff
 */

#ifndef CMD_H
#define CMD_H

/*
 * Those bits are common to all descriptor formats and
 * may be manipulated by the core code.
 */
#define CMD_0_TOC			W0_BIT_(31)
#define CMD_0_ROC			W0_BIT_(30)
#define CMD_0_ATTR			W0_MASK(2, 0)
enum hci_cmd_attr {
	CMD_0_ATTR_A	= 0x2,
	CMD_0_ATTR_I	= 0x1,
	CMD_0_ATTR_R	= 0x0,
	CMD_0_ATTR_C	= 0x3,
	CMD_0_ATTR_M	= 0x7,
	CMD_0_ATTR_T	= 0x0,
};

/*
 * Response Descriptor Structure
 */
#define RESP_STATUS(resp)		FIELD_GET(GENMASK(31, 28), resp)
#define RESP_TID(resp)			FIELD_GET(GENMASK(27, 24), resp)
#define RESP_DATA_LENGTH(resp)		FIELD_GET(GENMASK(21,  0), resp)

#define RESP_ERR_FIELD			GENMASK(31, 28)

/*
 * Target mode Response Descriptor Structure
 */
#define TARGET_RESP_STATUS(resp)	FIELD_GET(GENMASK(31, 28), resp)
#define TARGET_RESP_XFER_TYPE(resp)	FIELD_GET(BIT(27), resp)
#define TARGET_RESP_XFER_TYPE_W		0
#define TARGET_RESP_XFER_TYPE_R		1
#define TARGET_RESP_TID(resp)		FIELD_GET(GENMASK(26, 24), resp)
#define TARGET_RESP_CCC_HDR(resp)	FIELD_GET(GENMASK(23, 16), resp)
#define TARGET_RESP_SDR_PRIV_XFER	0
#define TARGET_RESP_DATA_LENGTH(resp)	FIELD_GET(GENMASK(15,  0), resp)

enum hci_resp_err {
	RESP_SUCCESS			= 0x0,
	RESP_ERR_CRC			= 0x1,
	RESP_ERR_PARITY			= 0x2,
	RESP_ERR_FRAME			= 0x3,
	RESP_ERR_ADDR_HEADER		= 0x4,
	RESP_ERR_BCAST_NACK_7E		= 0x4,
	RESP_ERR_NACK			= 0x5,
	RESP_ERR_OVL			= 0x6,
	RESP_ERR_I3C_SHORT_READ		= 0x7,
	RESP_ERR_HC_TERMINATED		= 0x8,
	RESP_ERR_I2C_WR_DATA_NACK	= 0x9,
	RESP_ERR_BUS_XFER_ABORTED	= 0x9,
	RESP_ERR_NOT_SUPPORTED		= 0xa,
	RESP_ERR_ABORTED_WITH_CRC	= 0xb,
	/* 0xc to 0xf are reserved for transfer specific errors */
};

/* Sub command in the internal control command */
enum hci_m_sub_cmd {
	M_SUB_CMD_RING_LOCK = 0x1,
	M_SUB_CMD_BROCAST_ADDR_EN = 0x2,
	M_SUB_CMD_DAT_CONT_UPDATE = 0x3,
	M_SUB_CMD_TARGET_RST_PATTERN = 0x4,
	M_SUB_CMD_REC_RST_PROC = 0x5,
	M_SUB_CMD_END_XFER = 0x6,
	M_SUB_CMD_CR_W_GETACCCR = 0x7,
};

/* Parameter for Sub command: RING_LOCK */
#define CMD_M0_RING_LOCK_ON 0x1

/* Parameter for Sub command: BROCAST_ADDR_EN */
#define CMD_M0_BROCAST_ADDR_ON 0x1

/* Parameter for Sub command: TARGET_RST_PATTERN */
enum hci_rst_op_type {
	RST_OP_TARGET_RST = 0x0,
	RST_OP_ENTER_CRITICAL_SEC = 0x2,
	RST_OP_EXIT_CRITICAL_SEC = 0x3,
};

/* Parameter for Sub command: REC_RST_PROC */
enum hci_rec_rst_proc {
	REC_PROC_I2C_SDA_STUCK = 0x0,
	REC_PROC_I3C_SDR_SDA_STUCK = 0x1,
	REC_PROC_I3C_HDR_SDA_STUCK = 0x2,
	REC_PROC_FORCE_STOP = 0x4,
	REC_PROC_CE2_ERR = 0x5,
	REC_PROC_TIMED_RST = 0x6,
};

/* TID generation (4 bits wide in all cases) */
#define hci_get_tid(bits) \
	(atomic_inc_return_relaxed(&hci->next_cmd_tid) % (1U << 4))
/* Specific tid to identify the response for IBI or master read */
#define TID_TARGET_IBI		0b0001
#define TID_TARGET_RD_DATA	0b0010

/* This abstracts operations with our command descriptor formats */
struct hci_cmd_ops {
	int (*prep_ccc)(struct i3c_hci *hci, struct hci_xfer *xfer,
			u8 ccc_addr, u8 ccc_cmd, bool raw);
	void (*prep_i3c_xfer)(struct i3c_hci *hci, struct i3c_dev_desc *dev,
			      struct hci_xfer *xfer);
	void (*prep_i2c_xfer)(struct i3c_hci *hci, struct i2c_dev_desc *dev,
			      struct hci_xfer *xfer);
	void (*prep_internal)(struct i3c_hci *hci, struct hci_xfer *xfer,
			      u8 sub_cmd, u32 param);
	int (*perform_daa)(struct i3c_hci *hci);
};

/* Our various instances */
extern const struct hci_cmd_ops mipi_i3c_hci_cmd_v1;
extern const struct hci_cmd_ops mipi_i3c_hci_cmd_v2;

#endif
