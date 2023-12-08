// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for NXP PN533 NFC Chip - core functions
 *
 * Copyright (C) 2011 Instituto Nokia de Tecnologia
 * Copyright (C) 2012-2013 Tieto Poland
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/nfc.h>
#include <linux/netdevice.h>
#include <net/nfc/nfc.h>
#include "pn533.h"

#define VERSION "0.3"

/* How much time we spend listening for initiators */
#define PN533_LISTEN_TIME 2
/* Delay between each poll frame (ms) */
#define PN533_POLL_INTERVAL 10

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

struct pn532_autopoll_resp {
	u8 type;
	u8 ln;
	u8 tg;
	u8 tgdata[];
};

/* PN532_CMD_IN_AUTOPOLL */
#define PN532_AUTOPOLL_POLLNR_INFINITE	0xff
#define PN532_AUTOPOLL_PERIOD		0x03 /* in units of 150 ms */

#define PN532_AUTOPOLL_TYPE_GENERIC_106		0x00
#define PN532_AUTOPOLL_TYPE_GENERIC_212		0x01
#define PN532_AUTOPOLL_TYPE_GENERIC_424		0x02
#define PN532_AUTOPOLL_TYPE_JEWEL		0x04
#define PN532_AUTOPOLL_TYPE_MIFARE		0x10
#define PN532_AUTOPOLL_TYPE_FELICA212		0x11
#define PN532_AUTOPOLL_TYPE_FELICA424		0x12
#define PN532_AUTOPOLL_TYPE_ISOA		0x20
#define PN532_AUTOPOLL_TYPE_ISOB		0x23
#define PN532_AUTOPOLL_TYPE_DEP_PASSIVE_106	0x40
#define PN532_AUTOPOLL_TYPE_DEP_PASSIVE_212	0x41
#define PN532_AUTOPOLL_TYPE_DEP_PASSIVE_424	0x42
#define PN532_AUTOPOLL_TYPE_DEP_ACTIVE_106	0x80
#define PN532_AUTOPOLL_TYPE_DEP_ACTIVE_212	0x81
#define PN532_AUTOPOLL_TYPE_DEP_ACTIVE_424	0x82

/* PN533_TG_INIT_AS_TARGET */
#define PN533_INIT_TARGET_PASSIVE 0x1
#define PN533_INIT_TARGET_DEP 0x2

#define PN533_INIT_TARGET_RESP_FRAME_MASK 0x3
#define PN533_INIT_TARGET_RESP_ACTIVE     0x1
#define PN533_INIT_TARGET_RESP_DEP        0x4

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

bool pn533_rx_frame_is_ack(void *_frame)
{
	struct pn533_std_frame *frame = _frame;

	if (frame->start_frame != cpu_to_be16(PN533_STD_FRAME_SOF))
		return false;

	if (frame->datalen != 0 || frame->datalen_checksum != 0xFF)
		return false;

	return true;
}
EXPORT_SYMBOL_GPL(pn533_rx_frame_is_ack);

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

bool pn533_rx_frame_is_cmd_response(struct pn533 *dev, void *frame)
{
	return (dev->ops->get_cmd_code(frame) ==
				PN533_CMD_RESPONSE(dev->cmd->code));
}
EXPORT_SYMBOL_GPL(pn533_rx_frame_is_cmd_response);


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
	struct sk_buff *resp;
	int status, rc = 0;

	if (!cmd) {
		dev_dbg(dev->dev, "%s: cmd not set\n", __func__);
		goto done;
	}

	dev_kfree_skb(cmd->req);

	status = cmd->status;
	resp = cmd->resp;

	if (status < 0) {
		rc = cmd->complete_cb(dev, cmd->complete_cb_context,
				      ERR_PTR(status));
		dev_kfree_skb(resp);
		goto done;
	}

	/* when no response is set we got interrupted */
	if (!resp)
		resp = ERR_PTR(-EINTR);

	if (!IS_ERR(resp)) {
		skb_pull(resp, dev->ops->rx_header_len);
		skb_trim(resp, resp->len - dev->ops->rx_tail_len);
	}

	rc = cmd->complete_cb(dev, cmd->complete_cb_context, resp);

done:
	kfree(cmd);
	dev->cmd = NULL;
	return rc;
}

static int __pn533_send_async(struct pn533 *dev, u8 cmd_code,
			      struct sk_buff *req,
			      pn533_send_async_complete_t complete_cb,
			      void *complete_cb_context)
{
	struct pn533_cmd *cmd;
	int rc = 0;

	dev_dbg(dev->dev, "Sending command 0x%x\n", cmd_code);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->code = cmd_code;
	cmd->req = req;
	cmd->complete_cb = complete_cb;
	cmd->complete_cb_context = complete_cb_context;

	pn533_build_cmd_frame(dev, cmd_code, req);

	mutex_lock(&dev->cmd_lock);

	if (!dev->cmd_pending) {
		dev->cmd = cmd;
		rc = dev->phy_ops->send_frame(dev, req);
		if (rc) {
			dev->cmd = NULL;
			goto error;
		}

		dev->cmd_pending = 1;
		goto unlock;
	}

	dev_dbg(dev->dev, "%s Queueing command 0x%x\n",
		__func__, cmd_code);

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
	return __pn533_send_async(dev, cmd_code, req, complete_cb,
				complete_cb_context);
}

static int pn533_send_cmd_async(struct pn533 *dev, u8 cmd_code,
				struct sk_buff *req,
				pn533_send_async_complete_t complete_cb,
				void *complete_cb_context)
{
	return __pn533_send_async(dev, cmd_code, req, complete_cb,
				complete_cb_context);
}

/*
 * pn533_send_cmd_direct_async
 *
 * The function sends a priority cmd directly to the chip omitting the cmd
 * queue. It's intended to be used by chaining mechanism of received responses
 * where the host has to request every single chunk of data before scheduling
 * next cmd from the queue.
 */
