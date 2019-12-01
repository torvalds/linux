/*
 * Sony NFC Port-100 Series driver
 * Copyright (c) 2013, Intel Corporation.
 *
 * Partly based/Inspired by Stephen Tiedemann's nfcpy
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/module.h>
#include <linux/usb.h>
#include <net/nfc/digital.h>

#define VERSION "0.1"

#define SONY_VENDOR_ID		0x054c
#define RCS380S_PRODUCT_ID	0x06c1
#define RCS380P_PRODUCT_ID	0x06c3

#define PORT100_PROTOCOLS (NFC_PROTO_JEWEL_MASK    | \
			   NFC_PROTO_MIFARE_MASK   | \
			   NFC_PROTO_FELICA_MASK   | \
			   NFC_PROTO_NFC_DEP_MASK  | \
			   NFC_PROTO_ISO14443_MASK | \
			   NFC_PROTO_ISO14443_B_MASK)

#define PORT100_CAPABILITIES (NFC_DIGITAL_DRV_CAPS_IN_CRC | \
			      NFC_DIGITAL_DRV_CAPS_TG_CRC)

/* Standard port100 frame definitions */
#define PORT100_FRAME_HEADER_LEN (sizeof(struct port100_frame) \
				  + 2) /* data[0] CC, data[1] SCC */
#define PORT100_FRAME_TAIL_LEN 2 /* data[len] DCS, data[len + 1] postamble*/

#define PORT100_COMM_RF_HEAD_MAX_LEN (sizeof(struct port100_tg_comm_rf_cmd))

/*
 * Max extended frame payload len, excluding CC and SCC
 * which are already in PORT100_FRAME_HEADER_LEN.
 */
#define PORT100_FRAME_MAX_PAYLOAD_LEN 1001

#define PORT100_FRAME_ACK_SIZE 6 /* Preamble (1), SoPC (2), ACK Code (2),
				    Postamble (1) */
static u8 ack_frame[PORT100_FRAME_ACK_SIZE] = {
	0x00, 0x00, 0xff, 0x00, 0xff, 0x00
};

#define PORT100_FRAME_CHECKSUM(f) (f->data[le16_to_cpu(f->datalen)])
#define PORT100_FRAME_POSTAMBLE(f) (f->data[le16_to_cpu(f->datalen) + 1])

/* start of frame */
#define PORT100_FRAME_SOF	0x00FF
#define PORT100_FRAME_EXT	0xFFFF
#define PORT100_FRAME_ACK	0x00FF

/* Port-100 command: in or out */
#define PORT100_FRAME_DIRECTION(f) (f->data[0]) /* CC */
#define PORT100_FRAME_DIR_OUT 0xD6
#define PORT100_FRAME_DIR_IN  0xD7

/* Port-100 sub-command */
#define PORT100_FRAME_CMD(f) (f->data[1]) /* SCC */

#define PORT100_CMD_GET_FIRMWARE_VERSION 0x20
#define PORT100_CMD_GET_COMMAND_TYPE     0x28
#define PORT100_CMD_SET_COMMAND_TYPE     0x2A

#define PORT100_CMD_IN_SET_RF       0x00
#define PORT100_CMD_IN_SET_PROTOCOL 0x02
#define PORT100_CMD_IN_COMM_RF      0x04

#define PORT100_CMD_TG_SET_RF       0x40
#define PORT100_CMD_TG_SET_PROTOCOL 0x42
#define PORT100_CMD_TG_SET_RF_OFF   0x46
#define PORT100_CMD_TG_COMM_RF      0x48

#define PORT100_CMD_SWITCH_RF       0x06

#define PORT100_CMD_RESPONSE(cmd) (cmd + 1)

#define PORT100_CMD_TYPE_IS_SUPPORTED(mask, cmd_type) \
	((mask) & (0x01 << (cmd_type)))
#define PORT100_CMD_TYPE_0	0
#define PORT100_CMD_TYPE_1	1

#define PORT100_CMD_STATUS_OK      0x00
#define PORT100_CMD_STATUS_TIMEOUT 0x80

#define PORT100_MDAA_TGT_HAS_BEEN_ACTIVATED_MASK 0x01
#define PORT100_MDAA_TGT_WAS_ACTIVATED_MASK      0x02

struct port100;

typedef void (*port100_send_async_complete_t)(struct port100 *dev, void *arg,
					      struct sk_buff *resp);

/**
 * Setting sets structure for in_set_rf command
 *
 * @in_*_set_number: Represent the entry indexes in the port-100 RF Base Table.
 *              This table contains multiple RF setting sets required for RF
 *              communication.
 *
 * @in_*_comm_type: Theses fields set the communication type to be used.
 */
struct port100_in_rf_setting {
	u8 in_send_set_number;
	u8 in_send_comm_type;
	u8 in_recv_set_number;
	u8 in_recv_comm_type;
} __packed;

#define PORT100_COMM_TYPE_IN_212F 0x01
#define PORT100_COMM_TYPE_IN_424F 0x02
#define PORT100_COMM_TYPE_IN_106A 0x03
#define PORT100_COMM_TYPE_IN_106B 0x07

static const struct port100_in_rf_setting in_rf_settings[] = {
	[NFC_DIGITAL_RF_TECH_212F] = {
		.in_send_set_number = 1,
		.in_send_comm_type  = PORT100_COMM_TYPE_IN_212F,
		.in_recv_set_number = 15,
		.in_recv_comm_type  = PORT100_COMM_TYPE_IN_212F,
	},
	[NFC_DIGITAL_RF_TECH_424F] = {
		.in_send_set_number = 1,
		.in_send_comm_type  = PORT100_COMM_TYPE_IN_424F,
		.in_recv_set_number = 15,
		.in_recv_comm_type  = PORT100_COMM_TYPE_IN_424F,
	},
	[NFC_DIGITAL_RF_TECH_106A] = {
		.in_send_set_number = 2,
		.in_send_comm_type  = PORT100_COMM_TYPE_IN_106A,
		.in_recv_set_number = 15,
		.in_recv_comm_type  = PORT100_COMM_TYPE_IN_106A,
	},
	[NFC_DIGITAL_RF_TECH_106B] = {
		.in_send_set_number = 3,
		.in_send_comm_type  = PORT100_COMM_TYPE_IN_106B,
		.in_recv_set_number = 15,
		.in_recv_comm_type  = PORT100_COMM_TYPE_IN_106B,
	},
	/* Ensures the array has NFC_DIGITAL_RF_TECH_LAST elements */
	[NFC_DIGITAL_RF_TECH_LAST] = { 0 },
};

/**
 * Setting sets structure for tg_set_rf command
 *
 * @tg_set_number: Represents the entry index in the port-100 RF Base Table.
 *                 This table contains multiple RF setting sets required for RF
 *                 communication. this field is used for both send and receive
 *                 settings.
 *
 * @tg_comm_type: Sets the communication type to be used to send and receive
 *                data.
 */
struct port100_tg_rf_setting {
	u8 tg_set_number;
	u8 tg_comm_type;
} __packed;

#define PORT100_COMM_TYPE_TG_106A 0x0B
#define PORT100_COMM_TYPE_TG_212F 0x0C
#define PORT100_COMM_TYPE_TG_424F 0x0D

