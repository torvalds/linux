/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (C) 2008, cozybit Inc.
 *  Copyright (C) 2003-2006, Marvell International Ltd.
 */
#include <linux/wait.h>
#include <linux/timer.h>

struct lbtf_private;

/**
  * This file contains definition for USB interface.
  */
#define CMD_TYPE_REQUEST		0xF00DFACE
#define CMD_TYPE_DATA			0xBEADC0DE
#define CMD_TYPE_INDICATION		0xBEEFFACE

#define BOOT_CMD_FW_BY_USB		0x01
#define BOOT_CMD_FW_IN_EEPROM		0x02
#define BOOT_CMD_UPDATE_BOOT2		0x03
#define BOOT_CMD_UPDATE_FW		0x04
#define BOOT_CMD_MAGIC_NUMBER		0x4C56524D   /* LVRM */

struct bootcmd {
	__le32	magic;
	uint8_t	cmd;
	uint8_t	pad[11];
};

#define BOOT_CMD_RESP_OK		0x0001
#define BOOT_CMD_RESP_FAIL		0x0000

struct bootcmdresp {
	__le32	magic;
	uint8_t	cmd;
	uint8_t	result;
	uint8_t	pad[2];
};

/** USB card description structure*/
struct if_usb_card {
	struct usb_device *udev;
	struct urb *rx_urb, *tx_urb, *cmd_urb;
	struct lbtf_private *priv;

	struct sk_buff *rx_skb;

	uint8_t ep_in;
	uint8_t ep_out;

	int8_t bootcmdresp;

	int ep_in_size;

	void *ep_out_buf;
	int ep_out_size;

	const struct firmware *fw;
	struct timer_list fw_timeout;
	wait_queue_head_t fw_wq;
	uint32_t fwseqnum;
	uint32_t totalbytes;
	uint32_t fwlastblksent;
	uint8_t CRC_OK;
	uint8_t fwdnldover;
	uint8_t fwfinalblk;

	__le16 boot2_version;
};

/** fwheader */
struct fwheader {
	__le32 dnldcmd;
	__le32 baseaddr;
	__le32 datalength;
	__le32 CRC;
};

#define FW_MAX_DATA_BLK_SIZE	600
/** FWData */
struct fwdata {
	struct fwheader hdr;
	__le32 seqnum;
	uint8_t data[];
};

/** fwsyncheader */
struct fwsyncheader {
	__le32 cmd;
	__le32 seqnum;
};

#define FW_HAS_DATA_TO_RECV		0x00000001
#define FW_HAS_LAST_BLOCK		0x00000004