static int pn533_send_cmd_direct_async(struct pn533 *dev, u8 cmd_code,
				       struct sk_buff *req,
				       pn533_send_async_complete_t complete_cb,
				       void *complete_cb_context)
{
	struct pn533_cmd *cmd;
	int rc;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->code = cmd_code;
	cmd->req = req;
	cmd->complete_cb = complete_cb;
	cmd->complete_cb_context = complete_cb_context;

	pn533_build_cmd_frame(dev, cmd_code, req);

	dev->cmd = cmd;
	rc = dev->phy_ops->send_frame(dev, req);
	if (rc < 0) {
		dev->cmd = NULL;
		kfree(cmd);
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

	dev->cmd = cmd;
	rc = dev->phy_ops->send_frame(dev, cmd->req);
	if (rc < 0) {
		dev->cmd = NULL;
		dev_kfree_skb(cmd->req);
		kfree(cmd);
		return;
	}

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
 *     as it's been already freed at the beginning of RX path by
 *     async_complete_cb.
 *
 *  3. valid pointer in case of successful RX path
 *
 *  A caller has to check a return value with IS_ERR macro. If the test pass,
 *  the returned pointer is valid.
 *
 */
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

	/*
	 * The length check of nfcid[] and ats[] are not being performed because
	 * the values are not being used
	 */

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

	if (type_a->nfcid_len > NFC_NFCID1_MAXSIZE)
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

static void pn533_poll_reset_mod_list(struct pn533 *dev);
static int pn533_target_found(struct pn533 *dev, u8 tg, u8 *tgdata,
			      int tgdata_len)
{
	struct nfc_target nfc_tgt;
	int rc;

	dev_dbg(dev->dev, "%s: modulation=%d\n",
		__func__, dev->poll_mod_curr);

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
		nfc_err(dev->dev,
			"Unknown current poll modulation\n");
		return -EPROTO;
	}

	if (rc)
		return rc;

	if (!(nfc_tgt.supported_protocols & dev->poll_protocols)) {
		dev_dbg(dev->dev,
			"The Tg found doesn't have the desired protocol\n");
		return -EAGAIN;
	}

	dev_dbg(dev->dev,
		"Target found - supported protocols: 0x%x\n",
		nfc_tgt.supported_protocols);

	dev->tgt_available_prots = nfc_tgt.supported_protocols;

	pn533_poll_reset_mod_list(dev);
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

	/* Toggle the DEP polling */
	if (dev->poll_protocols & NFC_PROTO_NFC_DEP_MASK)
		dev->poll_dep = 1;

	nbtg = resp->data[0];
	tg = resp->data[1];
	tgdata = &resp->data[2];
	tgdata_len = resp->len - 2;  /* nbtg + tg */

	if (nbtg) {
		rc = pn533_target_found(dev, tg, tgdata, tgdata_len);

		/* We must stop the poll after a valid target found */
		if (rc == 0)
			return 0;
	}

	return -EAGAIN;
}

static struct sk_buff *pn533_alloc_poll_tg_frame(struct pn533 *dev)
{
	struct sk_buff *skb;
	u8 *felica, *nfcid3;

	u8 *gbytes = dev->gb;
	size_t gbytes_len = dev->gb_len;

	u8 felica_params[18] = {0x1, 0xfe, /* DEP */
				0x0, 0x0, 0x0, 0x0, 0x0, 0x0, /* random */
				0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
				0xff, 0xff}; /* System code */

	u8 mifare_params[6] = {0x1, 0x1, /* SENS_RES */
			       0x0, 0x0, 0x0,
			       0x40}; /* SEL_RES for DEP */

	unsigned int skb_len = 36 + /*
				     * mode (1), mifare (6),
				     * felica (18), nfcid3 (10), gb_len (1)
				     */
			       gbytes_len +
			       1;  /* len Tk*/

	skb = pn533_alloc_skb(dev, skb_len);
	if (!skb)
		return NULL;

	/* DEP support only */
	skb_put_u8(skb, PN533_INIT_TARGET_DEP);

	/* MIFARE params */
	skb_put_data(skb, mifare_params, 6);

	/* Felica params */
	felica = skb_put_data(skb, felica_params, 18);
	get_random_bytes(felica + 2, 6);

	/* NFCID3 */
	nfcid3 = skb_put_zero(skb, 10);
	memcpy(nfcid3, felica, 8);

	/* General bytes */
	skb_put_u8(skb, gbytes_len);

	skb_put_data(skb, gbytes, gbytes_len);

	/* Len Tk */
	skb_put_u8(skb, 0);

	return skb;
}

static void pn533_wq_tm_mi_recv(struct work_struct *work);
static struct sk_buff *pn533_build_response(struct pn533 *dev);

static int pn533_tm_get_data_complete(struct pn533 *dev, void *arg,
				      struct sk_buff *resp)
{
	struct sk_buff *skb;
	u8 status, ret, mi;
	int rc;

	if (IS_ERR(resp)) {
		skb_queue_purge(&dev->resp_q);
		return PTR_ERR(resp);
	}

	status = resp->data[0];

	ret = status & PN533_CMD_RET_MASK;
	mi = status & PN533_CMD_MI_MASK;

	skb_pull(resp, sizeof(status));

	if (ret != PN533_CMD_RET_SUCCESS) {
		rc = -EIO;
		goto error;
	}

	skb_queue_tail(&dev->resp_q, resp);

	if (mi) {
		queue_work(dev->wq, &dev->mi_tm_rx_work);
		return -EINPROGRESS;
	}

	skb = pn533_build_response(dev);
	if (!skb) {
		rc = -EIO;
		goto error;
	}

	return nfc_tm_data_received(dev->nfc_dev, skb);

error:
	nfc_tm_deactivated(dev->nfc_dev);
	dev->tgt_mode = 0;
	skb_queue_purge(&dev->resp_q);
	dev_kfree_skb(resp);

	return rc;
}

static void pn533_wq_tm_mi_recv(struct work_struct *work)
{
	struct pn533 *dev = container_of(work, struct pn533, mi_tm_rx_work);
	struct sk_buff *skb;
	int rc;

	skb = pn533_alloc_skb(dev, 0);
	if (!skb)
		return;

	rc = pn533_send_cmd_direct_async(dev,
					PN533_CMD_TG_GET_DATA,
					skb,
					pn533_tm_get_data_complete,
					NULL);

	if (rc < 0)
		dev_kfree_skb(skb);
}

static int pn533_tm_send_complete(struct pn533 *dev, void *arg,
				  struct sk_buff *resp);
static void pn533_wq_tm_mi_send(struct work_struct *work)
{
	struct pn533 *dev = container_of(work, struct pn533, mi_tm_tx_work);
	struct sk_buff *skb;
	int rc;

	/* Grab the first skb in the queue */
	skb = skb_dequeue(&dev->fragment_skb);
	if (skb == NULL) {	/* No more data */
		/* Reset the queue for future use */
		skb_queue_head_init(&dev->fragment_skb);
		goto error;
	}

	/* last entry - remove MI bit */
	if (skb_queue_len(&dev->fragment_skb) == 0) {
		rc = pn533_send_cmd_direct_async(dev, PN533_CMD_TG_SET_DATA,
					skb, pn533_tm_send_complete, NULL);
	} else
		rc = pn533_send_cmd_direct_async(dev,
					PN533_CMD_TG_SET_META_DATA,
					skb, pn533_tm_send_complete, NULL);

	if (rc == 0) /* success */
		return;

	dev_err(dev->dev,
		"Error %d when trying to perform set meta data_exchange", rc);

	dev_kfree_skb(skb);

error:
	dev->phy_ops->send_ack(dev, GFP_KERNEL);
	queue_work(dev->wq, &dev->cmd_work);
}

static void pn533_wq_tg_get_data(struct work_struct *work)
{
	struct pn533 *dev = container_of(work, struct pn533, tg_work);
	struct sk_buff *skb;
	int rc;

	skb = pn533_alloc_skb(dev, 0);
	if (!skb)
		return;

	rc = pn533_send_data_async(dev, PN533_CMD_TG_GET_DATA, skb,
				   pn533_tm_get_data_complete, NULL);

	if (rc < 0)
		dev_kfree_skb(skb);
}

#define ATR_REQ_GB_OFFSET 17
static int pn533_init_target_complete(struct pn533 *dev, struct sk_buff *resp)
{
	u8 mode, *cmd, comm_mode = NFC_COMM_PASSIVE, *gb;
	size_t gb_len;
	int rc;

	if (resp->len < ATR_REQ_GB_OFFSET + 1)
		return -EINVAL;

	mode = resp->data[0];
	cmd = &resp->data[1];

	dev_dbg(dev->dev, "Target mode 0x%x len %d\n",
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
		nfc_err(dev->dev,
			"Error when signaling target activation\n");
		return rc;
	}

	dev->tgt_mode = 1;
	queue_work(dev->wq, &dev->tg_work);

	return 0;
}

static void pn533_listen_mode_timer(struct timer_list *t)
{
	struct pn533 *dev = from_timer(dev, t, listen_timer);

	dev->cancel_listen = 1;

	pn533_poll_next_mod(dev);

	queue_delayed_work(dev->wq, &dev->poll_work,
			   msecs_to_jiffies(PN533_POLL_INTERVAL));
}

static int pn533_rf_complete(struct pn533 *dev, void *arg,
			     struct sk_buff *resp)
{
	int rc = 0;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);

		nfc_err(dev->dev, "RF setting error %d\n", rc);

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

	skb = pn533_alloc_skb(dev, 2);
	if (!skb)
		return;

	skb_put_u8(skb, PN533_CFGITEM_RF_FIELD);
	skb_put_u8(skb, PN533_CFGITEM_RF_FIELD_AUTO_RFCA);

	rc = pn533_send_cmd_async(dev, PN533_CMD_RF_CONFIGURATION, skb,
				  pn533_rf_complete, NULL);
	if (rc < 0) {
		dev_kfree_skb(skb);
		nfc_err(dev->dev, "RF setting error %d\n", rc);
	}
}