static const struct port100_tg_rf_setting tg_rf_settings[] = {
	[NFC_DIGITAL_RF_TECH_106A] = {
		.tg_set_number = 8,
		.tg_comm_type = PORT100_COMM_TYPE_TG_106A,
	},
	[NFC_DIGITAL_RF_TECH_212F] = {
		.tg_set_number = 8,
		.tg_comm_type = PORT100_COMM_TYPE_TG_212F,
	},
	[NFC_DIGITAL_RF_TECH_424F] = {
		.tg_set_number = 8,
		.tg_comm_type = PORT100_COMM_TYPE_TG_424F,
	},
	/* Ensures the array has NFC_DIGITAL_RF_TECH_LAST elements */
	[NFC_DIGITAL_RF_TECH_LAST] = { 0 },

};

#define PORT100_IN_PROT_INITIAL_GUARD_TIME      0x00
#define PORT100_IN_PROT_ADD_CRC                 0x01
#define PORT100_IN_PROT_CHECK_CRC               0x02
#define PORT100_IN_PROT_MULTI_CARD              0x03
#define PORT100_IN_PROT_ADD_PARITY              0x04
#define PORT100_IN_PROT_CHECK_PARITY            0x05
#define PORT100_IN_PROT_BITWISE_AC_RECV_MODE    0x06
#define PORT100_IN_PROT_VALID_BIT_NUMBER        0x07
#define PORT100_IN_PROT_CRYPTO1                 0x08
#define PORT100_IN_PROT_ADD_SOF                 0x09
#define PORT100_IN_PROT_CHECK_SOF               0x0A
#define PORT100_IN_PROT_ADD_EOF                 0x0B
#define PORT100_IN_PROT_CHECK_EOF               0x0C
#define PORT100_IN_PROT_DEAF_TIME               0x0E
#define PORT100_IN_PROT_CRM                     0x0F
#define PORT100_IN_PROT_CRM_MIN_LEN             0x10
#define PORT100_IN_PROT_T1_TAG_FRAME            0x11
#define PORT100_IN_PROT_RFCA                    0x12
#define PORT100_IN_PROT_GUARD_TIME_AT_INITIATOR 0x13
#define PORT100_IN_PROT_END                     0x14

#define PORT100_IN_MAX_NUM_PROTOCOLS            19

#define PORT100_TG_PROT_TU           0x00
#define PORT100_TG_PROT_RF_OFF       0x01
#define PORT100_TG_PROT_CRM          0x02
#define PORT100_TG_PROT_END          0x03

#define PORT100_TG_MAX_NUM_PROTOCOLS 3

struct port100_protocol {
	u8 number;
	u8 value;
} __packed;

