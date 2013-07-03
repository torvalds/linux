/*
 * Copyright (C) 2011 Instituto Nokia de Tecnologia
 * Copyright (C) 2012-2013 Tieto Poland
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/nfc.h>
#include <linux/netdevice.h>
#include <net/nfc/nfc.h>

#define VERSION "0.2"

#define PN533_VENDOR_ID 0x4CC
#define PN533_PRODUCT_ID 0x2533

#define SCM_VENDOR_ID 0x4E6
#define SCL3711_PRODUCT_ID 0x5591

#define SONY_VENDOR_ID         0x054c
#define PASORI_PRODUCT_ID      0x02e1

#define ACS_VENDOR_ID 0x072f
#define ACR122U_PRODUCT_ID 0x2200

#define PN533_DEVICE_STD     0x1
#define PN533_DEVICE_PASORI  0x2
#define PN533_DEVICE_ACR122U 0x3

#define PN533_ALL_PROTOCOLS (NFC_PROTO_JEWEL_MASK | NFC_PROTO_MIFARE_MASK |\
			     NFC_PROTO_FELICA_MASK | NFC_PROTO_ISO14443_MASK |\
			     NFC_PROTO_NFC_DEP_MASK |\
			     NFC_PROTO_ISO14443_B_MASK)

#define PN533_NO_TYPE_B_PROTOCOLS (NFC_PROTO_JEWEL_MASK | \
				   NFC_PROTO_MIFARE_MASK | \
				   NFC_PROTO_FELICA_MASK | \
				   NFC_PROTO_ISO14443_MASK | \
				   NFC_PROTO_NFC_DEP_MASK)

static const struct usb_device_id pn533_table[] = {
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE,
	  .idVendor		= PN533_VENDOR_ID,
	  .idProduct		= PN533_PRODUCT_ID,
	  .driver_info		= PN533_DEVICE_STD,
	},
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE,
	  .idVendor		= SCM_VENDOR_ID,
	  .idProduct		= SCL3711_PRODUCT_ID,
	  .driver_info		= PN533_DEVICE_STD,
	},
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE,
	  .idVendor		= SONY_VENDOR_ID,
	  .idProduct		= PASORI_PRODUCT_ID,
	  .driver_info		= PN533_DEVICE_PASORI,
	},
	{ .match_flags		= USB_DEVICE_ID_MATCH_DEVICE,
	  .idVendor		= ACS_VENDOR_ID,
	  .idProduct		= ACR122U_PRODUCT_ID,
	  .driver_info		= PN533_DEVICE_ACR122U,
	},
	{ }
};
MODULE_DEVICE_TABLE(usb, pn533_table);

/* How much time we spend listening for initiators */
#define PN533_LISTEN_TIME 2
/* Delay between each poll frame (ms) */
#define PN533_POLL_INTERVAL 10

/* Standard pn533 frame definitions (standard and extended)*/
#define PN533_STD_FRAME_HEADER_LEN (sizeof(struct pn533_std_frame) \
					+ 2) /* data[0] TFI, data[1] CC */
#define PN533_STD_FRAME_TAIL_LEN 2 /* data[len] DCS, data[len + 1] postamble*/

#define PN533_EXT_FRAME_HEADER_LEN (sizeof(struct pn533_ext_frame) \
					+ 2) /* data[0] TFI, data[1] CC */

#define PN533_CMD_DATAEXCH_DATA_MAXLEN	262
#define PN533_CMD_DATAFRAME_MAXLEN	240	/* max data length (send) */

/*
 * Max extended frame payload len, excluding TFI and CC
 * which are already in PN533_FRAME_HEADER_LEN.
 */
#define PN533_STD_FRAME_MAX_PAYLOAD_LEN 263

#define PN533_STD_FRAME_ACK_SIZE 6 /* Preamble (1), SoPC (2), ACK Code (2),
				  Postamble (1) */
#define PN533_STD_FRAME_CHECKSUM(f) (f->data[f->datalen])
#define PN533_STD_FRAME_POSTAMBLE(f) (f->data[f->datalen + 1])
/* Half start code (3), LEN (4) should be 0xffff for extended frame */
#define PN533_STD_IS_EXTENDED(hdr) ((hdr)->datalen == 0xFF \
					&& (hdr)->datalen_checksum == 0xFF)
#define PN533_EXT_FRAME_CHECKSUM(f) (f->data[be16_to_cpu(f->datalen)])

/* start of frame */
#define PN533_STD_FRAME_SOF 0x00FF

/* standard frame identifier: in/out/error */
#define PN533_STD_FRAME_IDENTIFIER(f) (f->data[0]) /* TFI */
#define PN533_STD_FRAME_DIR_OUT 0xD4
#define PN533_STD_FRAME_DIR_IN 0xD5

/* ACS ACR122 pn533 frame definitions */
#define PN533_ACR122_TX_FRAME_HEADER_LEN (sizeof(struct pn533_acr122_tx_frame) \
					  + 2)
#define PN533_ACR122_TX_FRAME_TAIL_LEN 0
#define PN533_ACR122_RX_FRAME_HEADER_LEN (sizeof(struct pn533_acr122_rx_frame) \
					  + 2)
#define PN533_ACR122_RX_FRAME_TAIL_LEN 2
#define PN533_ACR122_FRAME_MAX_PAYLOAD_LEN PN533_STD_FRAME_MAX_PAYLOAD_LEN

/* CCID messages types */
#define PN533_ACR122_PC_TO_RDR_ICCPOWERON 0x62
#define PN533_ACR122_PC_TO_RDR_ESCAPE 0x6B

#define PN533_ACR122_RDR_TO_PC_ESCAPE 0x83

/* PN533 Commands */
#define PN533_FRAME_CMD(f) (f->data[1])

#define PN533_CMD_GET_FIRMWARE_VERSION 0x02
#define PN533_CMD_RF_CONFIGURATION 0x32
#define PN533_CMD_IN_DATA_EXCHANGE 0x40
#define PN533_CMD_IN_COMM_THRU     0x42
#define PN533_CMD_IN_LIST_PASSIVE_TARGET 0x4A
#define PN533_CMD_IN_ATR 0x50
#define PN533_CMD_IN_RELEASE 0x52
#define PN533_CMD_IN_JUMP_FOR_DEP 0x56

#define PN533_CMD_TG_INIT_AS_TARGET 0x8c
#define PN533_CMD_TG_GET_DATA 0x86
#define PN533_CMD_TG_SET_DATA 0x8e
#define PN533_CMD_UNDEF 0xff

#define PN533_CMD_RESPONSE(cmd) (cmd + 1)

/* PN533 Return codes */
#define PN533_CMD_RET_MASK 0x3F
#define PN533_CMD_MI_MASK 0x40
#define PN533_CMD_RET_SUCCESS 0x00

struct pn533;

typedef int (*pn533_send_async_complete_t) (struct pn533 *dev, void *arg,
					struct sk_buff *resp);

/* structs for pn533 commands */

/* PN533_CMD_GET_FIRMWARE_VERSION */
struct pn533_fw_version {
	u8 ic;
	u8 ver;
	u8 rev;
	u8 support;
};

/* PN533_CMD_RF_CONFIGURATION */
#define PN533_CFGITEM_RF_FIELD    0x01
#define PN533_CFGITEM_TIMING      0x02
#define PN533_CFGITEM_MAX_RETRIES 0x05
#define PN533_CFGITEM_PASORI      0x82

#define PN533_CFGITEM_RF_FIELD_AUTO_RFCA 0x2
#define PN533_CFGITEM_RF_FIELD_ON        0x1
#define PN533_CFGITEM_RF_FIELD_OFF       0x0

#define PN533_CONFIG_TIMING_102 0xb
#define PN533_CONFIG_TIMING_204 0xc
#define PN533_CONFIG_TIMING_409 0xd
#define PN533_CONFIG_TIMING_819 0xe

#define PN533_CONFIG_MAX_RETRIES_NO_RETRY 0x00
#define PN533_CONFIG_MAX_RETRIES_ENDLESS 0xFF

struct pn533_config_max_retries {
	u8 mx_rty_atr;
	u8 mx_rty_psl;
	u8 mx_rty_passive_act;
} __packed;

struct pn533_config_timing {
	u8 rfu;
	u8 atr_res_timeout;
	u8 dep_timeout;
} __packed;

/* PN533_CMD_IN_LIST_PASSIVE_TARGET */

/* felica commands opcode */
#define PN533_FELICA_OPC_SENSF_REQ 0
#define PN533_FELICA_OPC_SENSF_RES 1
/* felica SENSF_REQ parameters */
#define PN533_FELICA_SENSF_SC_ALL 0xFFFF
#define PN533_FELICA_SENSF_RC_NO_SYSTEM_CODE 0
#define PN533_FELICA_SENSF_RC_SYSTEM_CODE 1
#define PN533_FELICA_SENSF_RC_ADVANCED_PROTOCOL 2

/* type B initiator_data values */
#define PN533_TYPE_B_AFI_ALL_FAMILIES 0
#define PN533_TYPE_B_POLL_METHOD_TIMESLOT 0
#define PN533_TYPE_B_POLL_METHOD_PROBABILISTIC 1

union pn533_cmd_poll_initdata {
	struct {
		u8 afi;
		u8 polling_method;
	} __packed type_b;
	struct {
		u8 opcode;
		__be16 sc;
		u8 rc;
		u8 tsn;
	} __packed felica;
};

/* Poll modulations */
enum {
	PN533_POLL_MOD_106KBPS_A,
	PN533_POLL_MOD_212KBPS_FELICA,
	PN533_POLL_MOD_424KBPS_FELICA,
	PN533_POLL_MOD_106KBPS_JEWEL,
	PN533_POLL_MOD_847KBPS_B,
	PN533_LISTEN_MOD,

	__PN533_POLL_MOD_AFTER_LAST,
};
#define PN533_POLL_MOD_MAX (__PN533_POLL_MOD_AFTER_LAST - 1)

struct pn533_poll_modulations {
	struct {
		u8 maxtg;
		u8 brty;
		union pn533_cmd_poll_initdata initiator_data;
	} __packed data;
	u8 len;
};

static const struct pn533_poll_modulations poll_mod[] = {
	[PN533_POLL_MOD_106KBPS_A] = {
		.data = {
			.maxtg = 1,
			.brty = 0,
		},
		.len = 2,
	},
	[PN533_POLL_MOD_212KBPS_FELICA] = {
		.data = {
			.maxtg = 1,
			.brty = 1,
			.initiator_data.felica = {
				.opcode = PN533_FELICA_OPC_SENSF_REQ,
				.sc = PN533_FELICA_SENSF_SC_ALL,
				.rc = PN533_FELICA_SENSF_RC_SYSTEM_CODE,
				.tsn = 0x03,
			},
		},
		.len = 7,
	},
	[PN533_POLL_MOD_424KBPS_FELICA] = {
		.data = {
			.maxtg = 1,
			.brty = 2,
			.initiator_data.felica = {
				.opcode = PN533_FELICA_OPC_SENSF_REQ,
				.sc = PN533_FELICA_SENSF_SC_ALL,
				.rc = PN533_FELICA_SENSF_RC_SYSTEM_CODE,
				.tsn = 0x03,
			},
		 },
		.len = 7,
	},
	[PN533_POLL_MOD_106KBPS_JEWEL] = {
		.data = {
			.maxtg = 1,
			.brty = 4,
		},
		.len = 2,
	},
	[PN533_POLL_MOD_847KBPS_B] = {
		.data = {
			.maxtg = 1,
			.brty = 8,
			.initiator_data.type_b = {
				.afi = PN533_TYPE_B_AFI_ALL_FAMILIES,
				.polling_method =
					PN533_TYPE_B_POLL_METHOD_TIMESLOT,
			},
		},
		.len = 3,
	},
	[PN533_LISTEN_MOD] = {
		.len = 0,
	},
};

/* PN533_CMD_IN_ATR */

struct pn533_cmd_activate_response {
	u8 status;
	u8 nfcid3t[10];
	u8 didt;
	u8 bst;
	u8 brt;
	u8 to;
	u8 ppt;
	/* optional */
	u8 gt[];
} __packed;