static int pn533_poll_dep_complete(struct pn533 *dev, void *arg,
				   struct sk_buff *resp)
{
	struct pn533_cmd_jump_dep_response *rsp;
	struct nfc_target nfc_target;
	u8 target_gt_len;
	int rc;

	if (IS_ERR(resp))
		return PTR_ERR(resp);

	memset(&nfc_target, 0, sizeof(struct nfc_target));

	rsp = (struct pn533_cmd_jump_dep_response *)resp->data;

	rc = rsp->status & PN533_CMD_RET_MASK;
	if (rc != PN533_CMD_RET_SUCCESS) {
		/* Not target found, turn radio off */
		queue_work(dev->wq, &dev->rf_work);

		dev_kfree_skb(resp);
		return 0;
	}

	dev_dbg(dev->dev, "Creating new target");

	nfc_target.supported_protocols = NFC_PROTO_NFC_DEP_MASK;
	nfc_target.nfcid1_len = 10;
	memcpy(nfc_target.nfcid1, rsp->nfcid3t, nfc_target.nfcid1_len);
	rc = nfc_targets_found(dev->nfc_dev, &nfc_target, 1);
	if (rc)
		goto error;

	dev->tgt_available_prots = 0;
	dev->tgt_active_prot = NFC_PROTO_NFC_DEP;

	/* ATR_RES general bytes are located at offset 17 */
	target_gt_len = resp->len - 17;
	rc = nfc_set_remote_general_bytes(dev->nfc_dev,
					  rsp->gt, target_gt_len);
	if (!rc) {
		rc = nfc_dep_link_is_up(dev->nfc_dev,
					dev->nfc_dev->targets[0].idx,
					0, NFC_RF_INITIATOR);

		if (!rc)
			pn533_poll_reset_mod_list(dev);
	}
error:
	dev_kfree_skb(resp);
	return rc;
}

#define PASSIVE_DATA_LEN 5
static int pn533_poll_dep(struct nfc_dev *nfc_dev)
{
	struct pn533 *dev = nfc_get_drvdata(nfc_dev);
	struct sk_buff *skb;
	int rc, skb_len;
	u8 *next, nfcid3[NFC_NFCID3_MAXSIZE];
	u8 passive_data[PASSIVE_DATA_LEN] = {0x00, 0xff, 0xff, 0x00, 0x3};

	if (!dev->gb) {
		dev->gb = nfc_get_local_general_bytes(nfc_dev, &dev->gb_len);

		if (!dev->gb || !dev->gb_len) {
			dev->poll_dep = 0;
			queue_work(dev->wq, &dev->rf_work);
		}
	}

	skb_len = 3 + dev->gb_len; /* ActPass + BR + Next */
	skb_len += PASSIVE_DATA_LEN;

	/* NFCID3 */
	skb_len += NFC_NFCID3_MAXSIZE;
	nfcid3[0] = 0x1;
	nfcid3[1] = 0xfe;
	get_random_bytes(nfcid3 + 2, 6);

	skb = pn533_alloc_skb(dev, skb_len);
	if (!skb)
		return -ENOMEM;

	skb_put_u8(skb, 0x01);  /* Active */
	skb_put_u8(skb, 0x02);  /* 424 kbps */

	next = skb_put(skb, 1);  /* Next */
	*next = 0;

	/* Copy passive data */
	skb_put_data(skb, passive_data, PASSIVE_DATA_LEN);
	*next |= 1;

	/* Copy NFCID3 (which is NFCID2 from SENSF_RES) */
	skb_put_data(skb, nfcid3, NFC_NFCID3_MAXSIZE);
	*next |= 2;

	skb_put_data(skb, dev->gb, dev->gb_len);
	*next |= 4; /* We have some Gi */

	rc = pn533_send_cmd_async(dev, PN533_CMD_IN_JUMP_FOR_DEP, skb,
				  pn533_poll_dep_complete, NULL);

	if (rc < 0)
		dev_kfree_skb(skb);

	return rc;
}