static struct port100_protocol
in_protocols[][PORT100_IN_MAX_NUM_PROTOCOLS + 1] = {
	[NFC_DIGITAL_FRAMING_NFCA_SHORT] = {
		{ PORT100_IN_PROT_INITIAL_GUARD_TIME,      6 },
		{ PORT100_IN_PROT_ADD_CRC,                 0 },
		{ PORT100_IN_PROT_CHECK_CRC,               0 },
		{ PORT100_IN_PROT_MULTI_CARD,              0 },
		{ PORT100_IN_PROT_ADD_PARITY,              0 },
		{ PORT100_IN_PROT_CHECK_PARITY,            1 },
		{ PORT100_IN_PROT_BITWISE_AC_RECV_MODE,    0 },
		{ PORT100_IN_PROT_VALID_BIT_NUMBER,        7 },
		{ PORT100_IN_PROT_CRYPTO1,                 0 },
		{ PORT100_IN_PROT_ADD_SOF,                 0 },
		{ PORT100_IN_PROT_CHECK_SOF,               0 },
		{ PORT100_IN_PROT_ADD_EOF,                 0 },
		{ PORT100_IN_PROT_CHECK_EOF,               0 },
		{ PORT100_IN_PROT_DEAF_TIME,               4 },
		{ PORT100_IN_PROT_CRM,                     0 },
		{ PORT100_IN_PROT_CRM_MIN_LEN,             0 },
		{ PORT100_IN_PROT_T1_TAG_FRAME,            0 },
		{ PORT100_IN_PROT_RFCA,                    0 },
		{ PORT100_IN_PROT_GUARD_TIME_AT_INITIATOR, 6 },
		{ PORT100_IN_PROT_END,                     0 },
	},
	[NFC_DIGITAL_FRAMING_NFCA_STANDARD] = {
		{ PORT100_IN_PROT_INITIAL_GUARD_TIME,      6 },
		{ PORT100_IN_PROT_ADD_CRC,                 0 },
		{ PORT100_IN_PROT_CHECK_CRC,               0 },
		{ PORT100_IN_PROT_MULTI_CARD,              0 },
		{ PORT100_IN_PROT_ADD_PARITY,              1 },
		{ PORT100_IN_PROT_CHECK_PARITY,            1 },
		{ PORT100_IN_PROT_BITWISE_AC_RECV_MODE,    0 },
		{ PORT100_IN_PROT_VALID_BIT_NUMBER,        8 },
		{ PORT100_IN_PROT_CRYPTO1,                 0 },
		{ PORT100_IN_PROT_ADD_SOF,                 0 },
		{ PORT100_IN_PROT_CHECK_SOF,               0 },
		{ PORT100_IN_PROT_ADD_EOF,                 0 },
		{ PORT100_IN_PROT_CHECK_EOF,               0 },
		{ PORT100_IN_PROT_DEAF_TIME,               4 },
		{ PORT100_IN_PROT_CRM,                     0 },
		{ PORT100_IN_PROT_CRM_MIN_LEN,             0 },
		{ PORT100_IN_PROT_T1_TAG_FRAME,            0 },
		{ PORT100_IN_PROT_RFCA,                    0 },
		{ PORT100_IN_PROT_GUARD_TIME_AT_INITIATOR, 6 },
		{ PORT100_IN_PROT_END,                     0 },
	},
	[NFC_DIGITAL_FRAMING_NFCA_STANDARD_WITH_CRC_A] = {
		{ PORT100_IN_PROT_INITIAL_GUARD_TIME,      6 },
		{ PORT100_IN_PROT_ADD_CRC,                 1 },
		{ PORT100_IN_PROT_CHECK_CRC,               1 },
		{ PORT100_IN_PROT_MULTI_CARD,              0 },
		{ PORT100_IN_PROT_ADD_PARITY,              1 },
		{ PORT100_IN_PROT_CHECK_PARITY,            1 },
		{ PORT100_IN_PROT_BITWISE_AC_RECV_MODE,    0 },
		{ PORT100_IN_PROT_VALID_BIT_NUMBER,        8 },
		{ PORT100_IN_PROT_CRYPTO1,                 0 },
		{ PORT100_IN_PROT_ADD_SOF,                 0 },
		{ PORT100_IN_PROT_CHECK_SOF,               0 },
		{ PORT100_IN_PROT_ADD_EOF,                 0 },
		{ PORT100_IN_PROT_CHECK_EOF,               0 },
		{ PORT100_IN_PROT_DEAF_TIME,               4 },
		{ PORT100_IN_PROT_CRM,                     0 },
		{ PORT100_IN_PROT_CRM_MIN_LEN,             0 },
		{ PORT100_IN_PROT_T1_TAG_FRAME,            0 },
		{ PORT100_IN_PROT_RFCA,                    0 },
		{ PORT100_IN_PROT_GUARD_TIME_AT_INITIATOR, 6 },
		{ PORT100_IN_PROT_END,                     0 },
	},
	[NFC_DIGITAL_FRAMING_NFCA_T1T] = {
		/* nfc_digital_framing_nfca_short */
		{ PORT100_IN_PROT_ADD_CRC,          2 },
		{ PORT100_IN_PROT_CHECK_CRC,        2 },
		{ PORT100_IN_PROT_VALID_BIT_NUMBER, 8 },
		{ PORT100_IN_PROT_T1_TAG_FRAME,     2 },
		{ PORT100_IN_PROT_END,              0 },
	},
	[NFC_DIGITAL_FRAMING_NFCA_T2T] = {
		/* nfc_digital_framing_nfca_standard */
		{ PORT100_IN_PROT_ADD_CRC,   1 },
		{ PORT100_IN_PROT_CHECK_CRC, 0 },
		{ PORT100_IN_PROT_END,       0 },
	},
	[NFC_DIGITAL_FRAMING_NFCA_T4T] = {
		/* nfc_digital_framing_nfca_standard_with_crc_a */
		{ PORT100_IN_PROT_END,       0 },
	},
	[NFC_DIGITAL_FRAMING_NFCA_NFC_DEP] = {
		/* nfc_digital_framing_nfca_standard */
		{ PORT100_IN_PROT_END, 0 },
	},
	[NFC_DIGITAL_FRAMING_NFCF] = {
		{ PORT100_IN_PROT_INITIAL_GUARD_TIME,     18 },
		{ PORT100_IN_PROT_ADD_CRC,                 1 },
		{ PORT100_IN_PROT_CHECK_CRC,               1 },
		{ PORT100_IN_PROT_MULTI_CARD,              0 },
		{ PORT100_IN_PROT_ADD_PARITY,              0 },
		{ PORT100_IN_PROT_CHECK_PARITY,            0 },
		{ PORT100_IN_PROT_BITWISE_AC_RECV_MODE,    0 },
		{ PORT100_IN_PROT_VALID_BIT_NUMBER,        8 },
		{ PORT100_IN_PROT_CRYPTO1,                 0 },
		{ PORT100_IN_PROT_ADD_SOF,                 0 },
		{ PORT100_IN_PROT_CHECK_SOF,               0 },
		{ PORT100_IN_PROT_ADD_EOF,                 0 },
		{ PORT100_IN_PROT_CHECK_EOF,               0 },
		{ PORT100_IN_PROT_DEAF_TIME,               4 },
		{ PORT100_IN_PROT_CRM,                     0 },
		{ PORT100_IN_PROT_CRM_MIN_LEN,             0 },
		{ PORT100_IN_PROT_T1_TAG_FRAME,            0 },
		{ PORT100_IN_PROT_RFCA,                    0 },
		{ PORT100_IN_PROT_GUARD_TIME_AT_INITIATOR, 6 },
		{ PORT100_IN_PROT_END,                     0 },
	},
	[NFC_DIGITAL_FRAMING_NFCF_T3T] = {
		/* nfc_digital_framing_nfcf */
		{ PORT100_IN_PROT_END, 0 },
	},
	[NFC_DIGITAL_FRAMING_NFCF_NFC_DEP] = {
		/* nfc_digital_framing_nfcf */
		{ PORT100_IN_PROT_INITIAL_GUARD_TIME,     18 },
		{ PORT100_IN_PROT_ADD_CRC,                 1 },
		{ PORT100_IN_PROT_CHECK_CRC,               1 },
		{ PORT100_IN_PROT_MULTI_CARD,              0 },
		{ PORT100_IN_PROT_ADD_PARITY,              0 },
		{ PORT100_IN_PROT_CHECK_PARITY,            0 },
		{ PORT100_IN_PROT_BITWISE_AC_RECV_MODE,    0 },
		{ PORT100_IN_PROT_VALID_BIT_NUMBER,        8 },
		{ PORT100_IN_PROT_CRYPTO1,                 0 },
		{ PORT100_IN_PROT_ADD_SOF,                 0 },
		{ PORT100_IN_PROT_CHECK_SOF,               0 },
		{ PORT100_IN_PROT_ADD_EOF,                 0 },
		{ PORT100_IN_PROT_CHECK_EOF,               0 },
		{ PORT100_IN_PROT_DEAF_TIME,               4 },
		{ PORT100_IN_PROT_CRM,                     0 },
		{ PORT100_IN_PROT_CRM_MIN_LEN,             0 },
		{ PORT100_IN_PROT_T1_TAG_FRAME,            0 },
		{ PORT100_IN_PROT_RFCA,                    0 },
		{ PORT100_IN_PROT_GUARD_TIME_AT_INITIATOR, 6 },
		{ PORT100_IN_PROT_END,                     0 },
	},
	[NFC_DIGITAL_FRAMING_NFC_DEP_ACTIVATED] = {
		{ PORT100_IN_PROT_END, 0 },
	},
	[NFC_DIGITAL_FRAMING_NFCB] = {
		{ PORT100_IN_PROT_INITIAL_GUARD_TIME,     20 },
		{ PORT100_IN_PROT_ADD_CRC,                 1 },
		{ PORT100_IN_PROT_CHECK_CRC,               1 },
		{ PORT100_IN_PROT_MULTI_CARD,              0 },
		{ PORT100_IN_PROT_ADD_PARITY,              0 },
		{ PORT100_IN_PROT_CHECK_PARITY,            0 },
		{ PORT100_IN_PROT_BITWISE_AC_RECV_MODE,    0 },
		{ PORT100_IN_PROT_VALID_BIT_NUMBER,        8 },
		{ PORT100_IN_PROT_CRYPTO1,                 0 },
		{ PORT100_IN_PROT_ADD_SOF,                 1 },
		{ PORT100_IN_PROT_CHECK_SOF,               1 },
		{ PORT100_IN_PROT_ADD_EOF,                 1 },
		{ PORT100_IN_PROT_CHECK_EOF,               1 },
		{ PORT100_IN_PROT_DEAF_TIME,               4 },
		{ PORT100_IN_PROT_CRM,                     0 },
		{ PORT100_IN_PROT_CRM_MIN_LEN,             0 },
		{ PORT100_IN_PROT_T1_TAG_FRAME,            0 },
		{ PORT100_IN_PROT_RFCA,                    0 },
		{ PORT100_IN_PROT_GUARD_TIME_AT_INITIATOR, 6 },
		{ PORT100_IN_PROT_END,                     0 },
	},
	[NFC_DIGITAL_FRAMING_NFCB_T4T] = {
		/* nfc_digital_framing_nfcb */
		{ PORT100_IN_PROT_END,                     0 },
	},
	/* Ensures the array has NFC_DIGITAL_FRAMING_LAST elements */
	[NFC_DIGITAL_FRAMING_LAST] = {
		{ PORT100_IN_PROT_END, 0 },
	},
};