struct pn533_cmd_jump_dep_response {
	u8 status;
	u8 tg;
	u8 nfcid3t[10];
	u8 didt;
	u8 bst;
	u8 brt;
	u8 to;
	u8 ppt;
	/* optional */
	u8 gt[];
} __packed;


/* PN533_TG_INIT_AS_TARGET */
#define PN533_INIT_TARGET_PASSIVE 0x1
#define PN533_INIT_TARGET_DEP 0x2

#define PN533_INIT_TARGET_RESP_FRAME_MASK 0x3
#define PN533_INIT_TARGET_RESP_ACTIVE     0x1
#define PN533_INIT_TARGET_RESP_DEP        0x4

enum  pn533_protocol_type {
	PN533_PROTO_REQ_ACK_RESP = 0,
	PN533_PROTO_REQ_RESP
};

struct pn533 {
	struct usb_device *udev;
	struct usb_interface *interface;
	struct nfc_dev *nfc_dev;
	u32 device_type;
	enum pn533_protocol_type protocol_type;

	struct urb *out_urb;
	struct urb *in_urb;

	struct sk_buff_head resp_q;
	struct sk_buff_head fragment_skb;

	struct workqueue_struct	*wq;
	struct work_struct cmd_work;
	struct work_struct cmd_complete_work;
	struct delayed_work poll_work;
	struct work_struct mi_rx_work;
	struct work_struct mi_tx_work;
	struct work_struct tg_work;
	struct work_struct rf_work;

	struct list_head cmd_queue;
	struct pn533_cmd *cmd;
	u8 cmd_pending;
	struct mutex cmd_lock;  /* protects cmd queue */

	void *cmd_complete_mi_arg;
	void *cmd_complete_dep_arg;

	struct pn533_poll_modulations *poll_mod_active[PN533_POLL_MOD_MAX + 1];
	u8 poll_mod_count;
	u8 poll_mod_curr;
	u32 poll_protocols;
	u32 listen_protocols;
	struct timer_list listen_timer;
	int cancel_listen;

	u8 *gb;
	size_t gb_len;

	u8 tgt_available_prots;
	u8 tgt_active_prot;
	u8 tgt_mode;

	struct pn533_frame_ops *ops;
};

struct pn533_cmd {
	struct list_head queue;
	u8 code;
	int status;
	struct sk_buff *req;
	struct sk_buff *resp;
	int resp_len;
	pn533_send_async_complete_t  complete_cb;
	void *complete_cb_context;
};

struct pn533_std_frame {
	u8 preamble;
	__be16 start_frame;
	u8 datalen;
	u8 datalen_checksum;
	u8 data[];
} __packed;

struct pn533_ext_frame {	/* Extended Information frame */
	u8 preamble;
	__be16 start_frame;
	__be16 eif_flag;	/* fixed to 0xFFFF */
	__be16 datalen;
	u8 datalen_checksum;
	u8 data[];
} __packed;

struct pn533_frame_ops {
	void (*tx_frame_init)(void *frame, u8 cmd_code);
	void (*tx_frame_finish)(void *frame);
	void (*tx_update_payload_len)(void *frame, int len);
	int tx_header_len;
	int tx_tail_len;

	bool (*rx_is_frame_valid)(void *frame, struct pn533 *dev);
	int (*rx_frame_size)(void *frame);
	int rx_header_len;
	int rx_tail_len;

	int max_payload_len;
	u8 (*get_cmd_code)(void *frame);
};

struct pn533_acr122_ccid_hdr {
	u8 type;
	u32 datalen;
	u8 slot;
	u8 seq;
	u8 params[3]; /* 3 msg specific bytes or status, error and 1 specific
			 byte for reposnse msg */
	u8 data[]; /* payload */
} __packed;

struct pn533_acr122_apdu_hdr {
	u8 class;
	u8 ins;
	u8 p1;
	u8 p2;
} __packed;

struct pn533_acr122_tx_frame {
	struct pn533_acr122_ccid_hdr ccid;
	struct pn533_acr122_apdu_hdr apdu;
	u8 datalen;
	u8 data[]; /* pn533 frame: TFI ... */
} __packed;

struct pn533_acr122_rx_frame {
	struct pn533_acr122_ccid_hdr ccid;
	u8 data[]; /* pn533 frame : TFI ... */
} __packed;

static void pn533_acr122_tx_frame_init(void *_frame, u8 cmd_code)
{
	struct pn533_acr122_tx_frame *frame = _frame;

	frame->ccid.type = PN533_ACR122_PC_TO_RDR_ESCAPE;
	frame->ccid.datalen = sizeof(frame->apdu) + 1; /* sizeof(apdu_hdr) +
							  sizeof(datalen) */
	frame->ccid.slot = 0;
	frame->ccid.seq = 0;
	frame->ccid.params[0] = 0;
	frame->ccid.params[1] = 0;
	frame->ccid.params[2] = 0;

	frame->data[0] = PN533_STD_FRAME_DIR_OUT;
	frame->data[1] = cmd_code;
	frame->datalen = 2;  /* data[0] + data[1] */

	frame->apdu.class = 0xFF;
	frame->apdu.ins = 0;
	frame->apdu.p1 = 0;
	frame->apdu.p2 = 0;
}

static void pn533_acr122_tx_frame_finish(void *_frame)
{
	struct pn533_acr122_tx_frame *frame = _frame;

	frame->ccid.datalen += frame->datalen;
}

static void pn533_acr122_tx_update_payload_len(void *_frame, int len)
{
	struct pn533_acr122_tx_frame *frame = _frame;

	frame->datalen += len;
}

static bool pn533_acr122_is_rx_frame_valid(void *_frame, struct pn533 *dev)
{
	struct pn533_acr122_rx_frame *frame = _frame;

	if (frame->ccid.type != 0x83)
		return false;

	if (frame->data[frame->ccid.datalen - 2] == 0x63)
		return false;

	return true;
}

static int pn533_acr122_rx_frame_size(void *frame)
{
	struct pn533_acr122_rx_frame *f = frame;

	/* f->ccid.datalen already includes tail length */
	return sizeof(struct pn533_acr122_rx_frame) + f->ccid.datalen;
}

static u8 pn533_acr122_get_cmd_code(void *frame)
{
	struct pn533_acr122_rx_frame *f = frame;

	return PN533_FRAME_CMD(f);
}

static struct pn533_frame_ops pn533_acr122_frame_ops = {
	.tx_frame_init = pn533_acr122_tx_frame_init,
	.tx_frame_finish = pn533_acr122_tx_frame_finish,
	.tx_update_payload_len = pn533_acr122_tx_update_payload_len,
	.tx_header_len = PN533_ACR122_TX_FRAME_HEADER_LEN,
	.tx_tail_len = PN533_ACR122_TX_FRAME_TAIL_LEN,

	.rx_is_frame_valid = pn533_acr122_is_rx_frame_valid,
	.rx_header_len = PN533_ACR122_RX_FRAME_HEADER_LEN,
	.rx_tail_len = PN533_ACR122_RX_FRAME_TAIL_LEN,
	.rx_frame_size = pn533_acr122_rx_frame_size,

	.max_payload_len = PN533_ACR122_FRAME_MAX_PAYLOAD_LEN,
	.get_cmd_code = pn533_acr122_get_cmd_code,
};

/* The rule: value(high byte) + value(low byte) + checksum = 0 */
static inline u8 pn533_ext_checksum(u16 value)
{
	return ~(u8)(((value & 0xFF00) >> 8) + (u8)(value & 0xFF)) + 1;
}

/* The rule: value + checksum = 0 */
static inline u8 pn533_std_checksum(u8 value)
{
	return ~value + 1;
}

/* The rule: sum(data elements) + checksum = 0 */
static u8 pn533_std_data_checksum(u8 *data, int datalen)
{
	u8 sum = 0;
	int i;

	for (i = 0; i < datalen; i++)
		sum += data[i];

	return pn533_std_checksum(sum);
}

static void pn533_std_tx_frame_init(void *_frame, u8 cmd_code)
{
	struct pn533_std_frame *frame = _frame;

	frame->preamble = 0;
	frame->start_frame = cpu_to_be16(PN533_STD_FRAME_SOF);
	PN533_STD_FRAME_IDENTIFIER(frame) = PN533_STD_FRAME_DIR_OUT;
	PN533_FRAME_CMD(frame) = cmd_code;
	frame->datalen = 2;
}

static void pn533_std_tx_frame_finish(void *_frame)
{
	struct pn533_std_frame *frame = _frame;

	frame->datalen_checksum = pn533_std_checksum(frame->datalen);

	PN533_STD_FRAME_CHECKSUM(frame) =
		pn533_std_data_checksum(frame->data, frame->datalen);

	PN533_STD_FRAME_POSTAMBLE(frame) = 0;
}

static void pn533_std_tx_update_payload_len(void *_frame, int len)
{
	struct pn533_std_frame *frame = _frame;

	frame->datalen += len;
}

static bool pn533_std_rx_frame_is_valid(void *_frame, struct pn533 *dev)
{
	u8 checksum;
	struct pn533_std_frame *stdf = _frame;

	if (stdf->start_frame != cpu_to_be16(PN533_STD_FRAME_SOF))
		return false;

	if (likely(!PN533_STD_IS_EXTENDED(stdf))) {
		/* Standard frame code */
		dev->ops->rx_header_len = PN533_STD_FRAME_HEADER_LEN;

		checksum = pn533_std_checksum(stdf->datalen);
		if (checksum != stdf->datalen_checksum)
			return false;

		checksum = pn533_std_data_checksum(stdf->data, stdf->datalen);
		if (checksum != PN533_STD_FRAME_CHECKSUM(stdf))
			return false;
	} else {
		/* Extended */
		struct pn533_ext_frame *eif = _frame;

		dev->ops->rx_header_len = PN533_EXT_FRAME_HEADER_LEN;

		checksum = pn533_ext_checksum(be16_to_cpu(eif->datalen));
		if (checksum != eif->datalen_checksum)
			return false;

		/* check data checksum */
		checksum = pn533_std_data_checksum(eif->data,
						   be16_to_cpu(eif->datalen));
		if (checksum != PN533_EXT_FRAME_CHECKSUM(eif))
			return false;
	}

	return true;
}

static bool pn533_std_rx_frame_is_ack(struct pn533_std_frame *frame)
{
	if (frame->start_frame != cpu_to_be16(PN533_STD_FRAME_SOF))
		return false;

	if (frame->datalen != 0 || frame->datalen_checksum != 0xFF)
		return false;

	return true;
}

static inline int pn533_std_rx_frame_size(void *frame)
{
	struct pn533_std_frame *f = frame;

	/* check for Extended Information frame */
	if (PN533_STD_IS_EXTENDED(f)) {
		struct pn533_ext_frame *eif = frame;

		return sizeof(struct pn533_ext_frame)
			+ be16_to_cpu(eif->datalen) + PN533_STD_FRAME_TAIL_LEN;
	}

	return sizeof(struct pn533_std_frame) + f->datalen +
	       PN533_STD_FRAME_TAIL_LEN;
}

static u8 pn533_std_get_cmd_code(void *frame)
{
	struct pn533_std_frame *f = frame;
	struct pn533_ext_frame *eif = frame;

	if (PN533_STD_IS_EXTENDED(f))
		return PN533_FRAME_CMD(eif);
	else
		return PN533_FRAME_CMD(f);
}

static struct pn533_frame_ops pn533_std_frame_ops = {
	.tx_frame_init = pn533_std_tx_frame_init,
	.tx_frame_finish = pn533_std_tx_frame_finish,
	.tx_update_payload_len = pn533_std_tx_update_payload_len,
	.tx_header_len = PN533_STD_FRAME_HEADER_LEN,
	.tx_tail_len = PN533_STD_FRAME_TAIL_LEN,

	.rx_is_frame_valid = pn533_std_rx_frame_is_valid,
	.rx_frame_size = pn533_std_rx_frame_size,
	.rx_header_len = PN533_STD_FRAME_HEADER_LEN,
	.rx_tail_len = PN533_STD_FRAME_TAIL_LEN,

	.max_payload_len =  PN533_STD_FRAME_MAX_PAYLOAD_LEN,
	.get_cmd_code = pn533_std_get_cmd_code,
};

