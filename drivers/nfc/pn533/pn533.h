/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for NXP PN533 NFC Chip
 *
 * Copyright (C) 2011 Instituto Nokia de Tecnologia
 * Copyright (C) 2012-2013 Tieto Poland
 */

#define PN533_DEVICE_STD     0x1
#define PN533_DEVICE_PASORI  0x2
#define PN533_DEVICE_ACR122U 0x3
#define PN533_DEVICE_PN532   0x4

#define PN533_ALL_PROTOCOLS (NFC_PROTO_JEWEL_MASK | NFC_PROTO_MIFARE_MASK |\
			     NFC_PROTO_FELICA_MASK | NFC_PROTO_ISO14443_MASK |\
			     NFC_PROTO_NFC_DEP_MASK |\
			     NFC_PROTO_ISO14443_B_MASK)

#define PN533_NO_TYPE_B_PROTOCOLS (NFC_PROTO_JEWEL_MASK | \
				   NFC_PROTO_MIFARE_MASK | \
				   NFC_PROTO_FELICA_MASK | \
				   NFC_PROTO_ISO14443_MASK | \
				   NFC_PROTO_NFC_DEP_MASK)

/* Standard pn533 frame definitions (standard and extended)*/
#define PN533_STD_FRAME_HEADER_LEN (sizeof(struct pn533_std_frame) \
					+ 2) /* data[0] TFI, data[1] CC */
#define PN533_STD_FRAME_TAIL_LEN 2 /* data[len] DCS, data[len + 1] postamble*/

#define PN533_EXT_FRAME_HEADER_LEN (sizeof(struct pn533_ext_frame) \
					+ 2) /* data[0] TFI, data[1] CC */

#define PN533_CMD_DATAEXCH_HEAD_LEN 1
#define PN533_CMD_DATAEXCH_DATA_MAXLEN	262
#define PN533_CMD_DATAFRAME_MAXLEN	240	/* max data length (send) */

/*
 * Max extended frame payload len, excluding TFI and CC
 * which are already in PN533_FRAME_HEADER_LEN.
 */
#define PN533_STD_FRAME_MAX_PAYLOAD_LEN 263


/* Preamble (1), SoPC (2), ACK Code (2), Postamble (1) */
#define PN533_STD_FRAME_ACK_SIZE 6
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

/* PN533 Commands */
#define PN533_FRAME_CMD(f) (f->data[1])

#define PN533_CMD_GET_FIRMWARE_VERSION 0x02
#define PN533_CMD_SAM_CONFIGURATION 0x14
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
#define PN533_CMD_TG_SET_META_DATA 0x94
#define PN533_CMD_UNDEF 0xff

#define PN533_CMD_RESPONSE(cmd) (cmd + 1)

/* PN533 Return codes */
#define PN533_CMD_RET_MASK 0x3F
#define PN533_CMD_MI_MASK 0x40
#define PN533_CMD_RET_SUCCESS 0x00


enum  pn533_protocol_type {
	PN533_PROTO_REQ_ACK_RESP = 0,
	PN533_PROTO_REQ_RESP
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

struct pn533 {
	struct nfc_dev *nfc_dev;
	u32 device_type;
	enum pn533_protocol_type protocol_type;

	struct sk_buff_head resp_q;
	struct sk_buff_head fragment_skb;

	struct workqueue_struct	*wq;
	struct work_struct cmd_work;
	struct work_struct cmd_complete_work;
	struct delayed_work poll_work;
	struct work_struct mi_rx_work;
	struct work_struct mi_tx_work;
	struct work_struct mi_tm_rx_work;
	struct work_struct mi_tm_tx_work;
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
	u8 poll_dep;
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

	struct device *dev;
	void *phy;
	struct pn533_phy_ops *phy_ops;
};

typedef int (*pn533_send_async_complete_t) (struct pn533 *dev, void *arg,
					struct sk_buff *resp);

struct pn533_cmd {
	struct list_head queue;
	u8 code;
	int status;
	struct sk_buff *req;
	struct sk_buff *resp;
	pn533_send_async_complete_t  complete_cb;
	void *complete_cb_context;
};


struct pn533_frame_ops {
	void (*tx_frame_init)(void *frame, u8 cmd_code);
	void (*tx_frame_finish)(void *frame);
	void (*tx_update_payload_len)(void *frame, int len);
	int tx_header_len;
	int tx_tail_len;

	bool (*rx_is_frame_valid)(void *frame, struct pn533 *dev);
	bool (*rx_frame_is_ack)(void *frame);
	int (*rx_frame_size)(void *frame);
	int rx_header_len;
	int rx_tail_len;

	int max_payload_len;
	u8 (*get_cmd_code)(void *frame);
};


struct pn533_phy_ops {
	int (*send_frame)(struct pn533 *priv,
			  struct sk_buff *out);
	int (*send_ack)(struct pn533 *dev, gfp_t flags);
	void (*abort_cmd)(struct pn533 *priv, gfp_t flags);
};


struct pn533 *pn533_register_device(u32 device_type,
				u32 protocols,
				enum pn533_protocol_type protocol_type,
				void *phy,
				struct pn533_phy_ops *phy_ops,
				struct pn533_frame_ops *fops,
				struct device *dev,
				struct device *parent);

int pn533_finalize_setup(struct pn533 *dev);
void pn533_unregister_device(struct pn533 *priv);
void pn533_recv_frame(struct pn533 *dev, struct sk_buff *skb, int status);

bool pn533_rx_frame_is_cmd_response(struct pn533 *dev, void *frame);
bool pn533_rx_frame_is_ack(void *_frame);