static struct port100_protocol
tg_protocols[][PORT100_TG_MAX_NUM_PROTOCOLS + 1] = {
	[NFC_DIGITAL_FRAMING_NFCA_SHORT] = {
		{ PORT100_TG_PROT_END, 0 },
	},
	[NFC_DIGITAL_FRAMING_NFCA_STANDARD] = {
		{ PORT100_TG_PROT_END, 0 },
	},
	[NFC_DIGITAL_FRAMING_NFCA_STANDARD_WITH_CRC_A] = {
		{ PORT100_TG_PROT_END, 0 },
	},
	[NFC_DIGITAL_FRAMING_NFCA_T1T] = {
		{ PORT100_TG_PROT_END, 0 },
	},
	[NFC_DIGITAL_FRAMING_NFCA_T2T] = {
		{ PORT100_TG_PROT_END, 0 },
	},
	[NFC_DIGITAL_FRAMING_NFCA_NFC_DEP] = {
		{ PORT100_TG_PROT_TU,     1 },
		{ PORT100_TG_PROT_RF_OFF, 0 },
		{ PORT100_TG_PROT_CRM,    7 },
		{ PORT100_TG_PROT_END,    0 },
	},
	[NFC_DIGITAL_FRAMING_NFCF] = {
		{ PORT100_TG_PROT_END, 0 },
	},
	[NFC_DIGITAL_FRAMING_NFCF_T3T] = {
		{ PORT100_TG_PROT_END, 0 },
	},
	[NFC_DIGITAL_FRAMING_NFCF_NFC_DEP] = {
		{ PORT100_TG_PROT_TU,     1 },
		{ PORT100_TG_PROT_RF_OFF, 0 },
		{ PORT100_TG_PROT_CRM,    7 },
		{ PORT100_TG_PROT_END,    0 },
	},
	[NFC_DIGITAL_FRAMING_NFC_DEP_ACTIVATED] = {
		{ PORT100_TG_PROT_RF_OFF, 1 },
		{ PORT100_TG_PROT_END,    0 },
	},
	/* Ensures the array has NFC_DIGITAL_FRAMING_LAST elements */
	[NFC_DIGITAL_FRAMING_LAST] = {
		{ PORT100_TG_PROT_END,    0 },
	},
};

struct port100 {
	struct nfc_digital_dev *nfc_digital_dev;

	int skb_headroom;
	int skb_tailroom;

	struct usb_device *udev;
	struct usb_interface *interface;

	struct urb *out_urb;
	struct urb *in_urb;

	/* This mutex protects the out_urb and avoids to submit a new command
	 * through port100_send_frame_async() while the previous one is being
	 * canceled through port100_abort_cmd().
	 */
	struct mutex out_urb_lock;

	struct work_struct cmd_complete_work;

	u8 cmd_type;

	/* The digital stack serializes commands to be sent. There is no need
	 * for any queuing/locking mechanism at driver level.
	 */
	struct port100_cmd *cmd;

	bool cmd_cancel;
	struct completion cmd_cancel_done;
};

struct port100_cmd {
	u8 code;
	int status;
	struct sk_buff *req;
	struct sk_buff *resp;
	int resp_len;
	port100_send_async_complete_t  complete_cb;
	void *complete_cb_context;
};

struct port100_frame {
	u8 preamble;
	__be16 start_frame;
	__be16 extended_frame;
	__le16 datalen;
	u8 datalen_checksum;
	u8 data[];
} __packed;

struct port100_ack_frame {
	u8 preamble;
	__be16 start_frame;
	__be16 ack_frame;
	u8 postambule;
} __packed;

struct port100_cb_arg {
	nfc_digital_cmd_complete_t complete_cb;
	void *complete_arg;
	u8 mdaa;
};

struct port100_tg_comm_rf_cmd {
	__le16 guard_time;
	__le16 send_timeout;
	u8 mdaa;
	u8 nfca_param[6];
	u8 nfcf_param[18];
	u8 mf_halted;
	u8 arae_flag;
	__le16 recv_timeout;
	u8 data[];
} __packed;

struct port100_tg_comm_rf_res {
	u8 comm_type;
	u8 ar_status;
	u8 target_activated;
	__le32 status;
	u8 data[];
} __packed;

/* The rule: value + checksum = 0 */
static inline u8 port100_checksum(u16 value)
{
	return ~(((u8 *)&value)[0] + ((u8 *)&value)[1]) + 1;
}

/* The rule: sum(data elements) + checksum = 0 */
static u8 port100_data_checksum(u8 *data, int datalen)
{
	u8 sum = 0;
	int i;

	for (i = 0; i < datalen; i++)
		sum += data[i];

	return port100_checksum(sum);
}

static void port100_tx_frame_init(void *_frame, u8 cmd_code)
{
	struct port100_frame *frame = _frame;

	frame->preamble = 0;
	frame->start_frame = cpu_to_be16(PORT100_FRAME_SOF);
	frame->extended_frame = cpu_to_be16(PORT100_FRAME_EXT);
	PORT100_FRAME_DIRECTION(frame) = PORT100_FRAME_DIR_OUT;
	PORT100_FRAME_CMD(frame) = cmd_code;
	frame->datalen = cpu_to_le16(2);
}

static void port100_tx_frame_finish(void *_frame)
{
	struct port100_frame *frame = _frame;

	frame->datalen_checksum = port100_checksum(le16_to_cpu(frame->datalen));

	PORT100_FRAME_CHECKSUM(frame) =
		port100_data_checksum(frame->data, le16_to_cpu(frame->datalen));

	PORT100_FRAME_POSTAMBLE(frame) = 0;
}

static void port100_tx_update_payload_len(void *_frame, int len)
{
	struct port100_frame *frame = _frame;

	frame->datalen = cpu_to_le16(le16_to_cpu(frame->datalen) + len);
}

static bool port100_rx_frame_is_valid(void *_frame)
{
	u8 checksum;
	struct port100_frame *frame = _frame;

	if (frame->start_frame != cpu_to_be16(PORT100_FRAME_SOF) ||
	    frame->extended_frame != cpu_to_be16(PORT100_FRAME_EXT))
		return false;

	checksum = port100_checksum(le16_to_cpu(frame->datalen));
	if (checksum != frame->datalen_checksum)
		return false;

	checksum = port100_data_checksum(frame->data,
					 le16_to_cpu(frame->datalen));
	if (checksum != PORT100_FRAME_CHECKSUM(frame))
		return false;

	return true;
}

static bool port100_rx_frame_is_ack(struct port100_ack_frame *frame)
{
	return (frame->start_frame == cpu_to_be16(PORT100_FRAME_SOF) &&
		frame->ack_frame == cpu_to_be16(PORT100_FRAME_ACK));
}

static inline int port100_rx_frame_size(void *frame)
{
	struct port100_frame *f = frame;

	return sizeof(struct port100_frame) + le16_to_cpu(f->datalen) +
	       PORT100_FRAME_TAIL_LEN;
}

static bool port100_rx_frame_is_cmd_response(struct port100 *dev, void *frame)
{
	struct port100_frame *f = frame;

	return (PORT100_FRAME_CMD(f) == PORT100_CMD_RESPONSE(dev->cmd->code));
}

static void port100_recv_response(struct urb *urb)
{
	struct port100 *dev = urb->context;
	struct port100_cmd *cmd = dev->cmd;
	u8 *in_frame;

	cmd->status = urb->status;

	switch (urb->status) {
	case 0:
		break; /* success */
	case -ECONNRESET:
	case -ENOENT:
		nfc_err(&dev->interface->dev,
			"The urb has been canceled (status %d)\n", urb->status);
		goto sched_wq;
	case -ESHUTDOWN:
	default:
		nfc_err(&dev->interface->dev, "Urb failure (status %d)\n",
			urb->status);
		goto sched_wq;
	}

	in_frame = dev->in_urb->transfer_buffer;

	if (!port100_rx_frame_is_valid(in_frame)) {
		nfc_err(&dev->interface->dev, "Received an invalid frame\n");
		cmd->status = -EIO;
		goto sched_wq;
	}

	print_hex_dump_debug("PORT100 RX: ", DUMP_PREFIX_NONE, 16, 1, in_frame,
			     port100_rx_frame_size(in_frame), false);

	if (!port100_rx_frame_is_cmd_response(dev, in_frame)) {
		nfc_err(&dev->interface->dev,
			"It's not the response to the last command\n");
		cmd->status = -EIO;
		goto sched_wq;
	}

sched_wq:
	schedule_work(&dev->cmd_complete_work);
}