static bool pn533_rx_frame_is_cmd_response(struct pn533 *dev, void *frame)
{
	return (dev->ops->get_cmd_code(frame) ==
				PN533_CMD_RESPONSE(dev->cmd->code));
}

static void pn533_recv_response(struct urb *urb)
{
	struct pn533 *dev = urb->context;
	struct pn533_cmd *cmd = dev->cmd;
	u8 *in_frame;

	cmd->status = urb->status;

	switch (urb->status) {
	case 0:
		break; /* success */
	case -ECONNRESET:
	case -ENOENT:
		nfc_dev_dbg(&dev->interface->dev,
			    "The urb has been canceled (status %d)",
			    urb->status);
		goto sched_wq;
	case -ESHUTDOWN:
	default:
		nfc_dev_err(&dev->interface->dev,
			    "Urb failure (status %d)", urb->status);
		goto sched_wq;
	}

	in_frame = dev->in_urb->transfer_buffer;

	nfc_dev_dbg(&dev->interface->dev, "Received a frame.");
	print_hex_dump_debug("PN533 RX: ", DUMP_PREFIX_NONE, 16, 1, in_frame,
			     dev->ops->rx_frame_size(in_frame), false);

	if (!dev->ops->rx_is_frame_valid(in_frame, dev)) {
		nfc_dev_err(&dev->interface->dev, "Received an invalid frame");
		cmd->status = -EIO;
		goto sched_wq;
	}

	if (!pn533_rx_frame_is_cmd_response(dev, in_frame)) {
		nfc_dev_err(&dev->interface->dev,
			    "It it not the response to the last command");
		cmd->status = -EIO;
		goto sched_wq;
	}

sched_wq:
	queue_work(dev->wq, &dev->cmd_complete_work);
}

static int pn533_submit_urb_for_response(struct pn533 *dev, gfp_t flags)
{
	dev->in_urb->complete = pn533_recv_response;

	return usb_submit_urb(dev->in_urb, flags);
}

static void pn533_recv_ack(struct urb *urb)
{
	struct pn533 *dev = urb->context;
	struct pn533_cmd *cmd = dev->cmd;
	struct pn533_std_frame *in_frame;
	int rc;

	cmd->status = urb->status;

	switch (urb->status) {
	case 0:
		break; /* success */
	case -ECONNRESET:
	case -ENOENT:
		nfc_dev_dbg(&dev->interface->dev,
			    "The urb has been stopped (status %d)",
			    urb->status);
		goto sched_wq;
	case -ESHUTDOWN:
	default:
		nfc_dev_err(&dev->interface->dev,
			    "Urb failure (status %d)", urb->status);
		goto sched_wq;
	}

	in_frame = dev->in_urb->transfer_buffer;

	if (!pn533_std_rx_frame_is_ack(in_frame)) {
		nfc_dev_err(&dev->interface->dev, "Received an invalid ack");
		cmd->status = -EIO;
		goto sched_wq;
	}

	rc = pn533_submit_urb_for_response(dev, GFP_ATOMIC);
	if (rc) {
		nfc_dev_err(&dev->interface->dev,
			    "usb_submit_urb failed with result %d", rc);
		cmd->status = rc;
		goto sched_wq;
	}

	return;

sched_wq:
	queue_work(dev->wq, &dev->cmd_complete_work);
}

static int pn533_submit_urb_for_ack(struct pn533 *dev, gfp_t flags)
{
	dev->in_urb->complete = pn533_recv_ack;

	return usb_submit_urb(dev->in_urb, flags);
}

static int pn533_send_ack(struct pn533 *dev, gfp_t flags)
{
	u8 ack[PN533_STD_FRAME_ACK_SIZE] = {0x00, 0x00, 0xff, 0x00, 0xff, 0x00};
	/* spec 7.1.1.3:  Preamble, SoPC (2), ACK Code (2), Postamble */
	int rc;

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	dev->out_urb->transfer_buffer = ack;
	dev->out_urb->transfer_buffer_length = sizeof(ack);
	rc = usb_submit_urb(dev->out_urb, flags);

	return rc;
}

static int __pn533_send_frame_async(struct pn533 *dev,
					struct sk_buff *out,
					struct sk_buff *in,
					int in_len)
{
	int rc;

	dev->out_urb->transfer_buffer = out->data;
	dev->out_urb->transfer_buffer_length = out->len;

	dev->in_urb->transfer_buffer = in->data;
	dev->in_urb->transfer_buffer_length = in_len;

	print_hex_dump_debug("PN533 TX: ", DUMP_PREFIX_NONE, 16, 1,
			     out->data, out->len, false);

	rc = usb_submit_urb(dev->out_urb, GFP_KERNEL);
	if (rc)
		return rc;

	if (dev->protocol_type == PN533_PROTO_REQ_RESP) {
		/* request for response for sent packet directly */
		rc = pn533_submit_urb_for_response(dev, GFP_ATOMIC);
		if (rc)
			goto error;
	} else if (dev->protocol_type == PN533_PROTO_REQ_ACK_RESP) {
		/* request for ACK if that's the case */
		rc = pn533_submit_urb_for_ack(dev, GFP_KERNEL);
		if (rc)
			goto error;
	}

	return 0;

error:
	usb_unlink_urb(dev->out_urb);
	return rc;
}

static void pn533_build_cmd_frame(struct pn533 *dev, u8 cmd_code,
				  struct sk_buff *skb)
{
	/* payload is already there, just update datalen */
	int payload_len = skb->len;
	struct pn533_frame_ops *ops = dev->ops;


	skb_push(skb, ops->tx_header_len);
	skb_put(skb, ops->tx_tail_len);

	ops->tx_frame_init(skb->data, cmd_code);
	ops->tx_update_payload_len(skb->data, payload_len);
	ops->tx_frame_finish(skb->data);
}

static int pn533_send_async_complete(struct pn533 *dev)
{
	struct pn533_cmd *cmd = dev->cmd;
	int status = cmd->status;

	struct sk_buff *req = cmd->req;
	struct sk_buff *resp = cmd->resp;

	int rc;

	dev_kfree_skb(req);

	if (status < 0) {
		rc = cmd->complete_cb(dev, cmd->complete_cb_context,
				      ERR_PTR(status));
		dev_kfree_skb(resp);
		goto done;
	}

	skb_put(resp, dev->ops->rx_frame_size(resp->data));
	skb_pull(resp, dev->ops->rx_header_len);
	skb_trim(resp, resp->len - dev->ops->rx_tail_len);

	rc = cmd->complete_cb(dev, cmd->complete_cb_context, resp);

done:
	kfree(cmd);
	dev->cmd = NULL;
	return rc;
}

static int __pn533_send_async(struct pn533 *dev, u8 cmd_code,
			      struct sk_buff *req, struct sk_buff *resp,
			      int resp_len,
			      pn533_send_async_complete_t complete_cb,
			      void *complete_cb_context)
{
	struct pn533_cmd *cmd;
	int rc = 0;

	nfc_dev_dbg(&dev->interface->dev, "Sending command 0x%x", cmd_code);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->code = cmd_code;
	cmd->req = req;
	cmd->resp = resp;
	cmd->resp_len = resp_len;
	cmd->complete_cb = complete_cb;
	cmd->complete_cb_context = complete_cb_context;

	pn533_build_cmd_frame(dev, cmd_code, req);

	mutex_lock(&dev->cmd_lock);

	if (!dev->cmd_pending) {
		rc = __pn533_send_frame_async(dev, req, resp, resp_len);
		if (rc)
			goto error;

		dev->cmd_pending = 1;
		dev->cmd = cmd;
		goto unlock;
	}

	nfc_dev_dbg(&dev->interface->dev, "%s Queueing command 0x%x", __func__,
		    cmd_code);

	INIT_LIST_HEAD(&cmd->queue);
	list_add_tail(&cmd->queue, &dev->cmd_queue);

	goto unlock;

error:
	kfree(cmd);
unlock:
	mutex_unlock(&dev->cmd_lock);
	return rc;
}

static int pn533_send_data_async(struct pn533 *dev, u8 cmd_code,
				 struct sk_buff *req,
				 pn533_send_async_complete_t complete_cb,
				 void *complete_cb_context)
{
	struct sk_buff *resp;
	int rc;
	int  resp_len = dev->ops->rx_header_len +
			dev->ops->max_payload_len +
			dev->ops->rx_tail_len;

	resp = nfc_alloc_recv_skb(resp_len, GFP_KERNEL);
	if (!resp)
		return -ENOMEM;

	rc = __pn533_send_async(dev, cmd_code, req, resp, resp_len, complete_cb,
				complete_cb_context);
	if (rc)
		dev_kfree_skb(resp);

	return rc;
}

static int pn533_send_cmd_async(struct pn533 *dev, u8 cmd_code,
				struct sk_buff *req,
				pn533_send_async_complete_t complete_cb,
				void *complete_cb_context)
{
	struct sk_buff *resp;
	int rc;
	int  resp_len = dev->ops->rx_header_len +
			dev->ops->max_payload_len +
			dev->ops->rx_tail_len;

	resp = alloc_skb(resp_len, GFP_KERNEL);
	if (!resp)
		return -ENOMEM;

	rc = __pn533_send_async(dev, cmd_code, req, resp, resp_len, complete_cb,
				complete_cb_context);
	if (rc)
		dev_kfree_skb(resp);

	return rc;
}

/*
 * pn533_send_cmd_direct_async
 *
 * The function sends a piority cmd directly to the chip omiting the cmd
 * queue. It's intended to be used by chaining mechanism of received responses
 * where the host has to request every single chunk of data before scheduling
 * next cmd from the queue.
 */
static int pn533_send_cmd_direct_async(struct pn533 *dev, u8 cmd_code,
				       struct sk_buff *req,
				       pn533_send_async_complete_t complete_cb,
				       void *complete_cb_context)
{
	struct sk_buff *resp;
	struct pn533_cmd *cmd;
	int rc;
	int resp_len = dev->ops->rx_header_len +
		       dev->ops->max_payload_len +
		       dev->ops->rx_tail_len;

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

	pn533_build_cmd_frame(dev, cmd_code, req);

	rc = __pn533_send_frame_async(dev, req, resp, resp_len);
	if (rc < 0) {
		dev_kfree_skb(resp);
		kfree(cmd);
	} else {
		dev->cmd = cmd;
	}

	return rc;
}

static void pn533_wq_cmd_complete(struct work_struct *work)
{
	struct pn533 *dev = container_of(work, struct pn533, cmd_complete_work);
	int rc;

	rc = pn533_send_async_complete(dev);
	if (rc != -EINPROGRESS)
		queue_work(dev->wq, &dev->cmd_work);
}

static void pn533_wq_cmd(struct work_struct *work)
{
	struct pn533 *dev = container_of(work, struct pn533, cmd_work);
	struct pn533_cmd *cmd;
	int rc;

	mutex_lock(&dev->cmd_lock);

	if (list_empty(&dev->cmd_queue)) {
		dev->cmd_pending = 0;
		mutex_unlock(&dev->cmd_lock);
		return;
	}

	cmd = list_first_entry(&dev->cmd_queue, struct pn533_cmd, queue);

	list_del(&cmd->queue);

	mutex_unlock(&dev->cmd_lock);

	rc = __pn533_send_frame_async(dev, cmd->req, cmd->resp, cmd->resp_len);
	if (rc < 0) {
		dev_kfree_skb(cmd->req);
		dev_kfree_skb(cmd->resp);
		kfree(cmd);
		return;
	}

	dev->cmd = cmd;
}

struct pn533_sync_cmd_response {
	struct sk_buff *resp;
	struct completion done;
};

static int pn533_send_sync_complete(struct pn533 *dev, void *_arg,
				    struct sk_buff *resp)
{
	struct pn533_sync_cmd_response *arg = _arg;

	arg->resp = resp;
	complete(&arg->done);

	return 0;
}

/*  pn533_send_cmd_sync
 *
 *  Please note the req parameter is freed inside the function to
 *  limit a number of return value interpretations by the caller.
 *
 *  1. negative in case of error during TX path -> req should be freed
 *
 *  2. negative in case of error during RX path -> req should not be freed
 *     as it's been already freed at the begining of RX path by
 *     async_complete_cb.
 *
 *  3. valid pointer in case of succesfult RX path
 *
 *  A caller has to check a return value with IS_ERR macro. If the test pass,
 *  the returned pointer is valid.
 *
 * */