static int pn533_autopoll_complete(struct pn533 *dev, void *arg,
			       struct sk_buff *resp)
{
	struct pn532_autopoll_resp *apr;
	struct nfc_target nfc_tgt;
	u8 nbtg;
	int rc;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);

		nfc_err(dev->dev, "%s  autopoll complete error %d\n",
			__func__, rc);

		if (rc == -ENOENT) {
			if (dev->poll_mod_count != 0)
				return rc;
			goto stop_poll;
		} else if (rc < 0) {
			nfc_err(dev->dev,
				"Error %d when running autopoll\n", rc);
			goto stop_poll;
		}
	}

	nbtg = resp->data[0];
	if ((nbtg > 2) || (nbtg <= 0))
		return -EAGAIN;

	apr = (struct pn532_autopoll_resp *)&resp->data[1];
	while (nbtg--) {
		memset(&nfc_tgt, 0, sizeof(struct nfc_target));
		switch (apr->type) {
		case PN532_AUTOPOLL_TYPE_ISOA:
			dev_dbg(dev->dev, "ISOA\n");
			rc = pn533_target_found_type_a(&nfc_tgt, apr->tgdata,
						       apr->ln - 1);
			break;
		case PN532_AUTOPOLL_TYPE_FELICA212:
		case PN532_AUTOPOLL_TYPE_FELICA424:
			dev_dbg(dev->dev, "FELICA\n");
			rc = pn533_target_found_felica(&nfc_tgt, apr->tgdata,
						       apr->ln - 1);
			break;
		case PN532_AUTOPOLL_TYPE_JEWEL:
			dev_dbg(dev->dev, "JEWEL\n");
			rc = pn533_target_found_jewel(&nfc_tgt, apr->tgdata,
						      apr->ln - 1);
			break;
		case PN532_AUTOPOLL_TYPE_ISOB:
			dev_dbg(dev->dev, "ISOB\n");
			rc = pn533_target_found_type_b(&nfc_tgt, apr->tgdata,
						       apr->ln - 1);
			break;
		case PN532_AUTOPOLL_TYPE_MIFARE:
			dev_dbg(dev->dev, "Mifare\n");
			rc = pn533_target_found_type_a(&nfc_tgt, apr->tgdata,
						       apr->ln - 1);
			break;
		default:
			nfc_err(dev->dev,
				    "Unknown current poll modulation\n");
			rc = -EPROTO;
		}

		if (rc)
			goto done;

		if (!(nfc_tgt.supported_protocols & dev->poll_protocols)) {
			nfc_err(dev->dev,
				    "The Tg found doesn't have the desired protocol\n");
			rc = -EAGAIN;
			goto done;
		}

		dev->tgt_available_prots = nfc_tgt.supported_protocols;
		apr = (struct pn532_autopoll_resp *)
			(apr->tgdata + (apr->ln - 1));
	}

	pn533_poll_reset_mod_list(dev);
	nfc_targets_found(dev->nfc_dev, &nfc_tgt, 1);

done:
	dev_kfree_skb(resp);
	return rc;

stop_poll:
	nfc_err(dev->dev, "autopoll operation has been stopped\n");

	pn533_poll_reset_mod_list(dev);
	dev->poll_protocols = 0;
	return rc;
}

static int pn533_poll_complete(struct pn533 *dev, void *arg,
			       struct sk_buff *resp)
{
	struct pn533_poll_modulations *cur_mod;
	int rc;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);

		nfc_err(dev->dev, "%s  Poll complete error %d\n",
			__func__, rc);

		if (rc == -ENOENT) {
			if (dev->poll_mod_count != 0)
				return rc;
			goto stop_poll;
		} else if (rc < 0) {
			nfc_err(dev->dev,
				"Error %d when running poll\n", rc);
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
		dev_dbg(dev->dev, "Polling has been stopped\n");
		goto done;
	}

	pn533_poll_next_mod(dev);
	/* Not target found, turn radio off */
	queue_work(dev->wq, &dev->rf_work);

done:
	dev_kfree_skb(resp);
	return rc;

stop_poll:
	nfc_err(dev->dev, "Polling operation has been stopped\n");

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

	skb_put_data(skb, &mod->data, mod->len);

	return skb;
}

static int pn533_send_poll_frame(struct pn533 *dev)
{
	struct pn533_poll_modulations *mod;
	struct sk_buff *skb;
	int rc;
	u8 cmd_code;

	mod = dev->poll_mod_active[dev->poll_mod_curr];

	dev_dbg(dev->dev, "%s mod len %d\n",
		__func__, mod->len);

	if ((dev->poll_protocols & NFC_PROTO_NFC_DEP_MASK) && dev->poll_dep)  {
		dev->poll_dep = 0;
		return pn533_poll_dep(dev->nfc_dev);
	}

	if (mod->len == 0) {  /* Listen mode */
		cmd_code = PN533_CMD_TG_INIT_AS_TARGET;
		skb = pn533_alloc_poll_tg_frame(dev);
	} else {  /* Polling mode */
		cmd_code =  PN533_CMD_IN_LIST_PASSIVE_TARGET;
		skb = pn533_alloc_poll_in_frame(dev, mod);
	}

	if (!skb) {
		nfc_err(dev->dev, "Failed to allocate skb\n");
		return -ENOMEM;
	}

	rc = pn533_send_cmd_async(dev, cmd_code, skb, pn533_poll_complete,
				  NULL);
	if (rc < 0) {
		dev_kfree_skb(skb);
		nfc_err(dev->dev, "Polling loop error %d\n", rc);
	}

	return rc;
}

static void pn533_wq_poll(struct work_struct *work)
{
	struct pn533 *dev = container_of(work, struct pn533, poll_work.work);
	struct pn533_poll_modulations *cur_mod;
	int rc;

	cur_mod = dev->poll_mod_active[dev->poll_mod_curr];

	dev_dbg(dev->dev,
		"%s cancel_listen %d modulation len %d\n",
		__func__, dev->cancel_listen, cur_mod->len);

	if (dev->cancel_listen == 1) {
		dev->cancel_listen = 0;
		dev->phy_ops->abort_cmd(dev, GFP_ATOMIC);
	}

	rc = pn533_send_poll_frame(dev);
	if (rc)
		return;

	if (cur_mod->len == 0 && dev->poll_mod_count > 1)
		mod_timer(&dev->listen_timer, jiffies + PN533_LISTEN_TIME * HZ);
}