static int port100_submit_urb_for_response(struct port100 *dev, gfp_t flags)
{
	dev->in_urb->complete = port100_recv_response;

	return usb_submit_urb(dev->in_urb, flags);
}

static void port100_recv_ack(struct urb *urb)
{
	struct port100 *dev = urb->context;
	struct port100_cmd *cmd = dev->cmd;
	struct port100_ack_frame *in_frame;
	int rc;

	cmd->status = urb->status;

	switch (urb->status) {
	case 0:
		break; /* success */
	case -ECONNRESET:
	case -ENOENT:
		nfc_err(&dev->interface->dev,
			"The urb has been stopped (status %d)\n", urb->status);
		goto sched_wq;
	case -ESHUTDOWN:
	default:
		nfc_err(&dev->interface->dev, "Urb failure (status %d)\n",
			urb->status);
		goto sched_wq;
	}

	in_frame = dev->in_urb->transfer_buffer;

	if (!port100_rx_frame_is_ack(in_frame)) {
		nfc_err(&dev->interface->dev, "Received an invalid ack\n");
		cmd->status = -EIO;
		goto sched_wq;
	}

	rc = port100_submit_urb_for_response(dev, GFP_ATOMIC);
	if (rc) {
		nfc_err(&dev->interface->dev,
			"usb_submit_urb failed with result %d\n", rc);
		cmd->status = rc;
		goto sched_wq;
	}

	return;

sched_wq:
	schedule_work(&dev->cmd_complete_work);
}

static int port100_submit_urb_for_ack(struct port100 *dev, gfp_t flags)
{
	dev->in_urb->complete = port100_recv_ack;

	return usb_submit_urb(dev->in_urb, flags);
}

static int port100_send_ack(struct port100 *dev)
{
	int rc = 0;

	mutex_lock(&dev->out_urb_lock);

	/*
	 * If prior cancel is in-flight (dev->cmd_cancel == true), we
	 * can skip to send cancel. Then this will wait the prior
	 * cancel, or merged into the next cancel rarely if next
	 * cancel was started before waiting done. In any case, this
	 * will be waked up soon or later.
	 */
	if (!dev->cmd_cancel) {
		reinit_completion(&dev->cmd_cancel_done);

		usb_kill_urb(dev->out_urb);

		dev->out_urb->transfer_buffer = ack_frame;
		dev->out_urb->transfer_buffer_length = sizeof(ack_frame);
		rc = usb_submit_urb(dev->out_urb, GFP_KERNEL);

		/*
		 * Set the cmd_cancel flag only if the URB has been
		 * successfully submitted. It will be reset by the out
		 * URB completion callback port100_send_complete().
		 */
		dev->cmd_cancel = !rc;
	}

	mutex_unlock(&dev->out_urb_lock);

	if (!rc)
		wait_for_completion(&dev->cmd_cancel_done);

	return rc;
}

static int port100_send_frame_async(struct port100 *dev, struct sk_buff *out,
				    struct sk_buff *in, int in_len)
{
	int rc;

	mutex_lock(&dev->out_urb_lock);

	/* A command cancel frame as been sent through dev->out_urb. Don't try
	 * to submit a new one.
	 */
	if (dev->cmd_cancel) {
		rc = -EAGAIN;
		goto exit;
	}

	dev->out_urb->transfer_buffer = out->data;
	dev->out_urb->transfer_buffer_length = out->len;

	dev->in_urb->transfer_buffer = in->data;
	dev->in_urb->transfer_buffer_length = in_len;

	print_hex_dump_debug("PORT100 TX: ", DUMP_PREFIX_NONE, 16, 1,
			     out->data, out->len, false);

	rc = usb_submit_urb(dev->out_urb, GFP_KERNEL);
	if (rc)
		goto exit;

	rc = port100_submit_urb_for_ack(dev, GFP_KERNEL);
	if (rc)
		usb_kill_urb(dev->out_urb);

exit:
	mutex_unlock(&dev->out_urb_lock);

	return rc;
}

static void port100_build_cmd_frame(struct port100 *dev, u8 cmd_code,
				    struct sk_buff *skb)
{
	/* payload is already there, just update datalen */
	int payload_len = skb->len;

	skb_push(skb, PORT100_FRAME_HEADER_LEN);
	skb_put(skb, PORT100_FRAME_TAIL_LEN);

	port100_tx_frame_init(skb->data, cmd_code);
	port100_tx_update_payload_len(skb->data, payload_len);
	port100_tx_frame_finish(skb->data);
}

static void port100_send_async_complete(struct port100 *dev)
{
	struct port100_cmd *cmd = dev->cmd;
	int status = cmd->status;

	struct sk_buff *req = cmd->req;
	struct sk_buff *resp = cmd->resp;

	dev_kfree_skb(req);

	dev->cmd = NULL;

	if (status < 0) {
		cmd->complete_cb(dev, cmd->complete_cb_context,
				 ERR_PTR(status));
		dev_kfree_skb(resp);
		goto done;
	}

	skb_put(resp, port100_rx_frame_size(resp->data));
	skb_pull(resp, PORT100_FRAME_HEADER_LEN);
	skb_trim(resp, resp->len - PORT100_FRAME_TAIL_LEN);

	cmd->complete_cb(dev, cmd->complete_cb_context, resp);

done:
	kfree(cmd);
}

static int port100_send_cmd_async(struct port100 *dev, u8 cmd_code,
				struct sk_buff *req,
				port100_send_async_complete_t complete_cb,
				void *complete_cb_context)
{
	struct port100_cmd *cmd;
	struct sk_buff *resp;
	int rc;
	int  resp_len = PORT100_FRAME_HEADER_LEN +
			PORT100_FRAME_MAX_PAYLOAD_LEN +
			PORT100_FRAME_TAIL_LEN;

	if (dev->cmd) {
		nfc_err(&dev->interface->dev,
			"A command is still in process\n");
		return -EBUSY;
	}

	resp = alloc_skb(resp_len, GFP_KERNEL);
	if (!resp)
		return -ENOMEM;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		dev_kfree_skb(resp);
		return -ENOMEM;
	}

	cmd->code = cmd_code;
	cmd->req = req;
	cmd->resp = resp;
	cmd->resp_len = resp_len;
	cmd->complete_cb = complete_cb;
	cmd->complete_cb_context = complete_cb_context;

	port100_build_cmd_frame(dev, cmd_code, req);

	dev->cmd = cmd;

	rc = port100_send_frame_async(dev, req, resp, resp_len);
	if (rc) {
		kfree(cmd);
		dev_kfree_skb(resp);
		dev->cmd = NULL;
	}

	return rc;
}

struct port100_sync_cmd_response {
	struct sk_buff *resp;
	struct completion done;
};

static void port100_wq_cmd_complete(struct work_struct *work)
{
	struct port100 *dev = container_of(work, struct port100,
					   cmd_complete_work);

	port100_send_async_complete(dev);
}

static void port100_send_sync_complete(struct port100 *dev, void *_arg,
				      struct sk_buff *resp)
{
	struct port100_sync_cmd_response *arg = _arg;

	arg->resp = resp;
	complete(&arg->done);
}

static struct sk_buff *port100_send_cmd_sync(struct port100 *dev, u8 cmd_code,
					     struct sk_buff *req)
{
	int rc;
	struct port100_sync_cmd_response arg;

	init_completion(&arg.done);