static struct sk_buff *pn533_send_cmd_sync(struct pn533 *dev, u8 cmd_code,
					       struct sk_buff *req)
{
	int rc;
	struct pn533_sync_cmd_response arg;

	init_completion(&arg.done);

	rc = pn533_send_cmd_async(dev, cmd_code, req,
				  pn533_send_sync_complete, &arg);
	if (rc) {
		dev_kfree_skb(req);
		return ERR_PTR(rc);
	}

	wait_for_completion(&arg.done);

	return arg.resp;
}

static void pn533_send_complete(struct urb *urb)
{
	struct pn533 *dev = urb->context;

	switch (urb->status) {
	case 0:
		break; /* success */
	case -ECONNRESET:
	case -ENOENT:
		nfc_dev_dbg(&dev->interface->dev,
			    "The urb has been stopped (status %d)",
			    urb->status);
		break;
	case -ESHUTDOWN:
	default:
		nfc_dev_err(&dev->interface->dev,
			    "Urb failure (status %d)", urb->status);
	}
}

static void pn533_abort_cmd(struct pn533 *dev, gfp_t flags)
{
	/* ACR122U does not support any command which aborts last
	 * issued command i.e. as ACK for standard PN533. Additionally,
	 * it behaves stange, sending broken or incorrect responses,
	 * when we cancel urb before the chip will send response.
	 */
	if (dev->device_type == PN533_DEVICE_ACR122U)
		return;

	/* An ack will cancel the last issued command */
	pn533_send_ack(dev, flags);

	/* cancel the urb request */
	usb_kill_urb(dev->in_urb);
}

static struct sk_buff *pn533_alloc_skb(struct pn533 *dev, unsigned int size)
{
	struct sk_buff *skb;

	skb = alloc_skb(dev->ops->tx_header_len +
			size +
			dev->ops->tx_tail_len, GFP_KERNEL);

	if (skb)
		skb_reserve(skb, dev->ops->tx_header_len);

	return skb;
}

struct pn533_target_type_a {
	__be16 sens_res;
	u8 sel_res;
	u8 nfcid_len;
	u8 nfcid_data[];
} __packed;


#define PN533_TYPE_A_SENS_RES_NFCID1(x) ((u8)((be16_to_cpu(x) & 0x00C0) >> 6))
#define PN533_TYPE_A_SENS_RES_SSD(x) ((u8)((be16_to_cpu(x) & 0x001F) >> 0))
#define PN533_TYPE_A_SENS_RES_PLATCONF(x) ((u8)((be16_to_cpu(x) & 0x0F00) >> 8))

#define PN533_TYPE_A_SENS_RES_SSD_JEWEL 0x00
#define PN533_TYPE_A_SENS_RES_PLATCONF_JEWEL 0x0C

#define PN533_TYPE_A_SEL_PROT(x) (((x) & 0x60) >> 5)
#define PN533_TYPE_A_SEL_CASCADE(x) (((x) & 0x04) >> 2)

#define PN533_TYPE_A_SEL_PROT_MIFARE 0
#define PN533_TYPE_A_SEL_PROT_ISO14443 1
#define PN533_TYPE_A_SEL_PROT_DEP 2
#define PN533_TYPE_A_SEL_PROT_ISO14443_DEP 3

static bool pn533_target_type_a_is_valid(struct pn533_target_type_a *type_a,
							int target_data_len)
{
	u8 ssd;
	u8 platconf;

	if (target_data_len < sizeof(struct pn533_target_type_a))
		return false;

	/* The lenght check of nfcid[] and ats[] are not being performed because
	   the values are not being used */

	/* Requirement 4.6.3.3 from NFC Forum Digital Spec */
	ssd = PN533_TYPE_A_SENS_RES_SSD(type_a->sens_res);
	platconf = PN533_TYPE_A_SENS_RES_PLATCONF(type_a->sens_res);

	if ((ssd == PN533_TYPE_A_SENS_RES_SSD_JEWEL &&
	     platconf != PN533_TYPE_A_SENS_RES_PLATCONF_JEWEL) ||
	    (ssd != PN533_TYPE_A_SENS_RES_SSD_JEWEL &&
	     platconf == PN533_TYPE_A_SENS_RES_PLATCONF_JEWEL))
		return false;

	/* Requirements 4.8.2.1, 4.8.2.3, 4.8.2.5 and 4.8.2.7 from NFC Forum */
	if (PN533_TYPE_A_SEL_CASCADE(type_a->sel_res) != 0)
		return false;

	return true;
}

static int pn533_target_found_type_a(struct nfc_target *nfc_tgt, u8 *tgt_data,
							int tgt_data_len)
{
	struct pn533_target_type_a *tgt_type_a;

	tgt_type_a = (struct pn533_target_type_a *)tgt_data;

	if (!pn533_target_type_a_is_valid(tgt_type_a, tgt_data_len))
		return -EPROTO;

	switch (PN533_TYPE_A_SEL_PROT(tgt_type_a->sel_res)) {
	case PN533_TYPE_A_SEL_PROT_MIFARE:
		nfc_tgt->supported_protocols = NFC_PROTO_MIFARE_MASK;
		break;
	case PN533_TYPE_A_SEL_PROT_ISO14443:
		nfc_tgt->supported_protocols = NFC_PROTO_ISO14443_MASK;
		break;
	case PN533_TYPE_A_SEL_PROT_DEP:
		nfc_tgt->supported_protocols = NFC_PROTO_NFC_DEP_MASK;
		break;
	case PN533_TYPE_A_SEL_PROT_ISO14443_DEP:
		nfc_tgt->supported_protocols = NFC_PROTO_ISO14443_MASK |
							NFC_PROTO_NFC_DEP_MASK;
		break;
	}

	nfc_tgt->sens_res = be16_to_cpu(tgt_type_a->sens_res);
	nfc_tgt->sel_res = tgt_type_a->sel_res;
	nfc_tgt->nfcid1_len = tgt_type_a->nfcid_len;
	memcpy(nfc_tgt->nfcid1, tgt_type_a->nfcid_data, nfc_tgt->nfcid1_len);

	return 0;
}

struct pn533_target_felica {
	u8 pol_res;
	u8 opcode;
	u8 nfcid2[NFC_NFCID2_MAXSIZE];
	u8 pad[8];
	/* optional */
	u8 syst_code[];
} __packed;

#define PN533_FELICA_SENSF_NFCID2_DEP_B1 0x01
#define PN533_FELICA_SENSF_NFCID2_DEP_B2 0xFE

static bool pn533_target_felica_is_valid(struct pn533_target_felica *felica,
							int target_data_len)
{
	if (target_data_len < sizeof(struct pn533_target_felica))
		return false;

	if (felica->opcode != PN533_FELICA_OPC_SENSF_RES)
		return false;

	return true;
}

static int pn533_target_found_felica(struct nfc_target *nfc_tgt, u8 *tgt_data,
							int tgt_data_len)
{
	struct pn533_target_felica *tgt_felica;

	tgt_felica = (struct pn533_target_felica *)tgt_data;

	if (!pn533_target_felica_is_valid(tgt_felica, tgt_data_len))
		return -EPROTO;

	if ((tgt_felica->nfcid2[0] == PN533_FELICA_SENSF_NFCID2_DEP_B1) &&
	    (tgt_felica->nfcid2[1] == PN533_FELICA_SENSF_NFCID2_DEP_B2))
		nfc_tgt->supported_protocols = NFC_PROTO_NFC_DEP_MASK;
	else
		nfc_tgt->supported_protocols = NFC_PROTO_FELICA_MASK;

	memcpy(nfc_tgt->sensf_res, &tgt_felica->opcode, 9);
	nfc_tgt->sensf_res_len = 9;

	memcpy(nfc_tgt->nfcid2, tgt_felica->nfcid2, NFC_NFCID2_MAXSIZE);
	nfc_tgt->nfcid2_len = NFC_NFCID2_MAXSIZE;

	return 0;
}

struct pn533_target_jewel {
	__be16 sens_res;
	u8 jewelid[4];
} __packed;

static bool pn533_target_jewel_is_valid(struct pn533_target_jewel *jewel,
							int target_data_len)
{
	u8 ssd;
	u8 platconf;

	if (target_data_len < sizeof(struct pn533_target_jewel))
		return false;

	/* Requirement 4.6.3.3 from NFC Forum Digital Spec */
	ssd = PN533_TYPE_A_SENS_RES_SSD(jewel->sens_res);
	platconf = PN533_TYPE_A_SENS_RES_PLATCONF(jewel->sens_res);

	if ((ssd == PN533_TYPE_A_SENS_RES_SSD_JEWEL &&
	     platconf != PN533_TYPE_A_SENS_RES_PLATCONF_JEWEL) ||
	    (ssd != PN533_TYPE_A_SENS_RES_SSD_JEWEL &&
	     platconf == PN533_TYPE_A_SENS_RES_PLATCONF_JEWEL))
		return false;

	return true;
}

static int pn533_target_found_jewel(struct nfc_target *nfc_tgt, u8 *tgt_data,
							int tgt_data_len)
{
	struct pn533_target_jewel *tgt_jewel;

	tgt_jewel = (struct pn533_target_jewel *)tgt_data;

	if (!pn533_target_jewel_is_valid(tgt_jewel, tgt_data_len))
		return -EPROTO;

	nfc_tgt->supported_protocols = NFC_PROTO_JEWEL_MASK;
	nfc_tgt->sens_res = be16_to_cpu(tgt_jewel->sens_res);
	nfc_tgt->nfcid1_len = 4;
	memcpy(nfc_tgt->nfcid1, tgt_jewel->jewelid, nfc_tgt->nfcid1_len);

	return 0;
}

struct pn533_type_b_prot_info {
	u8 bitrate;
	u8 fsci_type;
	u8 fwi_adc_fo;
} __packed;

#define PN533_TYPE_B_PROT_FCSI(x) (((x) & 0xF0) >> 4)
#define PN533_TYPE_B_PROT_TYPE(x) (((x) & 0x0F) >> 0)
#define PN533_TYPE_B_PROT_TYPE_RFU_MASK 0x8

struct pn533_type_b_sens_res {
	u8 opcode;
	u8 nfcid[4];
	u8 appdata[4];
	struct pn533_type_b_prot_info prot_info;
} __packed;

#define PN533_TYPE_B_OPC_SENSB_RES 0x50

struct pn533_target_type_b {
	struct pn533_type_b_sens_res sensb_res;
	u8 attrib_res_len;
	u8 attrib_res[];
} __packed;

static bool pn533_target_type_b_is_valid(struct pn533_target_type_b *type_b,
							int target_data_len)
{
	if (target_data_len < sizeof(struct pn533_target_type_b))
		return false;

	if (type_b->sensb_res.opcode != PN533_TYPE_B_OPC_SENSB_RES)
		return false;

	if (PN533_TYPE_B_PROT_TYPE(type_b->sensb_res.prot_info.fsci_type) &
						PN533_TYPE_B_PROT_TYPE_RFU_MASK)
		return false;

	return true;
}

static int pn533_target_found_type_b(struct nfc_target *nfc_tgt, u8 *tgt_data,
							int tgt_data_len)
{
	struct pn533_target_type_b *tgt_type_b;

	tgt_type_b = (struct pn533_target_type_b *)tgt_data;

	if (!pn533_target_type_b_is_valid(tgt_type_b, tgt_data_len))
		return -EPROTO;

	nfc_tgt->supported_protocols = NFC_PROTO_ISO14443_B_MASK;

	return 0;
}

