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

/* TID generation (4 bits wide in all cases) */
#define hci_get_tid(bits) \
	(atomic_inc_return_relaxed(&hci->next_cmd_tid) % (1U << 4))

/* This abstracts operations with our command descriptor formats */
struct hci_cmd_ops {
	int (*prep_ccc)(struct i3c_hci *hci, struct hci_xfer *xfer,
			u8 ccc_addr, u8 ccc_cmd, bool raw);
	void (*prep_i3c_xfer)(struct i3c_hci *hci, struct i3c_dev_desc *dev,
			      struct hci_xfer *xfer);
	void (*prep_i2c_xfer)(struct i3c_hci *hci, struct i2c_dev_desc *dev,
			      struct hci_xfer *xfer);
	int (*perform_daa)(struct i3c_hci *hci);
};

/* Our various instances */
extern const struct hci_cmd_ops mipi_i3c_hci_cmd_v1;
extern const struct hci_cmd_ops mipi_i3c_hci_cmd_v2;

#endif