	rc = port100_send_cmd_async(dev, cmd_code, req,
				    port100_send_sync_complete, &arg);
	if (rc) {
		dev_kfree_skb(req);
		return ERR_PTR(rc);
	}

	wait_for_completion(&arg.done);

	return arg.resp;
}

static void port100_send_complete(struct urb *urb)
{
	struct port100 *dev = urb->context;

	if (dev->cmd_cancel) {
		complete_all(&dev->cmd_cancel_done);
		dev->cmd_cancel = false;
	}

	switch (urb->status) {
	case 0:
		break; /* success */
	case -ECONNRESET:
	case -ENOENT:
		nfc_err(&dev->interface->dev,
			"The urb has been stopped (status %d)\n", urb->status);
		break;
	case -ESHUTDOWN:
	default:
		nfc_err(&dev->interface->dev, "Urb failure (status %d)\n",
			urb->status);
	}
}

static void port100_abort_cmd(struct nfc_digital_dev *ddev)
{
	struct port100 *dev = nfc_digital_get_drvdata(ddev);

	/* An ack will cancel the last issued command */
	port100_send_ack(dev);

	/* cancel the urb request */
	usb_kill_urb(dev->in_urb);
}

static struct sk_buff *port100_alloc_skb(struct port100 *dev, unsigned int size)
{
	struct sk_buff *skb;

	skb = alloc_skb(dev->skb_headroom + dev->skb_tailroom + size,
			GFP_KERNEL);
	if (skb)
		skb_reserve(skb, dev->skb_headroom);

	return skb;
}

static int port100_set_command_type(struct port100 *dev, u8 command_type)
{
	struct sk_buff *skb;
	struct sk_buff *resp;
	int rc;

	skb = port100_alloc_skb(dev, 1);
	if (!skb)
		return -ENOMEM;

	skb_put_u8(skb, command_type);

	resp = port100_send_cmd_sync(dev, PORT100_CMD_SET_COMMAND_TYPE, skb);
	if (IS_ERR(resp))
		return PTR_ERR(resp);

	rc = resp->data[0];

	dev_kfree_skb(resp);

	return rc;
}

static u64 port100_get_command_type_mask(struct port100 *dev)
{
	struct sk_buff *skb;
	struct sk_buff *resp;
	u64 mask;

	skb = port100_alloc_skb(dev, 0);
	if (!skb)
		return -ENOMEM;

	resp = port100_send_cmd_sync(dev, PORT100_CMD_GET_COMMAND_TYPE, skb);
	if (IS_ERR(resp))
		return PTR_ERR(resp);

	if (resp->len < 8)
		mask = 0;
	else
		mask = be64_to_cpu(*(__be64 *)resp->data);

	dev_kfree_skb(resp);

	return mask;
}

static u16 port100_get_firmware_version(struct port100 *dev)
{
	struct sk_buff *skb;
	struct sk_buff *resp;
	u16 fw_ver;

	skb = port100_alloc_skb(dev, 0);
	if (!skb)
		return 0;

	resp = port100_send_cmd_sync(dev, PORT100_CMD_GET_FIRMWARE_VERSION,
				     skb);
	if (IS_ERR(resp))
		return 0;

	fw_ver = le16_to_cpu(*(__le16 *)resp->data);

	dev_kfree_skb(resp);

	return fw_ver;
}

static int port100_switch_rf(struct nfc_digital_dev *ddev, bool on)
{
	struct port100 *dev = nfc_digital_get_drvdata(ddev);
	struct sk_buff *skb, *resp;

	skb = port100_alloc_skb(dev, 1);
	if (!skb)
		return -ENOMEM;

	skb_put_u8(skb, on ? 1 : 0);

	/* Cancel the last command if the device is being switched off */
	if (!on)
		port100_abort_cmd(ddev);

	resp = port100_send_cmd_sync(dev, PORT100_CMD_SWITCH_RF, skb);

	if (IS_ERR(resp))
		return PTR_ERR(resp);

	dev_kfree_skb(resp);

	return 0;
}

static int port100_in_set_rf(struct nfc_digital_dev *ddev, u8 rf)
{
	struct port100 *dev = nfc_digital_get_drvdata(ddev);
	struct sk_buff *skb;
	struct sk_buff *resp;
	int rc;

	if (rf >= NFC_DIGITAL_RF_TECH_LAST)
		return -EINVAL;

	skb = port100_alloc_skb(dev, sizeof(struct port100_in_rf_setting));
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &in_rf_settings[rf],
		     sizeof(struct port100_in_rf_setting));

	resp = port100_send_cmd_sync(dev, PORT100_CMD_IN_SET_RF, skb);

	if (IS_ERR(resp))
		return PTR_ERR(resp);

	rc = resp->data[0];

	dev_kfree_skb(resp);

	return rc;
}

static int port100_in_set_framing(struct nfc_digital_dev *ddev, int param)
{
	struct port100 *dev = nfc_digital_get_drvdata(ddev);
	struct port100_protocol *protocols;
	struct sk_buff *skb;
	struct sk_buff *resp;
	int num_protocols;
	size_t size;
	int rc;

	if (param >= NFC_DIGITAL_FRAMING_LAST)
		return -EINVAL;

	protocols = in_protocols[param];

	num_protocols = 0;
	while (protocols[num_protocols].number != PORT100_IN_PROT_END)
		num_protocols++;

	if (!num_protocols)
		return 0;

	size = sizeof(struct port100_protocol) * num_protocols;

	skb = port100_alloc_skb(dev, size);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, protocols, size);

	resp = port100_send_cmd_sync(dev, PORT100_CMD_IN_SET_PROTOCOL, skb);

	if (IS_ERR(resp))
		return PTR_ERR(resp);

	rc = resp->data[0];

	dev_kfree_skb(resp);

	return rc;
}

static int port100_in_configure_hw(struct nfc_digital_dev *ddev, int type,
				   int param)
{
	if (type == NFC_DIGITAL_CONFIG_RF_TECH)
		return port100_in_set_rf(ddev, param);

	if (type == NFC_DIGITAL_CONFIG_FRAMING)
		return port100_in_set_framing(ddev, param);

	return -EINVAL;
}

static void port100_in_comm_rf_complete(struct port100 *dev, void *arg,
				       struct sk_buff *resp)
{
	struct port100_cb_arg *cb_arg = arg;
	nfc_digital_cmd_complete_t cb = cb_arg->complete_cb;
	u32 status;
	int rc;

	if (IS_ERR(resp)) {
		rc =  PTR_ERR(resp);
		goto exit;
	}

	if (resp->len < 4) {
		nfc_err(&dev->interface->dev,
			"Invalid packet length received\n");
		rc = -EIO;
		goto error;
	}

	status = le32_to_cpu(*(__le32 *)resp->data);

	skb_pull(resp, sizeof(u32));

	if (status == PORT100_CMD_STATUS_TIMEOUT) {
		rc = -ETIMEDOUT;
		goto error;
	}

	if (status != PORT100_CMD_STATUS_OK) {
		nfc_err(&dev->interface->dev,
			"in_comm_rf failed with status 0x%08x\n", status);
		rc = -EIO;
		goto error;
	}

	/* Remove collision bits byte */
	skb_pull(resp, 1);

	goto exit;

error:
	kfree_skb(resp);
	resp = ERR_PTR(rc);

exit:
	cb(dev->nfc_digital_dev, cb_arg->complete_arg, resp);

	kfree(cb_arg);
}