static int pn533_target_found(struct pn533 *dev, u8 tg, u8 *tgdata,
			      int tgdata_len)
{
	struct nfc_target nfc_tgt;
	int rc;

	nfc_dev_dbg(&dev->interface->dev, "%s - modulation=%d", __func__,
		    dev->poll_mod_curr);

	if (tg != 1)
		return -EPROTO;

	memset(&nfc_tgt, 0, sizeof(struct nfc_target));

	switch (dev->poll_mod_curr) {
	case PN533_POLL_MOD_106KBPS_A:
		rc = pn533_target_found_type_a(&nfc_tgt, tgdata, tgdata_len);
		break;
	case PN533_POLL_MOD_212KBPS_FELICA:
	case PN533_POLL_MOD_424KBPS_FELICA:
		rc = pn533_target_found_felica(&nfc_tgt, tgdata, tgdata_len);
		break;
	case PN533_POLL_MOD_106KBPS_JEWEL:
		rc = pn533_target_found_jewel(&nfc_tgt, tgdata, tgdata_len);
		break;
	case PN533_POLL_MOD_847KBPS_B:
		rc = pn533_target_found_type_b(&nfc_tgt, tgdata, tgdata_len);
		break;
	default:
		nfc_dev_err(&dev->interface->dev,
			    "Unknown current poll modulation");
		return -EPROTO;
	}

	if (rc)
		return rc;

	if (!(nfc_tgt.supported_protocols & dev->poll_protocols)) {
		nfc_dev_dbg(&dev->interface->dev,
			    "The Tg found doesn't have the desired protocol");
		return -EAGAIN;
	}

	nfc_dev_dbg(&dev->interface->dev,
		    "Target found - supported protocols: 0x%x",
		    nfc_tgt.supported_protocols);

	dev->tgt_available_prots = nfc_tgt.supported_protocols;

	nfc_targets_found(dev->nfc_dev, &nfc_tgt, 1);

	return 0;
}

static inline void pn533_poll_next_mod(struct pn533 *dev)
{
	dev->poll_mod_curr = (dev->poll_mod_curr + 1) % dev->poll_mod_count;
}

static void pn533_poll_reset_mod_list(struct pn533 *dev)
{
	dev->poll_mod_count = 0;
}

static void pn533_poll_add_mod(struct pn533 *dev, u8 mod_index)
{
	dev->poll_mod_active[dev->poll_mod_count] =
		(struct pn533_poll_modulations *)&poll_mod[mod_index];
	dev->poll_mod_count++;
}

static void pn533_poll_create_mod_list(struct pn533 *dev,
				       u32 im_protocols, u32 tm_protocols)
{
	pn533_poll_reset_mod_list(dev);

	if ((im_protocols & NFC_PROTO_MIFARE_MASK) ||
	    (im_protocols & NFC_PROTO_ISO14443_MASK) ||
	    (im_protocols & NFC_PROTO_NFC_DEP_MASK))
		pn533_poll_add_mod(dev, PN533_POLL_MOD_106KBPS_A);

	if (im_protocols & NFC_PROTO_FELICA_MASK ||
	    im_protocols & NFC_PROTO_NFC_DEP_MASK) {
		pn533_poll_add_mod(dev, PN533_POLL_MOD_212KBPS_FELICA);
		pn533_poll_add_mod(dev, PN533_POLL_MOD_424KBPS_FELICA);
	}

	if (im_protocols & NFC_PROTO_JEWEL_MASK)
		pn533_poll_add_mod(dev, PN533_POLL_MOD_106KBPS_JEWEL);

	if (im_protocols & NFC_PROTO_ISO14443_B_MASK)
		pn533_poll_add_mod(dev, PN533_POLL_MOD_847KBPS_B);

	if (tm_protocols)
		pn533_poll_add_mod(dev, PN533_LISTEN_MOD);
}

static int pn533_start_poll_complete(struct pn533 *dev, struct sk_buff *resp)
{
	u8 nbtg, tg, *tgdata;
	int rc, tgdata_len;

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	nbtg = resp->data[0];
	tg = resp->data[1];
	tgdata = &resp->data[2];
	tgdata_len = resp->len - 2;  /* nbtg + tg */

	if (nbtg) {
		rc = pn533_target_found(dev, tg, tgdata, tgdata_len);

		/* We must stop the poll after a valid target found */
		if (rc == 0) {
			pn533_poll_reset_mod_list(dev);
			return 0;
		}
	}

	return -EAGAIN;
}

static struct sk_buff *pn533_alloc_poll_tg_frame(struct pn533 *dev)
{
	struct sk_buff *skb;
	u8 *felica, *nfcid3, *gb;

	u8 *gbytes = dev->gb;
	size_t gbytes_len = dev->gb_len;

	u8 felica_params[18] = {0x1, 0xfe, /* DEP */
				0x0, 0x0, 0x0, 0x0, 0x0, 0x0, /* random */
				0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
				0xff, 0xff}; /* System code */

	u8 mifare_params[6] = {0x1, 0x1, /* SENS_RES */
			       0x0, 0x0, 0x0,
			       0x40}; /* SEL_RES for DEP */

	unsigned int skb_len = 36 + /* mode (1), mifare (6),
				       felica (18), nfcid3 (10), gb_len (1) */
			       gbytes_len +
			       1;  /* len Tk*/

	skb = pn533_alloc_skb(dev, skb_len);
	if (!skb)
		return NULL;

	/* DEP support only */
	*skb_put(skb, 1) = PN533_INIT_TARGET_DEP;

	/* MIFARE params */
	memcpy(skb_put(skb, 6), mifare_params, 6);

	/* Felica params */
	felica = skb_put(skb, 18);
	memcpy(felica, felica_params, 18);
	get_random_bytes(felica + 2, 6);

	/* NFCID3 */
	nfcid3 = skb_put(skb, 10);
	memset(nfcid3, 0, 10);
	memcpy(nfcid3, felica, 8);

	/* General bytes */
	*skb_put(skb, 1) = gbytes_len;

	gb = skb_put(skb, gbytes_len);
	memcpy(gb, gbytes, gbytes_len);

	/* Len Tk */
	*skb_put(skb, 1) = 0;

	return skb;
}

#define PN533_CMD_DATAEXCH_HEAD_LEN 1
#define PN533_CMD_DATAEXCH_DATA_MAXLEN 262
static int pn533_tm_get_data_complete(struct pn533 *dev, void *arg,
				      struct sk_buff *resp)
{
	u8 status;

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	if (IS_ERR(resp))
		return PTR_ERR(resp);

	status = resp->data[0];
	skb_pull(resp, sizeof(status));

	if (status != 0) {
		nfc_tm_deactivated(dev->nfc_dev);
		dev->tgt_mode = 0;
		dev_kfree_skb(resp);
		return 0;
	}

	return nfc_tm_data_received(dev->nfc_dev, resp);
}

static void pn533_wq_tg_get_data(struct work_struct *work)
{
	struct pn533 *dev = container_of(work, struct pn533, tg_work);

	struct sk_buff *skb;
	int rc;

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	skb = pn533_alloc_skb(dev, 0);
	if (!skb)
		return;

	rc = pn533_send_data_async(dev, PN533_CMD_TG_GET_DATA, skb,
				   pn533_tm_get_data_complete, NULL);

	if (rc < 0)
		dev_kfree_skb(skb);

	return;
}

#define ATR_REQ_GB_OFFSET 17
static int pn533_init_target_complete(struct pn533 *dev, struct sk_buff *resp)
{
	u8 mode, *cmd, comm_mode = NFC_COMM_PASSIVE, *gb;
	size_t gb_len;
	int rc;

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	if (resp->len < ATR_REQ_GB_OFFSET + 1)
		return -EINVAL;

	mode = resp->data[0];
	cmd = &resp->data[1];

	nfc_dev_dbg(&dev->interface->dev, "Target mode 0x%x len %d\n",
		    mode, resp->len);

	if ((mode & PN533_INIT_TARGET_RESP_FRAME_MASK) ==
	    PN533_INIT_TARGET_RESP_ACTIVE)
		comm_mode = NFC_COMM_ACTIVE;

	if ((mode & PN533_INIT_TARGET_RESP_DEP) == 0)  /* Only DEP supported */
		return -EOPNOTSUPP;

	gb = cmd + ATR_REQ_GB_OFFSET;
	gb_len = resp->len - (ATR_REQ_GB_OFFSET + 1);

	rc = nfc_tm_activated(dev->nfc_dev, NFC_PROTO_NFC_DEP_MASK,
			      comm_mode, gb, gb_len);
	if (rc < 0) {
		nfc_dev_err(&dev->interface->dev,
			    "Error when signaling target activation");
		return rc;
	}

	dev->tgt_mode = 1;
	queue_work(dev->wq, &dev->tg_work);

	return 0;
}

static void pn533_listen_mode_timer(unsigned long data)
{
	struct pn533 *dev = (struct pn533 *)data;

	nfc_dev_dbg(&dev->interface->dev, "Listen mode timeout");

	dev->cancel_listen = 1;

	pn533_poll_next_mod(dev);

	queue_delayed_work(dev->wq, &dev->poll_work,
			   msecs_to_jiffies(PN533_POLL_INTERVAL));
}

static int pn533_rf_complete(struct pn533 *dev, void *arg,
			     struct sk_buff *resp)
{
	int rc = 0;

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);

		nfc_dev_err(&dev->interface->dev, "%s RF setting error %d",
			    __func__, rc);

		return rc;
	}

	queue_delayed_work(dev->wq, &dev->poll_work,
			   msecs_to_jiffies(PN533_POLL_INTERVAL));

	dev_kfree_skb(resp);
	return rc;
}

static void pn533_wq_rf(struct work_struct *work)
{
	struct pn533 *dev = container_of(work, struct pn533, rf_work);
	struct sk_buff *skb;
	int rc;

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	skb = pn533_alloc_skb(dev, 2);
	if (!skb)
		return;

	*skb_put(skb, 1) = PN533_CFGITEM_RF_FIELD;
	*skb_put(skb, 1) = PN533_CFGITEM_RF_FIELD_AUTO_RFCA;

	rc = pn533_send_cmd_async(dev, PN533_CMD_RF_CONFIGURATION, skb,
				  pn533_rf_complete, NULL);
	if (rc < 0) {
		dev_kfree_skb(skb);
		nfc_dev_err(&dev->interface->dev, "RF setting error %d", rc);
	}

	return;
}

static int pn533_poll_complete(struct pn533 *dev, void *arg,
			       struct sk_buff *resp)
{
	struct pn533_poll_modulations *cur_mod;
	int rc;

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);

		nfc_dev_err(&dev->interface->dev, "%s  Poll complete error %d",
			    __func__, rc);

		if (rc == -ENOENT) {
			if (dev->poll_mod_count != 0)
				return rc;
			else
				goto stop_poll;
		} else if (rc < 0) {
			nfc_dev_err(&dev->interface->dev,
				    "Error %d when running poll", rc);
			goto stop_poll;
		}
	}

	cur_mod = dev->poll_mod_active[dev->poll_mod_curr];

	if (cur_mod->len == 0) { /* Target mode */
		del_timer(&dev->listen_timer);
		rc = pn533_init_target_complete(dev, resp);
		goto done;
	}

	/* Initiator mode */
	rc = pn533_start_poll_complete(dev, resp);
	if (!rc)
		goto done;

	if (!dev->poll_mod_count) {
		nfc_dev_dbg(&dev->interface->dev, "Polling has been stopped.");
		goto done;
	}

	pn533_poll_next_mod(dev);
	/* Not target found, turn radio off */
	queue_work(dev->wq, &dev->rf_work);

done:
	dev_kfree_skb(resp);
	return rc;

stop_poll:
	nfc_dev_err(&dev->interface->dev, "Polling operation has been stopped");

	pn533_poll_reset_mod_list(dev);
	dev->poll_protocols = 0;
	return rc;
}

static struct sk_buff *pn533_alloc_poll_in_frame(struct pn533 *dev,
					struct pn533_poll_modulations *mod)
{
	struct sk_buff *skb;

	skb = pn533_alloc_skb(dev, mod->len);
	if (!skb)
		return NULL;

	memcpy(skb_put(skb, mod->len), &mod->data, mod->len);

	return skb;
}

static int pn533_send_poll_frame(struct pn533 *dev)
{
	struct pn533_poll_modulations *mod;
	struct sk_buff *skb;
	int rc;
	u8 cmd_code;

	mod = dev->poll_mod_active[dev->poll_mod_curr];

	nfc_dev_dbg(&dev->interface->dev, "%s mod len %d\n",
		    __func__, mod->len);

	if (mod->len == 0) {  /* Listen mode */
		cmd_code = PN533_CMD_TG_INIT_AS_TARGET;
		skb = pn533_alloc_poll_tg_frame(dev);
	} else {  /* Polling mode */
		cmd_code =  PN533_CMD_IN_LIST_PASSIVE_TARGET;
		skb = pn533_alloc_poll_in_frame(dev, mod);
	}

	if (!skb) {
		nfc_dev_err(&dev->interface->dev, "Failed to allocate skb.");
		return -ENOMEM;
	}

	rc = pn533_send_cmd_async(dev, cmd_code, skb, pn533_poll_complete,
				  NULL);
	if (rc < 0) {
		dev_kfree_skb(skb);
		nfc_dev_err(&dev->interface->dev, "Polling loop error %d", rc);
	}

	return rc;
}