static int pn533_start_poll(struct nfc_dev *nfc_dev,
			    u32 im_protocols, u32 tm_protocols)
{
	struct pn533 *dev = nfc_get_drvdata(nfc_dev);
	struct pn533_poll_modulations *cur_mod;
	struct sk_buff *skb;
	u8 rand_mod;
	int rc;

	dev_dbg(dev->dev,
		"%s: im protocols 0x%x tm protocols 0x%x\n",
		__func__, im_protocols, tm_protocols);

	if (dev->tgt_active_prot) {
		nfc_err(dev->dev,
			"Cannot poll with a target already activated\n");
		return -EBUSY;
	}

	if (dev->tgt_mode) {
		nfc_err(dev->dev,
			"Cannot poll while already being activated\n");
		return -EBUSY;
	}

	if (tm_protocols) {
		dev->gb = nfc_get_local_general_bytes(nfc_dev, &dev->gb_len);
		if (dev->gb == NULL)
			tm_protocols = 0;
	}

	dev->poll_protocols = im_protocols;
	dev->listen_protocols = tm_protocols;
	if (dev->device_type == PN533_DEVICE_PN532_AUTOPOLL) {
		skb = pn533_alloc_skb(dev, 4 + 6);
		if (!skb)
			return -ENOMEM;

		*((u8 *)skb_put(skb, sizeof(u8))) =
			PN532_AUTOPOLL_POLLNR_INFINITE;
		*((u8 *)skb_put(skb, sizeof(u8))) = PN532_AUTOPOLL_PERIOD;

		if ((im_protocols & NFC_PROTO_MIFARE_MASK) &&
				(im_protocols & NFC_PROTO_ISO14443_MASK) &&
				(im_protocols & NFC_PROTO_NFC_DEP_MASK))
			*((u8 *)skb_put(skb, sizeof(u8))) =
				PN532_AUTOPOLL_TYPE_GENERIC_106;
		else {
			if (im_protocols & NFC_PROTO_MIFARE_MASK)
				*((u8 *)skb_put(skb, sizeof(u8))) =
					PN532_AUTOPOLL_TYPE_MIFARE;

			if (im_protocols & NFC_PROTO_ISO14443_MASK)
				*((u8 *)skb_put(skb, sizeof(u8))) =
					PN532_AUTOPOLL_TYPE_ISOA;

			if (im_protocols & NFC_PROTO_NFC_DEP_MASK) {
				*((u8 *)skb_put(skb, sizeof(u8))) =
					PN532_AUTOPOLL_TYPE_DEP_PASSIVE_106;
				*((u8 *)skb_put(skb, sizeof(u8))) =
					PN532_AUTOPOLL_TYPE_DEP_PASSIVE_212;
				*((u8 *)skb_put(skb, sizeof(u8))) =
					PN532_AUTOPOLL_TYPE_DEP_PASSIVE_424;
			}
		}

		if (im_protocols & NFC_PROTO_FELICA_MASK ||
				im_protocols & NFC_PROTO_NFC_DEP_MASK) {
			*((u8 *)skb_put(skb, sizeof(u8))) =
				PN532_AUTOPOLL_TYPE_FELICA212;
			*((u8 *)skb_put(skb, sizeof(u8))) =
				PN532_AUTOPOLL_TYPE_FELICA424;
		}

		if (im_protocols & NFC_PROTO_JEWEL_MASK)
			*((u8 *)skb_put(skb, sizeof(u8))) =
				PN532_AUTOPOLL_TYPE_JEWEL;

		if (im_protocols & NFC_PROTO_ISO14443_B_MASK)
			*((u8 *)skb_put(skb, sizeof(u8))) =
				PN532_AUTOPOLL_TYPE_ISOB;

		if (tm_protocols)
			*((u8 *)skb_put(skb, sizeof(u8))) =
				PN532_AUTOPOLL_TYPE_DEP_ACTIVE_106;

		rc = pn533_send_cmd_async(dev, PN533_CMD_IN_AUTOPOLL, skb,
				pn533_autopoll_complete, NULL);

		if (rc < 0)
			dev_kfree_skb(skb);
		else
			dev->poll_mod_count++;

		return rc;
	}

	pn533_poll_create_mod_list(dev, im_protocols, tm_protocols);

	/* Do not always start polling from the same modulation */
	get_random_bytes(&rand_mod, sizeof(rand_mod));
	rand_mod %= dev->poll_mod_count;
	dev->poll_mod_curr = rand_mod;

	cur_mod = dev->poll_mod_active[dev->poll_mod_curr];

	rc = pn533_send_poll_frame(dev);

	/* Start listen timer */
	if (!rc && cur_mod->len == 0 && dev->poll_mod_count > 1)
		mod_timer(&dev->listen_timer, jiffies + PN533_LISTEN_TIME * HZ);

	return rc;
}

static void pn533_stop_poll(struct nfc_dev *nfc_dev)
{
	struct pn533 *dev = nfc_get_drvdata(nfc_dev);

	del_timer(&dev->listen_timer);

	if (!dev->poll_mod_count) {
		dev_dbg(dev->dev,
			"Polling operation was not running\n");
		return;
	}

	dev->phy_ops->abort_cmd(dev, GFP_KERNEL);
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

	skb = pn533_alloc_skb(dev, sizeof(u8) * 2); /*TG + Next*/
	if (!skb)
		return -ENOMEM;

	skb_put_u8(skb, 1); /* TG */
	skb_put_u8(skb, 0); /* Next */

	resp = pn533_send_cmd_sync(dev, PN533_CMD_IN_ATR, skb);
	if (IS_ERR(resp))
		return PTR_ERR(resp);

	rsp = (struct pn533_cmd_activate_response *)resp->data;
	rc = rsp->status & PN533_CMD_RET_MASK;
	if (rc != PN533_CMD_RET_SUCCESS) {
		nfc_err(dev->dev,
			"Target activation failed (error 0x%x)\n", rc);
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

	dev_dbg(dev->dev, "%s: protocol=%u\n", __func__, protocol);

	if (dev->poll_mod_count) {
		nfc_err(dev->dev,
			"Cannot activate while polling\n");
		return -EBUSY;
	}

	if (dev->tgt_active_prot) {
		nfc_err(dev->dev,
			"There is already an active target\n");
		return -EBUSY;
	}

	if (!dev->tgt_available_prots) {
		nfc_err(dev->dev,
			"There is no available target to activate\n");
		return -EINVAL;
	}

	if (!(dev->tgt_available_prots & (1 << protocol))) {
		nfc_err(dev->dev,
			"Target doesn't support requested proto %u\n",
			protocol);
		return -EINVAL;
	}

	if (protocol == NFC_PROTO_NFC_DEP) {
		rc = pn533_activate_target_nfcdep(dev);
		if (rc) {
			nfc_err(dev->dev,
				"Activating target with DEP failed %d\n", rc);
			return rc;
		}
	}

	dev->tgt_active_prot = protocol;
	dev->tgt_available_prots = 0;

	return 0;
}

static int pn533_deactivate_target_complete(struct pn533 *dev, void *arg,
			     struct sk_buff *resp)
{
	int rc = 0;

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);

		nfc_err(dev->dev, "Target release error %d\n", rc);

		return rc;
	}

	rc = resp->data[0] & PN533_CMD_RET_MASK;
	if (rc != PN533_CMD_RET_SUCCESS)
		nfc_err(dev->dev,
			"Error 0x%x when releasing the target\n", rc);

	dev_kfree_skb(resp);
	return rc;
}