static int port100_in_send_cmd(struct nfc_digital_dev *ddev,
			       struct sk_buff *skb, u16 _timeout,
			       nfc_digital_cmd_complete_t cb, void *arg)
{
	struct port100 *dev = nfc_digital_get_drvdata(ddev);
	struct port100_cb_arg *cb_arg;
	__le16 timeout;

	cb_arg = kzalloc(sizeof(struct port100_cb_arg), GFP_KERNEL);
	if (!cb_arg)
		return -ENOMEM;

	cb_arg->complete_cb = cb;
	cb_arg->complete_arg = arg;

	timeout = cpu_to_le16(_timeout * 10);

	memcpy(skb_push(skb, sizeof(__le16)), &timeout, sizeof(__le16));

	return port100_send_cmd_async(dev, PORT100_CMD_IN_COMM_RF, skb,
				      port100_in_comm_rf_complete, cb_arg);
}

static int port100_tg_set_rf(struct nfc_digital_dev *ddev, u8 rf)
{
	struct port100 *dev = nfc_digital_get_drvdata(ddev);
	struct sk_buff *skb;
	struct sk_buff *resp;
	int rc;

	if (rf >= NFC_DIGITAL_RF_TECH_LAST)
		return -EINVAL;

	skb = port100_alloc_skb(dev, sizeof(struct port100_tg_rf_setting));
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &tg_rf_settings[rf],
		     sizeof(struct port100_tg_rf_setting));

	resp = port100_send_cmd_sync(dev, PORT100_CMD_TG_SET_RF, skb);

	if (IS_ERR(resp))
		return PTR_ERR(resp);

	rc = resp->data[0];

	dev_kfree_skb(resp);

	return rc;
}

static int port100_tg_set_framing(struct nfc_digital_dev *ddev, int param)
{
	struct port100 *dev = nfc_digital_get_drvdata(ddev);
	struct port100_protocol *protocols;
	struct sk_buff *skb;
	struct sk_buff *resp;
	int rc;
	int num_protocols;
	size_t size;

	if (param >= NFC_DIGITAL_FRAMING_LAST)
		return -EINVAL;

	protocols = tg_protocols[param];

	num_protocols = 0;
	while (protocols[num_protocols].number != PORT100_TG_PROT_END)
		num_protocols++;

	if (!num_protocols)
		return 0;

	size = sizeof(struct port100_protocol) * num_protocols;

	skb = port100_alloc_skb(dev, size);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, protocols, size);

	resp = port100_send_cmd_sync(dev, PORT100_CMD_TG_SET_PROTOCOL, skb);

	if (IS_ERR(resp))
		return PTR_ERR(resp);

	rc = resp->data[0];

	dev_kfree_skb(resp);

	return rc;
}

static int port100_tg_configure_hw(struct nfc_digital_dev *ddev, int type,
				   int param)
{
	if (type == NFC_DIGITAL_CONFIG_RF_TECH)
		return port100_tg_set_rf(ddev, param);

	if (type == NFC_DIGITAL_CONFIG_FRAMING)
		return port100_tg_set_framing(ddev, param);

	return -EINVAL;
}

static bool port100_tg_target_activated(struct port100 *dev, u8 tgt_activated)
{
	u8 mask;

	switch (dev->cmd_type) {
	case PORT100_CMD_TYPE_0:
		mask = PORT100_MDAA_TGT_HAS_BEEN_ACTIVATED_MASK;
		break;
	case PORT100_CMD_TYPE_1:
		mask = PORT100_MDAA_TGT_HAS_BEEN_ACTIVATED_MASK |
		       PORT100_MDAA_TGT_WAS_ACTIVATED_MASK;
		break;
	default:
		nfc_err(&dev->interface->dev, "Unknown command type\n");
		return false;
	}

	return ((tgt_activated & mask) == mask);
}

static void port100_tg_comm_rf_complete(struct port100 *dev, void *arg,
					struct sk_buff *resp)
{
	u32 status;
	struct port100_cb_arg *cb_arg = arg;
	nfc_digital_cmd_complete_t cb = cb_arg->complete_cb;
	struct port100_tg_comm_rf_res *hdr;

	if (IS_ERR(resp))
		goto exit;

	hdr = (struct port100_tg_comm_rf_res *)resp->data;

	status = le32_to_cpu(hdr->status);

	if (cb_arg->mdaa &&
	    !port100_tg_target_activated(dev, hdr->target_activated)) {
		kfree_skb(resp);
		resp = ERR_PTR(-ETIMEDOUT);

		goto exit;
	}

	skb_pull(resp, sizeof(struct port100_tg_comm_rf_res));

	if (status != PORT100_CMD_STATUS_OK) {
		kfree_skb(resp);

		if (status == PORT100_CMD_STATUS_TIMEOUT)
			resp = ERR_PTR(-ETIMEDOUT);
		else
			resp = ERR_PTR(-EIO);
	}

exit:
	cb(dev->nfc_digital_dev, cb_arg->complete_arg, resp);

	kfree(cb_arg);
}

static int port100_tg_send_cmd(struct nfc_digital_dev *ddev,
			       struct sk_buff *skb, u16 timeout,
			       nfc_digital_cmd_complete_t cb, void *arg)
{
	struct port100 *dev = nfc_digital_get_drvdata(ddev);
	struct port100_tg_comm_rf_cmd *hdr;
	struct port100_cb_arg *cb_arg;

	cb_arg = kzalloc(sizeof(struct port100_cb_arg), GFP_KERNEL);
	if (!cb_arg)
		return -ENOMEM;

	cb_arg->complete_cb = cb;
	cb_arg->complete_arg = arg;

	skb_push(skb, sizeof(struct port100_tg_comm_rf_cmd));

	hdr = (struct port100_tg_comm_rf_cmd *)skb->data;

	memset(hdr, 0, sizeof(struct port100_tg_comm_rf_cmd));
	hdr->guard_time = cpu_to_le16(500);
	hdr->send_timeout = cpu_to_le16(0xFFFF);
	hdr->recv_timeout = cpu_to_le16(timeout);

	return port100_send_cmd_async(dev, PORT100_CMD_TG_COMM_RF, skb,
				      port100_tg_comm_rf_complete, cb_arg);
}

static int port100_listen_mdaa(struct nfc_digital_dev *ddev,
			       struct digital_tg_mdaa_params *params,
			       u16 timeout,
			       nfc_digital_cmd_complete_t cb, void *arg)
{
	struct port100 *dev = nfc_digital_get_drvdata(ddev);
	struct port100_tg_comm_rf_cmd *hdr;
	struct port100_cb_arg *cb_arg;
	struct sk_buff *skb;
	int rc;

	rc = port100_tg_configure_hw(ddev, NFC_DIGITAL_CONFIG_RF_TECH,
				     NFC_DIGITAL_RF_TECH_106A);
	if (rc)
		return rc;

	rc = port100_tg_configure_hw(ddev, NFC_DIGITAL_CONFIG_FRAMING,
				     NFC_DIGITAL_FRAMING_NFCA_NFC_DEP);
	if (rc)
		return rc;

	cb_arg = kzalloc(sizeof(struct port100_cb_arg), GFP_KERNEL);
	if (!cb_arg)
		return -ENOMEM;

	cb_arg->complete_cb = cb;
	cb_arg->complete_arg = arg;
	cb_arg->mdaa = 1;

	skb = port100_alloc_skb(dev, 0);
	if (!skb) {
		kfree(cb_arg);
		return -ENOMEM;
	}

	skb_push(skb, sizeof(struct port100_tg_comm_rf_cmd));
	hdr = (struct port100_tg_comm_rf_cmd *)skb->data;

	memset(hdr, 0, sizeof(struct port100_tg_comm_rf_cmd));

	hdr->guard_time = 0;
	hdr->send_timeout = cpu_to_le16(0xFFFF);
	hdr->mdaa = 1;
	hdr->nfca_param[0] = (params->sens_res >> 8) & 0xFF;
	hdr->nfca_param[1] = params->sens_res & 0xFF;
	memcpy(hdr->nfca_param + 2, params->nfcid1, 3);
	hdr->nfca_param[5] = params->sel_res;
	memcpy(hdr->nfcf_param, params->nfcid2, 8);
	hdr->nfcf_param[16] = (params->sc >> 8) & 0xFF;
	hdr->nfcf_param[17] = params->sc & 0xFF;
	hdr->recv_timeout = cpu_to_le16(timeout);

	return port100_send_cmd_async(dev, PORT100_CMD_TG_COMM_RF, skb,
				      port100_tg_comm_rf_complete, cb_arg);
}