static void pn533_wq_poll(struct work_struct *work)
{
	struct pn533 *dev = container_of(work, struct pn533, poll_work.work);
	struct pn533_poll_modulations *cur_mod;
	int rc;

	cur_mod = dev->poll_mod_active[dev->poll_mod_curr];

	nfc_dev_dbg(&dev->interface->dev,
		    "%s cancel_listen %d modulation len %d",
		    __func__, dev->cancel_listen, cur_mod->len);

	if (dev->cancel_listen == 1) {
		dev->cancel_listen = 0;
		pn533_abort_cmd(dev, GFP_ATOMIC);
	}

	rc = pn533_send_poll_frame(dev);
	if (rc)
		return;

	if (cur_mod->len == 0 && dev->poll_mod_count > 1)
		mod_timer(&dev->listen_timer, jiffies + PN533_LISTEN_TIME * HZ);

	return;
}

static int pn533_start_poll(struct nfc_dev *nfc_dev,
			    u32 im_protocols, u32 tm_protocols)
{
	struct pn533 *dev = nfc_get_drvdata(nfc_dev);

	nfc_dev_dbg(&dev->interface->dev,
		    "%s: im protocols 0x%x tm protocols 0x%x",
		    __func__, im_protocols, tm_protocols);

	if (dev->tgt_active_prot) {
		nfc_dev_err(&dev->interface->dev,
			    "Cannot poll with a target already activated");
		return -EBUSY;
	}

	if (dev->tgt_mode) {
		nfc_dev_err(&dev->interface->dev,
			    "Cannot poll while already being activated");
		return -EBUSY;
	}

	if (tm_protocols) {
		dev->gb = nfc_get_local_general_bytes(nfc_dev, &dev->gb_len);
		if (dev->gb == NULL)
			tm_protocols = 0;
	}

	dev->poll_mod_curr = 0;
	pn533_poll_create_mod_list(dev, im_protocols, tm_protocols);
	dev->poll_protocols = im_protocols;
	dev->listen_protocols = tm_protocols;

	return pn533_send_poll_frame(dev);
}

static void pn533_stop_poll(struct nfc_dev *nfc_dev)
{
	struct pn533 *dev = nfc_get_drvdata(nfc_dev);

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	del_timer(&dev->listen_timer);

	if (!dev->poll_mod_count) {
		nfc_dev_dbg(&dev->interface->dev,
			    "Polling operation was not running");
		return;
	}

	pn533_abort_cmd(dev, GFP_KERNEL);
	flush_delayed_work(&dev->poll_work);
	pn533_poll_reset_mod_list(dev);
}

static int pn533_activate_target_nfcdep(struct pn533 *dev)
{
	struct pn533_cmd_activate_response *rsp;
	u16 gt_len;
	int rc;

	struct sk_buff *skb;
	struct sk_buff *resp;

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	skb = pn533_alloc_skb(dev, sizeof(u8) * 2); /*TG + Next*/
	if (!skb)
		return -ENOMEM;

	*skb_put(skb, sizeof(u8)) = 1; /* TG */
	*skb_put(skb, sizeof(u8)) = 0; /* Next */

	resp = pn533_send_cmd_sync(dev, PN533_CMD_IN_ATR, skb);
	if (IS_ERR(resp))
		return PTR_ERR(resp);

	rsp = (struct pn533_cmd_activate_response *)resp->data;
	rc = rsp->status & PN533_CMD_RET_MASK;
	if (rc != PN533_CMD_RET_SUCCESS) {
		nfc_dev_err(&dev->interface->dev,
			    "Target activation failed (error 0x%x)", rc);
		dev_kfree_skb(resp);
		return -EIO;
	}

	/* ATR_RES general bytes are located at offset 16 */
	gt_len = resp->len - 16;
	rc = nfc_set_remote_general_bytes(dev->nfc_dev, rsp->gt, gt_len);

	dev_kfree_skb(resp);
	return rc;
}

static int pn533_activate_target(struct nfc_dev *nfc_dev,
				 struct nfc_target *target, u32 protocol)
{
	struct pn533 *dev = nfc_get_drvdata(nfc_dev);
	int rc;

	nfc_dev_dbg(&dev->interface->dev, "%s - protocol=%u", __func__,
		    protocol);

	if (dev->poll_mod_count) {
		nfc_dev_err(&dev->interface->dev,
			    "Cannot activate while polling");
		return -EBUSY;
	}

	if (dev->tgt_active_prot) {
		nfc_dev_err(&dev->interface->dev,
			    "There is already an active target");
		return -EBUSY;
	}

	if (!dev->tgt_available_prots) {
		nfc_dev_err(&dev->interface->dev,
			    "There is no available target to activate");
		return -EINVAL;
	}

	if (!(dev->tgt_available_prots & (1 << protocol))) {
		nfc_dev_err(&dev->interface->dev,
			    "Target doesn't support requested proto %u",
			    protocol);
		return -EINVAL;
	}

	if (protocol == NFC_PROTO_NFC_DEP) {
		rc = pn533_activate_target_nfcdep(dev);
		if (rc) {
			nfc_dev_err(&dev->interface->dev,
				    "Activating target with DEP failed %d", rc);
			return rc;
		}
	}

	dev->tgt_active_prot = protocol;
	dev->tgt_available_prots = 0;

	return 0;
}

static void pn533_deactivate_target(struct nfc_dev *nfc_dev,
				    struct nfc_target *target)
{
	struct pn533 *dev = nfc_get_drvdata(nfc_dev);

	struct sk_buff *skb;
	struct sk_buff *resp;

	int rc;

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	if (!dev->tgt_active_prot) {
		nfc_dev_err(&dev->interface->dev, "There is no active target");
		return;
	}

	dev->tgt_active_prot = 0;
	skb_queue_purge(&dev->resp_q);

	skb = pn533_alloc_skb(dev, sizeof(u8));
	if (!skb)
		return;

	*skb_put(skb, 1) = 1; /* TG*/

	resp = pn533_send_cmd_sync(dev, PN533_CMD_IN_RELEASE, skb);
	if (IS_ERR(resp))
		return;

	rc = resp->data[0] & PN533_CMD_RET_MASK;
	if (rc != PN533_CMD_RET_SUCCESS)
		nfc_dev_err(&dev->interface->dev,
			    "Error 0x%x when releasing the target", rc);

	dev_kfree_skb(resp);
	return;
}


static int pn533_in_dep_link_up_complete(struct pn533 *dev, void *arg,
					 struct sk_buff *resp)
{
	struct pn533_cmd_jump_dep_response *rsp;
	u8 target_gt_len;
	int rc;
	u8 active = *(u8 *)arg;

	kfree(arg);

	if (IS_ERR(resp))
		return PTR_ERR(resp);

	if (dev->tgt_available_prots &&
	    !(dev->tgt_available_prots & (1 << NFC_PROTO_NFC_DEP))) {
		nfc_dev_err(&dev->interface->dev,
			    "The target does not support DEP");
		rc =  -EINVAL;
		goto error;
	}

	rsp = (struct pn533_cmd_jump_dep_response *)resp->data;

	rc = rsp->status & PN533_CMD_RET_MASK;
	if (rc != PN533_CMD_RET_SUCCESS) {
		nfc_dev_err(&dev->interface->dev,
			    "Bringing DEP link up failed (error 0x%x)", rc);
		goto error;
	}

	if (!dev->tgt_available_prots) {
		struct nfc_target nfc_target;

		nfc_dev_dbg(&dev->interface->dev, "Creating new target");

		nfc_target.supported_protocols = NFC_PROTO_NFC_DEP_MASK;
		nfc_target.nfcid1_len = 10;
		memcpy(nfc_target.nfcid1, rsp->nfcid3t, nfc_target.nfcid1_len);
		rc = nfc_targets_found(dev->nfc_dev, &nfc_target, 1);
		if (rc)
			goto error;

		dev->tgt_available_prots = 0;
	}

	dev->tgt_active_prot = NFC_PROTO_NFC_DEP;

	/* ATR_RES general bytes are located at offset 17 */
	target_gt_len = resp->len - 17;
	rc = nfc_set_remote_general_bytes(dev->nfc_dev,
					  rsp->gt, target_gt_len);
	if (rc == 0)
		rc = nfc_dep_link_is_up(dev->nfc_dev,
					dev->nfc_dev->targets[0].idx,
					!active, NFC_RF_INITIATOR);

error:
	dev_kfree_skb(resp);
	return rc;
}

static int pn533_rf_field(struct nfc_dev *nfc_dev, u8 rf);
#define PASSIVE_DATA_LEN 5
static int pn533_dep_link_up(struct nfc_dev *nfc_dev, struct nfc_target *target,
			     u8 comm_mode, u8 *gb, size_t gb_len)
{
	struct pn533 *dev = nfc_get_drvdata(nfc_dev);
	struct sk_buff *skb;
	int rc, skb_len;
	u8 *next, *arg, nfcid3[NFC_NFCID3_MAXSIZE];

	u8 passive_data[PASSIVE_DATA_LEN] = {0x00, 0xff, 0xff, 0x00, 0x3};

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	if (dev->poll_mod_count) {
		nfc_dev_err(&dev->interface->dev,
			    "Cannot bring the DEP link up while polling");
		return -EBUSY;
	}

	if (dev->tgt_active_prot) {
		nfc_dev_err(&dev->interface->dev,
			    "There is already an active target");
		return -EBUSY;
	}

	skb_len = 3 + gb_len; /* ActPass + BR + Next */
	skb_len += PASSIVE_DATA_LEN;

	/* NFCID3 */
	skb_len += NFC_NFCID3_MAXSIZE;
	if (target && !target->nfcid2_len) {
		nfcid3[0] = 0x1;
		nfcid3[1] = 0xfe;
		get_random_bytes(nfcid3 + 2, 6);
	}

	skb = pn533_alloc_skb(dev, skb_len);
	if (!skb)
		return -ENOMEM;

	*skb_put(skb, 1) = !comm_mode;  /* ActPass */
	*skb_put(skb, 1) = 0x02;  /* 424 kbps */

	next = skb_put(skb, 1);  /* Next */
	*next = 0;

	/* Copy passive data */
	memcpy(skb_put(skb, PASSIVE_DATA_LEN), passive_data, PASSIVE_DATA_LEN);
	*next |= 1;

	/* Copy NFCID3 (which is NFCID2 from SENSF_RES) */
	if (target && target->nfcid2_len)
		memcpy(skb_put(skb, NFC_NFCID3_MAXSIZE), target->nfcid2,
		       target->nfcid2_len);
	else
		memcpy(skb_put(skb, NFC_NFCID3_MAXSIZE), nfcid3,
		       NFC_NFCID3_MAXSIZE);
	*next |= 2;

	if (gb != NULL && gb_len > 0) {
		memcpy(skb_put(skb, gb_len), gb, gb_len);
		*next |= 4; /* We have some Gi */
	} else {
		*next = 0;
	}

	arg = kmalloc(sizeof(*arg), GFP_KERNEL);
	if (!arg) {
		dev_kfree_skb(skb);
		return -ENOMEM;
	}

	*arg = !comm_mode;

	pn533_rf_field(dev->nfc_dev, 0);

	rc = pn533_send_cmd_async(dev, PN533_CMD_IN_JUMP_FOR_DEP, skb,
				  pn533_in_dep_link_up_complete, arg);

	if (rc < 0) {
		dev_kfree_skb(skb);
		kfree(arg);
	}

	return rc;
}

static int pn533_dep_link_down(struct nfc_dev *nfc_dev)
{
	struct pn533 *dev = nfc_get_drvdata(nfc_dev);

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	pn533_poll_reset_mod_list(dev);

	if (dev->tgt_mode || dev->tgt_active_prot)
		pn533_abort_cmd(dev, GFP_KERNEL);

	dev->tgt_active_prot = 0;
	dev->tgt_mode = 0;

	skb_queue_purge(&dev->resp_q);

	return 0;
}