static void pn533_deactivate_target(struct nfc_dev *nfc_dev,
				    struct nfc_target *target, u8 mode)
{
	struct pn533 *dev = nfc_get_drvdata(nfc_dev);
	struct sk_buff *skb;
	int rc;

	if (!dev->tgt_active_prot) {
		nfc_err(dev->dev, "There is no active target\n");
		return;
	}

	dev->tgt_active_prot = 0;
	skb_queue_purge(&dev->resp_q);

	skb = pn533_alloc_skb(dev, sizeof(u8));
	if (!skb)
		return;

	skb_put_u8(skb, 1); /* TG*/

	rc = pn533_send_cmd_async(dev, PN533_CMD_IN_RELEASE, skb,
				  pn533_deactivate_target_complete, NULL);
	if (rc < 0) {
		dev_kfree_skb(skb);
		nfc_err(dev->dev, "Target release error %d\n", rc);
	}
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
		nfc_err(dev->dev,
			"The target does not support DEP\n");
		rc =  -EINVAL;
		goto error;
	}

	rsp = (struct pn533_cmd_jump_dep_response *)resp->data;

	rc = rsp->status & PN533_CMD_RET_MASK;
	if (rc != PN533_CMD_RET_SUCCESS) {
		nfc_err(dev->dev,
			"Bringing DEP link up failed (error 0x%x)\n", rc);
		goto error;
	}

	if (!dev->tgt_available_prots) {
		struct nfc_target nfc_target;

		dev_dbg(dev->dev, "Creating new target\n");

		memset(&nfc_target, 0, sizeof(struct nfc_target));

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
static int pn533_dep_link_up(struct nfc_dev *nfc_dev, struct nfc_target *target,
			     u8 comm_mode, u8 *gb, size_t gb_len)
{
	struct pn533 *dev = nfc_get_drvdata(nfc_dev);
	struct sk_buff *skb;
	int rc, skb_len;
	u8 *next, *arg, nfcid3[NFC_NFCID3_MAXSIZE];
	u8 passive_data[PASSIVE_DATA_LEN] = {0x00, 0xff, 0xff, 0x00, 0x3};

	if (dev->poll_mod_count) {
		nfc_err(dev->dev,
			"Cannot bring the DEP link up while polling\n");
		return -EBUSY;
	}

	if (dev->tgt_active_prot) {
		nfc_err(dev->dev,
			"There is already an active target\n");
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

	skb_put_u8(skb, !comm_mode);  /* ActPass */
	skb_put_u8(skb, 0x02);  /* 424 kbps */

	next = skb_put(skb, 1);  /* Next */
	*next = 0;

	/* Copy passive data */
	skb_put_data(skb, passive_data, PASSIVE_DATA_LEN);
	*next |= 1;

	/* Copy NFCID3 (which is NFCID2 from SENSF_RES) */
	if (target && target->nfcid2_len)
		memcpy(skb_put(skb, NFC_NFCID3_MAXSIZE), target->nfcid2,
		       target->nfcid2_len);
	else
		skb_put_data(skb, nfcid3, NFC_NFCID3_MAXSIZE);
	*next |= 2;

	if (gb != NULL && gb_len > 0) {
		skb_put_data(skb, gb, gb_len);
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

	pn533_poll_reset_mod_list(dev);

	if (dev->tgt_mode || dev->tgt_active_prot)
		dev->phy_ops->abort_cmd(dev, GFP_KERNEL);

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

	if (skb_queue_empty(&dev->resp_q))
		return NULL;

	if (skb_queue_len(&dev->resp_q) == 1) {
		skb = skb_dequeue(&dev->resp_q);
		goto out;
	}

	skb_queue_walk_safe(&dev->resp_q, tmp, t)
		skb_len += tmp->len;

	dev_dbg(dev->dev, "%s total length %d\n",
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

	if (IS_ERR(resp)) {
		rc = PTR_ERR(resp);
		goto _error;
	}

	status = resp->data[0];
	ret = status & PN533_CMD_RET_MASK;
	mi = status & PN533_CMD_MI_MASK;

	skb_pull(resp, sizeof(status));

	if (ret != PN533_CMD_RET_SUCCESS) {
		nfc_err(dev->dev,
			"Exchanging data failed (error 0x%x)\n", ret);
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
	if (!skb) {
		rc = -ENOMEM;
		goto error;
	}

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

/*
 * Receive an incoming pn533 frame. skb contains only header and payload.
 * If skb == NULL, it is a notification that the link below is dead.
 */
void pn533_recv_frame(struct pn533 *dev, struct sk_buff *skb, int status)
{
	if (!dev->cmd)
		goto sched_wq;

	dev->cmd->status = status;

	if (status != 0) {
		dev_dbg(dev->dev, "%s: Error received: %d\n", __func__, status);
		goto sched_wq;
	}

	if (skb == NULL) {
		dev_err(dev->dev, "NULL Frame -> link is dead\n");
		goto sched_wq;
	}

	if (pn533_rx_frame_is_ack(skb->data)) {
		dev_dbg(dev->dev, "%s: Received ACK frame\n", __func__);
		dev_kfree_skb(skb);
		return;
	}

	print_hex_dump_debug("PN533 RX: ", DUMP_PREFIX_NONE, 16, 1, skb->data,
			     dev->ops->rx_frame_size(skb->data), false);

	if (!dev->ops->rx_is_frame_valid(skb->data, dev)) {
		nfc_err(dev->dev, "Received an invalid frame\n");
		dev->cmd->status = -EIO;
	} else if (!pn533_rx_frame_is_cmd_response(dev, skb->data)) {
		nfc_err(dev->dev, "It it not the response to the last command\n");
		dev->cmd->status = -EIO;
	}

	dev->cmd->resp = skb;

sched_wq:
	queue_work(dev->wq, &dev->cmd_complete_work);
}
EXPORT_SYMBOL(pn533_recv_frame);

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
			return -ENOMEM;
		}

		if (!dev->tgt_mode) {
			/* Reserve the TG/MI byte */
			skb_reserve(frag, 1);

			/* MI + TG */
			if (frag_size  == PN533_CMD_DATAFRAME_MAXLEN)
				*(u8 *)skb_push(frag, sizeof(u8)) =
						(PN533_CMD_MI_MASK | 1);
			else
				*(u8 *)skb_push(frag, sizeof(u8)) =  1; /* TG */
		}

		skb_put_data(frag, skb->data, frag_size);

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

	if (!dev->tgt_active_prot) {
		nfc_err(dev->dev,
			"Can't exchange data if there is no active target\n");
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
		fallthrough;
	default:
		/* jumbo frame ? */
		if (skb->len > PN533_CMD_DATAEXCH_DATA_MAXLEN) {
			rc = pn533_fill_fragment_skbs(dev, skb);
			if (rc < 0)
				goto error;

			skb = skb_dequeue(&dev->fragment_skb);
			if (!skb) {
				rc = -EIO;
				goto error;
			}
		} else {
			*(u8 *)skb_push(skb, sizeof(u8)) =  1; /* TG */
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

	if (IS_ERR(resp))
		return PTR_ERR(resp);

	status = resp->data[0];

	/* Prepare for the next round */
	if (skb_queue_len(&dev->fragment_skb) > 0) {
		queue_work(dev->wq, &dev->mi_tm_tx_work);
		return -EINPROGRESS;
	}
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

	/* let's split in multiple chunks if size's too big */
	if (skb->len > PN533_CMD_DATAEXCH_DATA_MAXLEN) {
		rc = pn533_fill_fragment_skbs(dev, skb);
		if (rc < 0)
			goto error;

		/* get the first skb */
		skb = skb_dequeue(&dev->fragment_skb);
		if (!skb) {
			rc = -EIO;
			goto error;
		}

		rc = pn533_send_data_async(dev, PN533_CMD_TG_SET_META_DATA, skb,
						pn533_tm_send_complete, NULL);
	} else {
		/* Send th skb */
		rc = pn533_send_data_async(dev, PN533_CMD_TG_SET_DATA, skb,
						pn533_tm_send_complete, NULL);
	}

error:
	if (rc < 0) {
		dev_kfree_skb(skb);
		skb_queue_purge(&dev->fragment_skb);
	}

	return rc;
}

static void pn533_wq_mi_recv(struct work_struct *work)
{
	struct pn533 *dev = container_of(work, struct pn533, mi_rx_work);
	struct sk_buff *skb;
	int rc;

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
		fallthrough;
	default:
		skb_put_u8(skb, 1); /*TG*/

		rc = pn533_send_cmd_direct_async(dev,
						 PN533_CMD_IN_DATA_EXCHANGE,
						 skb,
						 pn533_data_exchange_complete,
						 dev->cmd_complete_mi_arg);

		break;
	}

	if (rc == 0) /* success */
		return;

	nfc_err(dev->dev,
		"Error %d when trying to perform data_exchange\n", rc);

	dev_kfree_skb(skb);
	kfree(dev->cmd_complete_mi_arg);

error:
	dev->phy_ops->send_ack(dev, GFP_KERNEL);
	queue_work(dev->wq, &dev->cmd_work);
}

static void pn533_wq_mi_send(struct work_struct *work)
{
	struct pn533 *dev = container_of(work, struct pn533, mi_tx_work);
	struct sk_buff *skb;
	int rc;

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
		rc = pn533_send_cmd_direct_async(dev,
						 PN533_CMD_IN_DATA_EXCHANGE,
						 skb,
						 pn533_data_exchange_complete,
						 dev->cmd_complete_dep_arg);

		break;
	}

	if (rc == 0) /* success */
		return;

	nfc_err(dev->dev,
		"Error %d when trying to perform data_exchange\n", rc);

	dev_kfree_skb(skb);
	kfree(dev->cmd_complete_dep_arg);

error:
	dev->phy_ops->send_ack(dev, GFP_KERNEL);
	queue_work(dev->wq, &dev->cmd_work);
}

static int pn533_set_configuration(struct pn533 *dev, u8 cfgitem, u8 *cfgdata,
								u8 cfgdata_len)
{
	struct sk_buff *skb;
	struct sk_buff *resp;
	int skb_len;

	skb_len = sizeof(cfgitem) + cfgdata_len; /* cfgitem + cfgdata */

	skb = pn533_alloc_skb(dev, skb_len);
	if (!skb)
		return -ENOMEM;

	skb_put_u8(skb, cfgitem);
	skb_put_data(skb, cfgdata, cfgdata_len);

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

	skb = pn533_alloc_skb(dev, sizeof(u8));
	if (!skb)
		return -ENOMEM;

	skb_put_u8(skb, 0x1);

	resp = pn533_send_cmd_sync(dev, 0x18, skb);
	if (IS_ERR(resp))
		return PTR_ERR(resp);

	dev_kfree_skb(resp);

	return 0;
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
		nfc_err(dev->dev, "Error on setting RF field\n");
		return rc;
	}

	return 0;
}

static int pn532_sam_configuration(struct nfc_dev *nfc_dev)
{
	struct pn533 *dev = nfc_get_drvdata(nfc_dev);
	struct sk_buff *skb;
	struct sk_buff *resp;

	skb = pn533_alloc_skb(dev, 1);
	if (!skb)
		return -ENOMEM;

	skb_put_u8(skb, 0x01);

	resp = pn533_send_cmd_sync(dev, PN533_CMD_SAM_CONFIGURATION, skb);
	if (IS_ERR(resp))
		return PTR_ERR(resp);

	dev_kfree_skb(resp);
	return 0;
}

static int pn533_dev_up(struct nfc_dev *nfc_dev)
{
	struct pn533 *dev = nfc_get_drvdata(nfc_dev);
	int rc;

	if (dev->phy_ops->dev_up) {
		rc = dev->phy_ops->dev_up(dev);
		if (rc)
			return rc;
	}

	if ((dev->device_type == PN533_DEVICE_PN532) ||
		(dev->device_type == PN533_DEVICE_PN532_AUTOPOLL)) {
		rc = pn532_sam_configuration(nfc_dev);

		if (rc)
			return rc;
	}

	return pn533_rf_field(nfc_dev, 1);
}

static int pn533_dev_down(struct nfc_dev *nfc_dev)
{
	struct pn533 *dev = nfc_get_drvdata(nfc_dev);
	int ret;

	ret = pn533_rf_field(nfc_dev, 0);
	if (dev->phy_ops->dev_down && !ret)
		ret = dev->phy_ops->dev_down(dev);

	return ret;
}

static const struct nfc_ops pn533_nfc_ops = {
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
	case PN533_DEVICE_PN532:
	case PN533_DEVICE_PN532_AUTOPOLL:
		max_retries.mx_rty_atr = 0x2;
		max_retries.mx_rty_psl = 0x1;
		max_retries.mx_rty_passive_act =
			PN533_CONFIG_MAX_RETRIES_NO_RETRY;

		timing.rfu = PN533_CONFIG_TIMING_102;
		timing.atr_res_timeout = PN533_CONFIG_TIMING_102;
		timing.dep_timeout = PN533_CONFIG_TIMING_204;

		break;

	default:
		nfc_err(dev->dev, "Unknown device type %d\n",
			dev->device_type);
		return -EINVAL;
	}

	rc = pn533_set_configuration(dev, PN533_CFGITEM_MAX_RETRIES,
				     (u8 *)&max_retries, sizeof(max_retries));
	if (rc) {
		nfc_err(dev->dev,
			"Error on setting MAX_RETRIES config\n");
		return rc;
	}


	rc = pn533_set_configuration(dev, PN533_CFGITEM_TIMING,
				     (u8 *)&timing, sizeof(timing));
	if (rc) {
		nfc_err(dev->dev, "Error on setting RF timings\n");
		return rc;
	}

	switch (dev->device_type) {
	case PN533_DEVICE_STD:
	case PN533_DEVICE_PN532:
	case PN533_DEVICE_PN532_AUTOPOLL:
		break;

	case PN533_DEVICE_PASORI:
		pn533_pasori_fw_reset(dev);

		rc = pn533_set_configuration(dev, PN533_CFGITEM_PASORI,
					     pasori_cfg, 3);
		if (rc) {
			nfc_err(dev->dev,
				"Error while settings PASORI config\n");
			return rc;
		}

		pn533_pasori_fw_reset(dev);

		break;
	}

	return 0;
}

int pn533_finalize_setup(struct pn533 *dev)
{

	struct pn533_fw_version fw_ver;
	int rc;

	memset(&fw_ver, 0, sizeof(fw_ver));

	rc = pn533_get_firmware_version(dev, &fw_ver);
	if (rc) {
		nfc_err(dev->dev, "Unable to get FW version\n");
		return rc;
	}

	nfc_info(dev->dev, "NXP PN5%02X firmware ver %d.%d now attached\n",
		fw_ver.ic, fw_ver.ver, fw_ver.rev);

	rc = pn533_setup(dev);
	if (rc)
		return rc;

	return 0;
}
EXPORT_SYMBOL_GPL(pn533_finalize_setup);

struct pn533 *pn53x_common_init(u32 device_type,
				enum pn533_protocol_type protocol_type,
				void *phy,
				const struct pn533_phy_ops *phy_ops,
				struct pn533_frame_ops *fops,
				struct device *dev)
{
	struct pn533 *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	priv->phy = phy;
	priv->phy_ops = phy_ops;
	priv->dev = dev;
	if (fops != NULL)
		priv->ops = fops;
	else
		priv->ops = &pn533_std_frame_ops;

	priv->protocol_type = protocol_type;
	priv->device_type = device_type;

	mutex_init(&priv->cmd_lock);

	INIT_WORK(&priv->cmd_work, pn533_wq_cmd);
	INIT_WORK(&priv->cmd_complete_work, pn533_wq_cmd_complete);
	INIT_WORK(&priv->mi_rx_work, pn533_wq_mi_recv);
	INIT_WORK(&priv->mi_tx_work, pn533_wq_mi_send);
	INIT_WORK(&priv->tg_work, pn533_wq_tg_get_data);
	INIT_WORK(&priv->mi_tm_rx_work, pn533_wq_tm_mi_recv);
	INIT_WORK(&priv->mi_tm_tx_work, pn533_wq_tm_mi_send);
	INIT_DELAYED_WORK(&priv->poll_work, pn533_wq_poll);
	INIT_WORK(&priv->rf_work, pn533_wq_rf);
	priv->wq = alloc_ordered_workqueue("pn533", 0);
	if (priv->wq == NULL)
		goto error;

	timer_setup(&priv->listen_timer, pn533_listen_mode_timer, 0);

	skb_queue_head_init(&priv->resp_q);
	skb_queue_head_init(&priv->fragment_skb);

	INIT_LIST_HEAD(&priv->cmd_queue);
	return priv;

error:
	kfree(priv);
	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL_GPL(pn53x_common_init);

void pn53x_common_clean(struct pn533 *priv)
{
	struct pn533_cmd *cmd, *n;

	/* delete the timer before cleanup the worker */
	del_timer_sync(&priv->listen_timer);

	flush_delayed_work(&priv->poll_work);
	destroy_workqueue(priv->wq);

	skb_queue_purge(&priv->resp_q);

	list_for_each_entry_safe(cmd, n, &priv->cmd_queue, queue) {
		list_del(&cmd->queue);
		kfree(cmd);
	}

	kfree(priv);
}
EXPORT_SYMBOL_GPL(pn53x_common_clean);

int pn532_i2c_nfc_alloc(struct pn533 *priv, u32 protocols,
			struct device *parent)
{
	priv->nfc_dev = nfc_allocate_device(&pn533_nfc_ops, protocols,
					   priv->ops->tx_header_len +
					   PN533_CMD_DATAEXCH_HEAD_LEN,
					   priv->ops->tx_tail_len);
	if (!priv->nfc_dev)
		return -ENOMEM;

	nfc_set_parent_dev(priv->nfc_dev, parent);
	nfc_set_drvdata(priv->nfc_dev, priv);
	return 0;
}
EXPORT_SYMBOL_GPL(pn532_i2c_nfc_alloc);

int pn53x_register_nfc(struct pn533 *priv, u32 protocols,
			struct device *parent)
{
	int rc;

	rc = pn532_i2c_nfc_alloc(priv, protocols, parent);
	if (rc)
		return rc;

	rc = nfc_register_device(priv->nfc_dev);
	if (rc)
		nfc_free_device(priv->nfc_dev);

	return rc;
}
EXPORT_SYMBOL_GPL(pn53x_register_nfc);

void pn53x_unregister_nfc(struct pn533 *priv)
{
	nfc_unregister_device(priv->nfc_dev);
	nfc_free_device(priv->nfc_dev);
}
EXPORT_SYMBOL_GPL(pn53x_unregister_nfc);

MODULE_AUTHOR("Lauro Ramos Venancio <lauro.venancio@openbossa.org>");
MODULE_AUTHOR("Aloisio Almeida Jr <aloisio.almeida@openbossa.org>");
MODULE_AUTHOR("Waldemar Rymarkiewicz <waldemar.rymarkiewicz@tieto.com>");
MODULE_DESCRIPTION("PN533 driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