static int port100_listen(struct nfc_digital_dev *ddev, u16 timeout,
			  nfc_digital_cmd_complete_t cb, void *arg)
{
	struct port100 *dev = nfc_digital_get_drvdata(ddev);
	struct sk_buff *skb;

	skb = port100_alloc_skb(dev, 0);
	if (!skb)
		return -ENOMEM;

	return port100_tg_send_cmd(ddev, skb, timeout, cb, arg);
}

static struct nfc_digital_ops port100_digital_ops = {
	.in_configure_hw = port100_in_configure_hw,
	.in_send_cmd = port100_in_send_cmd,

	.tg_listen_mdaa = port100_listen_mdaa,
	.tg_listen = port100_listen,
	.tg_configure_hw = port100_tg_configure_hw,
	.tg_send_cmd = port100_tg_send_cmd,

	.switch_rf = port100_switch_rf,
	.abort_cmd = port100_abort_cmd,
};

static const struct usb_device_id port100_table[] = {
	{ USB_DEVICE(SONY_VENDOR_ID, RCS380S_PRODUCT_ID), },
	{ USB_DEVICE(SONY_VENDOR_ID, RCS380P_PRODUCT_ID), },
	{ }
};
MODULE_DEVICE_TABLE(usb, port100_table);

static int port100_probe(struct usb_interface *interface,
			 const struct usb_device_id *id)
{
	struct port100 *dev;
	int rc;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int in_endpoint;
	int out_endpoint;
	u16 fw_version;
	u64 cmd_type_mask;
	int i;

	dev = devm_kzalloc(&interface->dev, sizeof(struct port100), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	mutex_init(&dev->out_urb_lock);
	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;
	usb_set_intfdata(interface, dev);

	in_endpoint = out_endpoint = 0;
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!in_endpoint && usb_endpoint_is_bulk_in(endpoint))
			in_endpoint = endpoint->bEndpointAddress;

		if (!out_endpoint && usb_endpoint_is_bulk_out(endpoint))
			out_endpoint = endpoint->bEndpointAddress;
	}

	if (!in_endpoint || !out_endpoint) {
		nfc_err(&interface->dev,
			"Could not find bulk-in or bulk-out endpoint\n");
		rc = -ENODEV;
		goto error;
	}

	dev->in_urb = usb_alloc_urb(0, GFP_KERNEL);
	dev->out_urb = usb_alloc_urb(0, GFP_KERNEL);

	if (!dev->in_urb || !dev->out_urb) {
		nfc_err(&interface->dev, "Could not allocate USB URBs\n");
		rc = -ENOMEM;
		goto error;
	}

	usb_fill_bulk_urb(dev->in_urb, dev->udev,
			  usb_rcvbulkpipe(dev->udev, in_endpoint),
			  NULL, 0, NULL, dev);
	usb_fill_bulk_urb(dev->out_urb, dev->udev,
			  usb_sndbulkpipe(dev->udev, out_endpoint),
			  NULL, 0, port100_send_complete, dev);
	dev->out_urb->transfer_flags = URB_ZERO_PACKET;

	dev->skb_headroom = PORT100_FRAME_HEADER_LEN +
			    PORT100_COMM_RF_HEAD_MAX_LEN;
	dev->skb_tailroom = PORT100_FRAME_TAIL_LEN;

	init_completion(&dev->cmd_cancel_done);
	INIT_WORK(&dev->cmd_complete_work, port100_wq_cmd_complete);

	/* The first thing to do with the Port-100 is to set the command type
	 * to be used. If supported we use command type 1. 0 otherwise.
	 */
	cmd_type_mask = port100_get_command_type_mask(dev);
	if (!cmd_type_mask) {
		nfc_err(&interface->dev,
			"Could not get supported command types\n");
		rc = -ENODEV;
		goto error;
	}

	if (PORT100_CMD_TYPE_IS_SUPPORTED(cmd_type_mask, PORT100_CMD_TYPE_1))
		dev->cmd_type = PORT100_CMD_TYPE_1;
	else
		dev->cmd_type = PORT100_CMD_TYPE_0;

	rc = port100_set_command_type(dev, dev->cmd_type);
	if (rc) {
		nfc_err(&interface->dev,
			"The device does not support command type %u\n",
			dev->cmd_type);
		goto error;
	}

	fw_version = port100_get_firmware_version(dev);
	if (!fw_version)
		nfc_err(&interface->dev,
			"Could not get device firmware version\n");

	nfc_info(&interface->dev,
		 "Sony NFC Port-100 Series attached (firmware v%x.%02x)\n",
		 (fw_version & 0xFF00) >> 8, fw_version & 0xFF);

	dev->nfc_digital_dev = nfc_digital_allocate_device(&port100_digital_ops,
							   PORT100_PROTOCOLS,
							   PORT100_CAPABILITIES,
							   dev->skb_headroom,
							   dev->skb_tailroom);
	if (!dev->nfc_digital_dev) {
		nfc_err(&interface->dev,
			"Could not allocate nfc_digital_dev\n");
		rc = -ENOMEM;
		goto error;
	}

	nfc_digital_set_parent_dev(dev->nfc_digital_dev, &interface->dev);
	nfc_digital_set_drvdata(dev->nfc_digital_dev, dev);

	rc = nfc_digital_register_device(dev->nfc_digital_dev);
	if (rc) {
		nfc_err(&interface->dev,
			"Could not register digital device\n");
		goto free_nfc_dev;
	}

	return 0;

free_nfc_dev:
	nfc_digital_free_device(dev->nfc_digital_dev);

error:
	usb_free_urb(dev->in_urb);
	usb_free_urb(dev->out_urb);
	usb_put_dev(dev->udev);

	return rc;
}

static void port100_disconnect(struct usb_interface *interface)
{
	struct port100 *dev;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	nfc_digital_unregister_device(dev->nfc_digital_dev);
	nfc_digital_free_device(dev->nfc_digital_dev);

	usb_kill_urb(dev->in_urb);
	usb_kill_urb(dev->out_urb);

	usb_free_urb(dev->in_urb);
	usb_free_urb(dev->out_urb);
	usb_put_dev(dev->udev);

	kfree(dev->cmd);

	nfc_info(&interface->dev, "Sony Port-100 NFC device disconnected\n");
}

static struct usb_driver port100_driver = {
	.name =		"port100",
	.probe =	port100_probe,
	.disconnect =	port100_disconnect,
	.id_table =	port100_table,
};

module_usb_driver(port100_driver);

MODULE_DESCRIPTION("NFC Port-100 series usb driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