struct pn533_data_exchange_arg {
	data_exchange_cb_t cb;
	void *cb_context;
};

static struct sk_buff *pn533_build_response(struct pn533 *dev)
{
	struct sk_buff *skb, *tmp, *t;
	unsigned int skb_len = 0, tmp_len = 0;

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	if (skb_queue_empty(&dev->resp_q))
		return NULL;

	if (skb_queue_len(&dev->resp_q) == 1) {
		skb = skb_dequeue(&dev->resp_q);
		goto out;
	}

	skb_queue_walk_safe(&dev->resp_q, tmp, t)
		skb_len += tmp->len;

	nfc_dev_dbg(&dev->interface->dev, "%s total length %d\n",
		    __func__, skb_len);

	skb = alloc_skb(skb_len, GFP_KERNEL);
	if (skb == NULL)
		goto out;

	skb_put(skb, skb_len);

	skb_queue_walk_safe(&dev->resp_q, tmp, t) {
		memcpy(skb->data + tmp_len, tmp->data, tmp->len);
		tmp_len += tmp->len;
	}

out:
	skb_queue_purge(&dev->resp_q);

	return skb;
}

static int pn533_data_exchange_complete(struct pn533 *dev, void *_arg,
					struct sk_buff *resp)
{
	struct pn533_data_exchange_arg *arg = _arg;
	struct sk_buff *skb;
	int rc = 0;
	u8 status, ret, mi;

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		goto _error;
	}

	status = resp->data[0];
	ret = status & PN533_CMD_RET_MASK;
	mi = status & PN533_CMD_MI_MASK;

	skb_pull(resp, sizeof(status));

	if (ret != PN533_CMD_RET_SUCCESS) {
		nfc_dev_err(&dev->interface->dev,
			    "Exchanging data failed (error 0x%x)", ret);
		rc = -EIO;
		goto error;
	}

	skb_queue_tail(&dev->resp_q, resp);

	if (mi) {
		dev->cmd_complete_mi_arg = arg;
		queue_work(dev->wq, &dev->mi_rx_work);
		return -EINPROGRESS;
	}

	/* Prepare for the next round */
	if (skb_queue_len(&dev->fragment_skb) > 0) {
		dev->cmd_complete_dep_arg = arg;
		queue_work(dev->wq, &dev->mi_tx_work);

		return -EINPROGRESS;
	}

	skb = pn533_build_response(dev);
	if (!skb)
		goto error;

	arg->cb(arg->cb_context, skb, 0);
	kfree(arg);
	return 0;

error:
	dev_kfree_skb(resp);
_error:
	skb_queue_purge(&dev->resp_q);
	arg->cb(arg->cb_context, NULL, rc);
	kfree(arg);
	return rc;
}

/* Split the Tx skb into small chunks */
static int pn533_fill_fragment_skbs(struct pn533 *dev, struct sk_buff *skb)
{
	struct sk_buff *frag;
	int  frag_size;

	do {
		/* Remaining size */
		if (skb->len > PN533_CMD_DATAFRAME_MAXLEN)
			frag_size = PN533_CMD_DATAFRAME_MAXLEN;
		else
			frag_size = skb->len;

		/* Allocate and reserve */
		frag = pn533_alloc_skb(dev, frag_size);
		if (!frag) {
			skb_queue_purge(&dev->fragment_skb);
			break;
		}

		/* Reserve the TG/MI byte */
		skb_reserve(frag, 1);

		/* MI + TG */
		if (frag_size  == PN533_CMD_DATAFRAME_MAXLEN)
			*skb_push(frag, sizeof(u8)) = (PN533_CMD_MI_MASK | 1);
		else
			*skb_push(frag, sizeof(u8)) =  1; /* TG */

		memcpy(skb_put(frag, frag_size), skb->data, frag_size);

		/* Reduce the size of incoming buffer */
		skb_pull(skb, frag_size);

		/* Add this to skb_queue */
		skb_queue_tail(&dev->fragment_skb, frag);

	} while (skb->len > 0);

	dev_kfree_skb(skb);

	return skb_queue_len(&dev->fragment_skb);
}

static int pn533_transceive(struct nfc_dev *nfc_dev,
			    struct nfc_target *target, struct sk_buff *skb,
			    data_exchange_cb_t cb, void *cb_context)
{
	struct pn533 *dev = nfc_get_drvdata(nfc_dev);
	struct pn533_data_exchange_arg *arg = NULL;
	int rc;

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	if (!dev->tgt_active_prot) {
		nfc_dev_err(&dev->interface->dev,
			    "Can't exchange data if there is no active target");
		rc = -EINVAL;
		goto error;
	}

	arg = kmalloc(sizeof(*arg), GFP_KERNEL);
	if (!arg) {
		rc = -ENOMEM;
		goto error;
	}

	arg->cb = cb;
	arg->cb_context = cb_context;

	switch (dev->device_type) {
	case PN533_DEVICE_PASORI:
		if (dev->tgt_active_prot == NFC_PROTO_FELICA) {
			rc = pn533_send_data_async(dev, PN533_CMD_IN_COMM_THRU,
						   skb,
						   pn533_data_exchange_complete,
						   arg);

			break;
		}
	default:
		/* jumbo frame ? */
		if (skb->len > PN533_CMD_DATAEXCH_DATA_MAXLEN) {
			rc = pn533_fill_fragment_skbs(dev, skb);
			if (rc <= 0)
				goto error;

			skb = skb_dequeue(&dev->fragment_skb);
			if (!skb) {
				rc = -EIO;
				goto error;
			}
		} else {
			*skb_push(skb, sizeof(u8)) =  1; /* TG */
		}

		rc = pn533_send_data_async(dev, PN533_CMD_IN_DATA_EXCHANGE,
					   skb, pn533_data_exchange_complete,
					   arg);

		break;
	}

	if (rc < 0) /* rc from send_async */
		goto error;

	return 0;

error:
	kfree(arg);
	dev_kfree_skb(skb);
	return rc;
}

static int pn533_tm_send_complete(struct pn533 *dev, void *arg,
				  struct sk_buff *resp)
{
	u8 status;

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	if (IS_ERR(resp))
		return PTR_ERR(resp);

	status = resp->data[0];

	dev_kfree_skb(resp);

	if (status != 0) {
		nfc_tm_deactivated(dev->nfc_dev);

		dev->tgt_mode = 0;

		return 0;
	}

	queue_work(dev->wq, &dev->tg_work);

	return 0;
}

static int pn533_tm_send(struct nfc_dev *nfc_dev, struct sk_buff *skb)
{
	struct pn533 *dev = nfc_get_drvdata(nfc_dev);
	int rc;

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	if (skb->len > PN533_CMD_DATAEXCH_DATA_MAXLEN) {
		nfc_dev_err(&dev->interface->dev,
			    "Data length greater than the max allowed: %d",
			    PN533_CMD_DATAEXCH_DATA_MAXLEN);
		return -ENOSYS;
	}

	rc = pn533_send_data_async(dev, PN533_CMD_TG_SET_DATA, skb,
				   pn533_tm_send_complete, NULL);
	if (rc < 0)
		dev_kfree_skb(skb);

	return rc;
}

static void pn533_wq_mi_recv(struct work_struct *work)
{
	struct pn533 *dev = container_of(work, struct pn533, mi_rx_work);

	struct sk_buff *skb;
	int rc;

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	skb = pn533_alloc_skb(dev, PN533_CMD_DATAEXCH_HEAD_LEN);
	if (!skb)
		goto error;

	switch (dev->device_type) {
	case PN533_DEVICE_PASORI:
		if (dev->tgt_active_prot == NFC_PROTO_FELICA) {
			rc = pn533_send_cmd_direct_async(dev,
						PN533_CMD_IN_COMM_THRU,
						skb,
						pn533_data_exchange_complete,
						 dev->cmd_complete_mi_arg);

			break;
		}
	default:
		*skb_put(skb, sizeof(u8)) =  1; /*TG*/

		rc = pn533_send_cmd_direct_async(dev,
						 PN533_CMD_IN_DATA_EXCHANGE,
						 skb,
						 pn533_data_exchange_complete,
						 dev->cmd_complete_mi_arg);

		break;
	}

	if (rc == 0) /* success */
		return;

	nfc_dev_err(&dev->interface->dev,
		    "Error %d when trying to perform data_exchange", rc);

	dev_kfree_skb(skb);
	kfree(dev->cmd_complete_mi_arg);

error:
	pn533_send_ack(dev, GFP_KERNEL);
	queue_work(dev->wq, &dev->cmd_work);
}

static void pn533_wq_mi_send(struct work_struct *work)
{
	struct pn533 *dev = container_of(work, struct pn533, mi_tx_work);
	struct sk_buff *skb;
	int rc;

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	/* Grab the first skb in the queue */
	skb = skb_dequeue(&dev->fragment_skb);

	if (skb == NULL) {	/* No more data */
		/* Reset the queue for future use */
		skb_queue_head_init(&dev->fragment_skb);
		goto error;
	}

	switch (dev->device_type) {
	case PN533_DEVICE_PASORI:
		if (dev->tgt_active_prot != NFC_PROTO_FELICA) {
			rc = -EIO;
			break;
		}

		rc = pn533_send_cmd_direct_async(dev, PN533_CMD_IN_COMM_THRU,
						 skb,
						 pn533_data_exchange_complete,
						 dev->cmd_complete_dep_arg);

		break;

	default:
		/* Still some fragments? */
		rc = pn533_send_cmd_direct_async(dev,PN533_CMD_IN_DATA_EXCHANGE,
						 skb,
						 pn533_data_exchange_complete,
						 dev->cmd_complete_dep_arg);

		break;
	}

	if (rc == 0) /* success */
		return;

	nfc_dev_err(&dev->interface->dev,
		    "Error %d when trying to perform data_exchange", rc);

	dev_kfree_skb(skb);
	kfree(dev->cmd_complete_dep_arg);

error:
	pn533_send_ack(dev, GFP_KERNEL);
	queue_work(dev->wq, &dev->cmd_work);
}

static int pn533_set_configuration(struct pn533 *dev, u8 cfgitem, u8 *cfgdata,
								u8 cfgdata_len)
{
	struct sk_buff *skb;
	struct sk_buff *resp;

	int skb_len;

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	skb_len = sizeof(cfgitem) + cfgdata_len; /* cfgitem + cfgdata */

	skb = pn533_alloc_skb(dev, skb_len);
	if (!skb)
		return -ENOMEM;

	*skb_put(skb, sizeof(cfgitem)) = cfgitem;
	memcpy(skb_put(skb, cfgdata_len), cfgdata, cfgdata_len);

	resp = pn533_send_cmd_sync(dev, PN533_CMD_RF_CONFIGURATION, skb);
	if (IS_ERR(resp))
		return PTR_ERR(resp);

	dev_kfree_skb(resp);
	return 0;
}

static int pn533_get_firmware_version(struct pn533 *dev,
				      struct pn533_fw_version *fv)
{
	struct sk_buff *skb;
	struct sk_buff *resp;

	skb = pn533_alloc_skb(dev, 0);
	if (!skb)
		return -ENOMEM;

	resp = pn533_send_cmd_sync(dev, PN533_CMD_GET_FIRMWARE_VERSION, skb);
	if (IS_ERR(resp))
		return PTR_ERR(resp);

	fv->ic = resp->data[0];
	fv->ver = resp->data[1];
	fv->rev = resp->data[2];
	fv->support = resp->data[3];

	dev_kfree_skb(resp);
	return 0;
}

static int pn533_pasori_fw_reset(struct pn533 *dev)
{
	struct sk_buff *skb;
	struct sk_buff *resp;

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	skb = pn533_alloc_skb(dev, sizeof(u8));
	if (!skb)
		return -ENOMEM;

	*skb_put(skb, sizeof(u8)) = 0x1;

	resp = pn533_send_cmd_sync(dev, 0x18, skb);
	if (IS_ERR(resp))
		return PTR_ERR(resp);

	dev_kfree_skb(resp);

	return 0;
}

struct pn533_acr122_poweron_rdr_arg {
	int rc;
	struct completion done;
};

static void pn533_acr122_poweron_rdr_resp(struct urb *urb)
{
	struct pn533_acr122_poweron_rdr_arg *arg = urb->context;

	nfc_dev_dbg(&urb->dev->dev, "%s", __func__);

	print_hex_dump_debug("ACR122 RX: ", DUMP_PREFIX_NONE, 16, 1,
		       urb->transfer_buffer, urb->transfer_buffer_length,
		       false);

	arg->rc = urb->status;
	complete(&arg->done);
}

static int pn533_acr122_poweron_rdr(struct pn533 *dev)
{
	/* Power on th reader (CCID cmd) */
	u8 cmd[10] = {PN533_ACR122_PC_TO_RDR_ICCPOWERON,
		      0, 0, 0, 0, 0, 0, 3, 0, 0};
	u8 buf[255];
	int rc;
	void *cntx;
	struct pn533_acr122_poweron_rdr_arg arg;

	nfc_dev_dbg(&dev->interface->dev, "%s", __func__);

	init_completion(&arg.done);
	cntx = dev->in_urb->context;  /* backup context */

	dev->in_urb->transfer_buffer = buf;
	dev->in_urb->transfer_buffer_length = 255;
	dev->in_urb->complete = pn533_acr122_poweron_rdr_resp;
	dev->in_urb->context = &arg;

	dev->out_urb->transfer_buffer = cmd;
	dev->out_urb->transfer_buffer_length = sizeof(cmd);

	print_hex_dump_debug("ACR122 TX: ", DUMP_PREFIX_NONE, 16, 1,
		       cmd, sizeof(cmd), false);

	rc = usb_submit_urb(dev->out_urb, GFP_KERNEL);
	if (rc) {
		nfc_dev_err(&dev->interface->dev,
			    "Reader power on cmd error %d", rc);
		return rc;
	}

	rc =  usb_submit_urb(dev->in_urb, GFP_KERNEL);
	if (rc) {
		nfc_dev_err(&dev->interface->dev,
			    "Can't submit for reader power on cmd response %d",
			    rc);
		return rc;
	}

	wait_for_completion(&arg.done);
	dev->in_urb->context = cntx; /* restore context */

	return arg.rc;
}

static int pn533_rf_field(struct nfc_dev *nfc_dev, u8 rf)
{
	struct pn533 *dev = nfc_get_drvdata(nfc_dev);
	u8 rf_field = !!rf;
	int rc;

	rf_field |= PN533_CFGITEM_RF_FIELD_AUTO_RFCA;

	rc = pn533_set_configuration(dev, PN533_CFGITEM_RF_FIELD,
				     (u8 *)&rf_field, 1);
	if (rc) {
		nfc_dev_err(&dev->interface->dev,
			    "Error on setting RF field");
		return rc;
	}

	return rc;
}

int pn533_dev_up(struct nfc_dev *nfc_dev)
{
	return pn533_rf_field(nfc_dev, 1);
}

int pn533_dev_down(struct nfc_dev *nfc_dev)
{
	return pn533_rf_field(nfc_dev, 0);
}

static struct nfc_ops pn533_nfc_ops = {
	.dev_up = pn533_dev_up,
	.dev_down = pn533_dev_down,
	.dep_link_up = pn533_dep_link_up,
	.dep_link_down = pn533_dep_link_down,
	.start_poll = pn533_start_poll,
	.stop_poll = pn533_stop_poll,
	.activate_target = pn533_activate_target,
	.deactivate_target = pn533_deactivate_target,
	.im_transceive = pn533_transceive,
	.tm_send = pn533_tm_send,
};

static int pn533_setup(struct pn533 *dev)
{
	struct pn533_config_max_retries max_retries;
	struct pn533_config_timing timing;
	u8 pasori_cfg[3] = {0x08, 0x01, 0x08};
	int rc;

	switch (dev->device_type) {
	case PN533_DEVICE_STD:
	case PN533_DEVICE_PASORI:
	case PN533_DEVICE_ACR122U:
		max_retries.mx_rty_atr = 0x2;
		max_retries.mx_rty_psl = 0x1;
		max_retries.mx_rty_passive_act =
			PN533_CONFIG_MAX_RETRIES_NO_RETRY;

		timing.rfu = PN533_CONFIG_TIMING_102;
		timing.atr_res_timeout = PN533_CONFIG_TIMING_102;
		timing.dep_timeout = PN533_CONFIG_TIMING_204;

		break;

	default:
		nfc_dev_err(&dev->interface->dev, "Unknown device type %d\n",
			    dev->device_type);
		return -EINVAL;
	}

	rc = pn533_set_configuration(dev, PN533_CFGITEM_MAX_RETRIES,
				     (u8 *)&max_retries, sizeof(max_retries));
	if (rc) {
		nfc_dev_err(&dev->interface->dev,
			    "Error on setting MAX_RETRIES config");
		return rc;
	}


	rc = pn533_set_configuration(dev, PN533_CFGITEM_TIMING,
				     (u8 *)&timing, sizeof(timing));
	if (rc) {
		nfc_dev_err(&dev->interface->dev,
			    "Error on setting RF timings");
		return rc;
	}

	switch (dev->device_type) {
	case PN533_DEVICE_STD:
		break;

	case PN533_DEVICE_PASORI:
		pn533_pasori_fw_reset(dev);

		rc = pn533_set_configuration(dev, PN533_CFGITEM_PASORI,
					     pasori_cfg, 3);
		if (rc) {
			nfc_dev_err(&dev->interface->dev,
				    "Error while settings PASORI config");
			return rc;
		}

		pn533_pasori_fw_reset(dev);

		break;
	}

	return 0;
}

static int pn533_probe(struct usb_interface *interface,
			const struct usb_device_id *id)
{
	struct pn533_fw_version fw_ver;
	struct pn533 *dev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int in_endpoint = 0;
	int out_endpoint = 0;
	int rc = -ENOMEM;
	int i;
	u32 protocols;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;
	mutex_init(&dev->cmd_lock);

	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!in_endpoint && usb_endpoint_is_bulk_in(endpoint))
			in_endpoint = endpoint->bEndpointAddress;

		if (!out_endpoint && usb_endpoint_is_bulk_out(endpoint))
			out_endpoint = endpoint->bEndpointAddress;
	}

	if (!in_endpoint || !out_endpoint) {
		nfc_dev_err(&interface->dev,
			    "Could not find bulk-in or bulk-out endpoint");
		rc = -ENODEV;
		goto error;
	}

	dev->in_urb = usb_alloc_urb(0, GFP_KERNEL);
	dev->out_urb = usb_alloc_urb(0, GFP_KERNEL);

	if (!dev->in_urb || !dev->out_urb)
		goto error;

	usb_fill_bulk_urb(dev->in_urb, dev->udev,
			  usb_rcvbulkpipe(dev->udev, in_endpoint),
			  NULL, 0, NULL, dev);
	usb_fill_bulk_urb(dev->out_urb, dev->udev,
			  usb_sndbulkpipe(dev->udev, out_endpoint),
			  NULL, 0, pn533_send_complete, dev);

	INIT_WORK(&dev->cmd_work, pn533_wq_cmd);
	INIT_WORK(&dev->cmd_complete_work, pn533_wq_cmd_complete);
	INIT_WORK(&dev->mi_rx_work, pn533_wq_mi_recv);
	INIT_WORK(&dev->mi_tx_work, pn533_wq_mi_send);
	INIT_WORK(&dev->tg_work, pn533_wq_tg_get_data);
	INIT_DELAYED_WORK(&dev->poll_work, pn533_wq_poll);
	INIT_WORK(&dev->rf_work, pn533_wq_rf);
	dev->wq = alloc_ordered_workqueue("pn533", 0);
	if (dev->wq == NULL)
		goto error;

	init_timer(&dev->listen_timer);
	dev->listen_timer.data = (unsigned long) dev;
	dev->listen_timer.function = pn533_listen_mode_timer;

	skb_queue_head_init(&dev->resp_q);
	skb_queue_head_init(&dev->fragment_skb);

	INIT_LIST_HEAD(&dev->cmd_queue);

	usb_set_intfdata(interface, dev);

	dev->ops = &pn533_std_frame_ops;

	dev->protocol_type = PN533_PROTO_REQ_ACK_RESP;
	dev->device_type = id->driver_info;
	switch (dev->device_type) {
	case PN533_DEVICE_STD:
		protocols = PN533_ALL_PROTOCOLS;
		break;

	case PN533_DEVICE_PASORI:
		protocols = PN533_NO_TYPE_B_PROTOCOLS;
		break;

	case PN533_DEVICE_ACR122U:
		protocols = PN533_NO_TYPE_B_PROTOCOLS;
		dev->ops = &pn533_acr122_frame_ops;
		dev->protocol_type = PN533_PROTO_REQ_RESP,

		rc = pn533_acr122_poweron_rdr(dev);
		if (rc < 0) {
			nfc_dev_err(&dev->interface->dev,
				    "Couldn't poweron the reader (error %d)",
				    rc);
			goto destroy_wq;
		}
		break;

	default:
		nfc_dev_err(&dev->interface->dev, "Unknown device type %d\n",
			    dev->device_type);
		rc = -EINVAL;
		goto destroy_wq;
	}

	memset(&fw_ver, 0, sizeof(fw_ver));
	rc = pn533_get_firmware_version(dev, &fw_ver);
	if (rc < 0)
		goto destroy_wq;

	nfc_dev_info(&dev->interface->dev,
		     "NXP PN5%02X firmware ver %d.%d now attached",
		     fw_ver.ic, fw_ver.ver, fw_ver.rev);


	dev->nfc_dev = nfc_allocate_device(&pn533_nfc_ops, protocols,
					   dev->ops->tx_header_len +
					   PN533_CMD_DATAEXCH_HEAD_LEN,
					   dev->ops->tx_tail_len);
	if (!dev->nfc_dev) {
		rc = -ENOMEM;
		goto destroy_wq;
	}

	nfc_set_parent_dev(dev->nfc_dev, &interface->dev);
	nfc_set_drvdata(dev->nfc_dev, dev);

	rc = nfc_register_device(dev->nfc_dev);
	if (rc)
		goto free_nfc_dev;

	rc = pn533_setup(dev);
	if (rc)
		goto unregister_nfc_dev;

	return 0;

unregister_nfc_dev:
	nfc_unregister_device(dev->nfc_dev);

free_nfc_dev:
	nfc_free_device(dev->nfc_dev);

destroy_wq:
	destroy_workqueue(dev->wq);
error:
	usb_free_urb(dev->in_urb);
	usb_free_urb(dev->out_urb);
	usb_put_dev(dev->udev);
	kfree(dev);
	return rc;
}

static void pn533_disconnect(struct usb_interface *interface)
{
	struct pn533 *dev;
	struct pn533_cmd *cmd, *n;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	nfc_unregister_device(dev->nfc_dev);
	nfc_free_device(dev->nfc_dev);

	usb_kill_urb(dev->in_urb);
	usb_kill_urb(dev->out_urb);

	flush_delayed_work(&dev->poll_work);
	destroy_workqueue(dev->wq);

	skb_queue_purge(&dev->resp_q);

	del_timer(&dev->listen_timer);

	list_for_each_entry_safe(cmd, n, &dev->cmd_queue, queue) {
		list_del(&cmd->queue);
		kfree(cmd);
	}

	usb_free_urb(dev->in_urb);
	usb_free_urb(dev->out_urb);
	kfree(dev);

	nfc_dev_info(&interface->dev, "NXP PN533 NFC device disconnected");
}

static struct usb_driver pn533_driver = {
	.name =		"pn533",
	.probe =	pn533_probe,
	.disconnect =	pn533_disconnect,
	.id_table =	pn533_table,
};

module_usb_driver(pn533_driver);

MODULE_AUTHOR("Lauro Ramos Venancio <lauro.venancio@openbossa.org>");
MODULE_AUTHOR("Aloisio Almeida Jr <aloisio.almeida@openbossa.org>");
MODULE_AUTHOR("Waldemar Rymarkiewicz <waldemar.rymarkiewicz@tieto.com>");
MODULE_DESCRIPTION("PN533 usb driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
