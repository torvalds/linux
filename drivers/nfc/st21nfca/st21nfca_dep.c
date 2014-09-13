/*
 * Copyright (C) 2014  STMicroelectronics SAS. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <net/nfc/hci.h>

#include "st21nfca.h"
#include "st21nfca_dep.h"

#define ST21NFCA_NFCIP1_INITIATOR 0x00
#define ST21NFCA_NFCIP1_REQ 0xd4
#define ST21NFCA_NFCIP1_RES 0xd5
#define ST21NFCA_NFCIP1_ATR_REQ 0x00
#define ST21NFCA_NFCIP1_ATR_RES 0x01
#define ST21NFCA_NFCIP1_PSL_REQ 0x04
#define ST21NFCA_NFCIP1_PSL_RES 0x05
#define ST21NFCA_NFCIP1_DEP_REQ 0x06
#define ST21NFCA_NFCIP1_DEP_RES 0x07

#define ST21NFCA_NFC_DEP_PFB_PNI(pfb)     ((pfb) & 0x03)
#define ST21NFCA_NFC_DEP_PFB_TYPE(pfb) ((pfb) & 0xE0)
#define ST21NFCA_NFC_DEP_PFB_IS_TIMEOUT(pfb) \
				((pfb) & ST21NFCA_NFC_DEP_PFB_TIMEOUT_BIT)
#define ST21NFCA_NFC_DEP_DID_BIT_SET(pfb) ((pfb) & 0x04)
#define ST21NFCA_NFC_DEP_NAD_BIT_SET(pfb) ((pfb) & 0x08)
#define ST21NFCA_NFC_DEP_PFB_TIMEOUT_BIT 0x10

#define ST21NFCA_NFC_DEP_PFB_IS_TIMEOUT(pfb) \
				((pfb) & ST21NFCA_NFC_DEP_PFB_TIMEOUT_BIT)

#define ST21NFCA_NFC_DEP_PFB_I_PDU          0x00
#define ST21NFCA_NFC_DEP_PFB_ACK_NACK_PDU   0x40
#define ST21NFCA_NFC_DEP_PFB_SUPERVISOR_PDU 0x80

#define ST21NFCA_ATR_REQ_MIN_SIZE 17
#define ST21NFCA_ATR_REQ_MAX_SIZE 65
#define ST21NFCA_LR_BITS_PAYLOAD_SIZE_254B 0x30
#define ST21NFCA_GB_BIT  0x02

#define ST21NFCA_EVT_CARD_F_BITRATE 0x16
#define ST21NFCA_EVT_READER_F_BITRATE 0x13
#define	ST21NFCA_PSL_REQ_SEND_SPEED(brs) (brs & 0x38)
#define ST21NFCA_PSL_REQ_RECV_SPEED(brs) (brs & 0x07)
#define ST21NFCA_PP2LRI(pp) ((pp & 0x30) >> 4)
#define ST21NFCA_CARD_BITRATE_212 0x01
#define ST21NFCA_CARD_BITRATE_424 0x02

#define ST21NFCA_DEFAULT_TIMEOUT 0x0a


#define PROTOCOL_ERR(req) pr_err("%d: ST21NFCA Protocol error: %s\n", \
				 __LINE__, req)

struct st21nfca_atr_req {
	u8 length;
	u8 cmd0;
	u8 cmd1;
	u8 nfcid3[NFC_NFCID3_MAXSIZE];
	u8 did;
	u8 bsi;
	u8 bri;
	u8 ppi;
	u8 gbi[0];
} __packed;

struct st21nfca_atr_res {
	u8 length;
	u8 cmd0;
	u8 cmd1;
	u8 nfcid3[NFC_NFCID3_MAXSIZE];
	u8 did;
	u8 bsi;
	u8 bri;
	u8 to;
	u8 ppi;
	u8 gbi[0];
} __packed;

struct st21nfca_psl_req {
	u8 length;
	u8 cmd0;
	u8 cmd1;
	u8 did;
	u8 brs;
	u8 fsl;
} __packed;

struct st21nfca_psl_res {
	u8 length;
	u8 cmd0;
	u8 cmd1;
	u8 did;
} __packed;

struct st21nfca_dep_req_res {
	u8 length;
	u8 cmd0;
	u8 cmd1;
	u8 pfb;
	u8 did;
	u8 nad;
} __packed;

static void st21nfca_tx_work(struct work_struct *work)
{
	struct st21nfca_hci_info *info = container_of(work,
						struct st21nfca_hci_info,
						dep_info.tx_work);

	struct nfc_dev *dev;
	struct sk_buff *skb;

	if (info) {
		dev = info->hdev->ndev;
		skb = info->dep_info.tx_pending;

		device_lock(&dev->dev);

		nfc_hci_send_cmd_async(info->hdev, ST21NFCA_RF_READER_F_GATE,
				ST21NFCA_WR_XCHG_DATA, skb->data, skb->len,
				info->async_cb, info);
		device_unlock(&dev->dev);
		kfree_skb(skb);
	}
}

static void st21nfca_im_send_pdu(struct st21nfca_hci_info *info,
						struct sk_buff *skb)
{
	info->dep_info.tx_pending = skb;
	schedule_work(&info->dep_info.tx_work);
}

static int st21nfca_tm_send_atr_res(struct nfc_hci_dev *hdev,
				    struct st21nfca_atr_req *atr_req)
{
	struct st21nfca_atr_res *atr_res;
	struct sk_buff *skb;
	size_t gb_len;
	int r;
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);

	gb_len = atr_req->length - sizeof(struct st21nfca_atr_req);
	skb = alloc_skb(atr_req->length + 1, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	skb_put(skb, sizeof(struct st21nfca_atr_res));

	atr_res = (struct st21nfca_atr_res *)skb->data;
	memset(atr_res, 0, sizeof(struct st21nfca_atr_res));

	atr_res->length = atr_req->length + 1;
	atr_res->cmd0 = ST21NFCA_NFCIP1_RES;
	atr_res->cmd1 = ST21NFCA_NFCIP1_ATR_RES;

	memcpy(atr_res->nfcid3, atr_req->nfcid3, 6);
	atr_res->bsi = 0x00;
	atr_res->bri = 0x00;
	atr_res->to = ST21NFCA_DEFAULT_TIMEOUT;
	atr_res->ppi = ST21NFCA_LR_BITS_PAYLOAD_SIZE_254B;

	if (gb_len) {
		skb_put(skb, gb_len);

		atr_res->ppi |= ST21NFCA_GB_BIT;
		memcpy(atr_res->gbi, atr_req->gbi, gb_len);
		r = nfc_set_remote_general_bytes(hdev->ndev, atr_res->gbi,
						  gb_len);
		if (r < 0)
			return r;
	}

	info->dep_info.curr_nfc_dep_pni = 0;

	return nfc_hci_send_event(hdev, ST21NFCA_RF_CARD_F_GATE,
				ST21NFCA_EVT_SEND_DATA, skb->data, skb->len);
}

static int st21nfca_tm_recv_atr_req(struct nfc_hci_dev *hdev,
				    struct sk_buff *skb)
{
	struct st21nfca_atr_req *atr_req;
	size_t gb_len;
	int r;

	skb_trim(skb, skb->len - 1);

	if (!skb->len) {
		r = -EIO;
		goto exit;
	}

	if (skb->len < ST21NFCA_ATR_REQ_MIN_SIZE) {
		r = -EPROTO;
		goto exit;
	}

	atr_req = (struct st21nfca_atr_req *)skb->data;

	if (atr_req->length < sizeof(struct st21nfca_atr_req)) {
		r = -EPROTO;
		goto exit;
	}

	r = st21nfca_tm_send_atr_res(hdev, atr_req);
	if (r)
		goto exit;

	gb_len = skb->len - sizeof(struct st21nfca_atr_req);

	r = nfc_tm_activated(hdev->ndev, NFC_PROTO_NFC_DEP_MASK,
			      NFC_COMM_PASSIVE, atr_req->gbi, gb_len);
	if (r)
		goto exit;

	r = 0;

exit:
	return r;
}

static int st21nfca_tm_send_psl_res(struct nfc_hci_dev *hdev,
				    struct st21nfca_psl_req *psl_req)
{
	struct st21nfca_psl_res *psl_res;
	struct sk_buff *skb;
	u8 bitrate[2] = {0, 0};
	int r;

	skb = alloc_skb(sizeof(struct st21nfca_psl_res), GFP_KERNEL);
	if (!skb)
		return -ENOMEM;
	skb_put(skb, sizeof(struct st21nfca_psl_res));

	psl_res = (struct st21nfca_psl_res *)skb->data;

	psl_res->length = sizeof(struct st21nfca_psl_res);
	psl_res->cmd0 = ST21NFCA_NFCIP1_RES;
	psl_res->cmd1 = ST21NFCA_NFCIP1_PSL_RES;
	psl_res->did = psl_req->did;

	r = nfc_hci_send_event(hdev, ST21NFCA_RF_CARD_F_GATE,
				ST21NFCA_EVT_SEND_DATA, skb->data, skb->len);

	/*
	 * ST21NFCA only support P2P passive.
	 * PSL_REQ BRS value != 0 has only a meaning to
	 * change technology to type F.
	 * We change to BITRATE 424Kbits.
	 * In other case switch to BITRATE 106Kbits.
	 */
	if (ST21NFCA_PSL_REQ_SEND_SPEED(psl_req->brs) &&
	    ST21NFCA_PSL_REQ_RECV_SPEED(psl_req->brs)) {
		bitrate[0] = ST21NFCA_CARD_BITRATE_424;
		bitrate[1] = ST21NFCA_CARD_BITRATE_424;
	}

	/* Send an event to change bitrate change event to card f */
	return nfc_hci_send_event(hdev, ST21NFCA_RF_CARD_F_GATE,
			ST21NFCA_EVT_CARD_F_BITRATE, bitrate, 2);
}

static int st21nfca_tm_recv_psl_req(struct nfc_hci_dev *hdev,
				    struct sk_buff *skb)
{
	struct st21nfca_psl_req *psl_req;
	int r;

	skb_trim(skb, skb->len - 1);

	if (!skb->len) {
		r = -EIO;
		goto exit;
	}

	psl_req = (struct st21nfca_psl_req *)skb->data;

	if (skb->len < sizeof(struct st21nfca_psl_req)) {
		r = -EIO;
		goto exit;
	}

	r = st21nfca_tm_send_psl_res(hdev, psl_req);
exit:
	return r;
}

int st21nfca_tm_send_dep_res(struct nfc_hci_dev *hdev, struct sk_buff *skb)
{
	int r;
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);

	*skb_push(skb, 1) = info->dep_info.curr_nfc_dep_pni;
	*skb_push(skb, 1) = ST21NFCA_NFCIP1_DEP_RES;
	*skb_push(skb, 1) = ST21NFCA_NFCIP1_RES;
	*skb_push(skb, 1) = skb->len;

	r = nfc_hci_send_event(hdev, ST21NFCA_RF_CARD_F_GATE,
			ST21NFCA_EVT_SEND_DATA, skb->data, skb->len);
	kfree_skb(skb);

	return r;
}
EXPORT_SYMBOL(st21nfca_tm_send_dep_res);

static int st21nfca_tm_recv_dep_req(struct nfc_hci_dev *hdev,
				    struct sk_buff *skb)
{
	struct st21nfca_dep_req_res *dep_req;
	u8 size;
	int r;
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);

	skb_trim(skb, skb->len - 1);

	size = 4;

	dep_req = (struct st21nfca_dep_req_res *)skb->data;
	if (skb->len < size) {
		r = -EIO;
		goto exit;
	}

	if (ST21NFCA_NFC_DEP_DID_BIT_SET(dep_req->pfb))
		size++;
	if (ST21NFCA_NFC_DEP_NAD_BIT_SET(dep_req->pfb))
		size++;

	if (skb->len < size) {
		r = -EIO;
		goto exit;
	}

	/* Receiving DEP_REQ - Decoding */
	switch (ST21NFCA_NFC_DEP_PFB_TYPE(dep_req->pfb)) {
	case ST21NFCA_NFC_DEP_PFB_I_PDU:
		info->dep_info.curr_nfc_dep_pni =
				ST21NFCA_NFC_DEP_PFB_PNI(dep_req->pfb);
		break;
	case ST21NFCA_NFC_DEP_PFB_ACK_NACK_PDU:
		pr_err("Received a ACK/NACK PDU\n");
		break;
	case ST21NFCA_NFC_DEP_PFB_SUPERVISOR_PDU:
		pr_err("Received a SUPERVISOR PDU\n");
		break;
	}

	skb_pull(skb, size);

	return nfc_tm_data_received(hdev->ndev, skb);
exit:
	return r;
}

int st21nfca_tm_event_send_data(struct nfc_hci_dev *hdev, struct sk_buff *skb,
				u8 gate)
{
	u8 cmd0, cmd1;
	int r;

	cmd0 = skb->data[1];
	switch (cmd0) {
	case ST21NFCA_NFCIP1_REQ:
		cmd1 = skb->data[2];
		switch (cmd1) {
		case ST21NFCA_NFCIP1_ATR_REQ:
			r = st21nfca_tm_recv_atr_req(hdev, skb);
			break;
		case ST21NFCA_NFCIP1_PSL_REQ:
			r = st21nfca_tm_recv_psl_req(hdev, skb);
			break;
		case ST21NFCA_NFCIP1_DEP_REQ:
			r = st21nfca_tm_recv_dep_req(hdev, skb);
			break;
		default:
			return 1;
		}
	default:
		return 1;
	}
	return r;
}
EXPORT_SYMBOL(st21nfca_tm_event_send_data);

static void st21nfca_im_send_psl_req(struct nfc_hci_dev *hdev, u8 did, u8 bsi,
				     u8 bri, u8 lri)
{
	struct sk_buff *skb;
	struct st21nfca_psl_req *psl_req;
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);

	skb =
	    alloc_skb(sizeof(struct st21nfca_psl_req) + 1, GFP_KERNEL);
	if (!skb)
		return;
	skb_reserve(skb, 1);

	skb_put(skb, sizeof(struct st21nfca_psl_req));
	psl_req = (struct st21nfca_psl_req *) skb->data;

	psl_req->length = sizeof(struct st21nfca_psl_req);
	psl_req->cmd0 = ST21NFCA_NFCIP1_REQ;
	psl_req->cmd1 = ST21NFCA_NFCIP1_PSL_REQ;
	psl_req->did = did;
	psl_req->brs = (0x30 & bsi << 4) | (bri & 0x03);
	psl_req->fsl = lri;

	*skb_push(skb, 1) = info->dep_info.to | 0x10;

	st21nfca_im_send_pdu(info, skb);

	kfree_skb(skb);
}

#define ST21NFCA_CB_TYPE_READER_F 1
static void st21nfca_im_recv_atr_res_cb(void *context, struct sk_buff *skb,
					int err)
{
	struct st21nfca_hci_info *info = context;
	struct st21nfca_atr_res *atr_res;
	int r;

	if (err != 0)
		return;

	if (IS_ERR(skb))
		return;

	switch (info->async_cb_type) {
	case ST21NFCA_CB_TYPE_READER_F:
		skb_trim(skb, skb->len - 1);
		atr_res = (struct st21nfca_atr_res *)skb->data;
		r = nfc_set_remote_general_bytes(info->hdev->ndev,
				atr_res->gbi,
				skb->len - sizeof(struct st21nfca_atr_res));
		if (r < 0)
			return;

		if (atr_res->to >= 0x0e)
			info->dep_info.to = 0x0e;
		else
			info->dep_info.to = atr_res->to + 1;

		info->dep_info.to |= 0x10;

		r = nfc_dep_link_is_up(info->hdev->ndev, info->dep_info.idx,
					NFC_COMM_PASSIVE, NFC_RF_INITIATOR);
		if (r < 0)
			return;

		info->dep_info.curr_nfc_dep_pni = 0;
		if (ST21NFCA_PP2LRI(atr_res->ppi) != info->dep_info.lri)
			st21nfca_im_send_psl_req(info->hdev, atr_res->did,
						atr_res->bsi, atr_res->bri,
						ST21NFCA_PP2LRI(atr_res->ppi));
		break;
	default:
		kfree_skb(skb);
		break;
	}
}

int st21nfca_im_send_atr_req(struct nfc_hci_dev *hdev, u8 *gb, size_t gb_len)
{
	struct sk_buff *skb;
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);
	struct st21nfca_atr_req *atr_req;
	struct nfc_target *target;
	uint size;

	info->dep_info.to = ST21NFCA_DEFAULT_TIMEOUT;
	size = ST21NFCA_ATR_REQ_MIN_SIZE + gb_len;
	if (size > ST21NFCA_ATR_REQ_MAX_SIZE) {
		PROTOCOL_ERR("14.6.1.1");
		return -EINVAL;
	}

	skb =
	    alloc_skb(sizeof(struct st21nfca_atr_req) + gb_len + 1, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	skb_reserve(skb, 1);

	skb_put(skb, sizeof(struct st21nfca_atr_req));

	atr_req = (struct st21nfca_atr_req *)skb->data;
	memset(atr_req, 0, sizeof(struct st21nfca_atr_req));

	atr_req->cmd0 = ST21NFCA_NFCIP1_REQ;
	atr_req->cmd1 = ST21NFCA_NFCIP1_ATR_REQ;
	memset(atr_req->nfcid3, 0, NFC_NFCID3_MAXSIZE);
	target = hdev->ndev->targets;

	if (target->sensf_res_len > 0)
		memcpy(atr_req->nfcid3, target->sensf_res,
				target->sensf_res_len);
	else
		get_random_bytes(atr_req->nfcid3, NFC_NFCID3_MAXSIZE);

	atr_req->did = 0x0;

	atr_req->bsi = 0x00;
	atr_req->bri = 0x00;
	atr_req->ppi = ST21NFCA_LR_BITS_PAYLOAD_SIZE_254B;
	if (gb_len) {
		atr_req->ppi |= ST21NFCA_GB_BIT;
		memcpy(skb_put(skb, gb_len), gb, gb_len);
	}
	atr_req->length = sizeof(struct st21nfca_atr_req) + hdev->gb_len;

	*skb_push(skb, 1) = info->dep_info.to | 0x10; /* timeout */

	info->async_cb_type = ST21NFCA_CB_TYPE_READER_F;
	info->async_cb_context = info;
	info->async_cb = st21nfca_im_recv_atr_res_cb;
	info->dep_info.bri = atr_req->bri;
	info->dep_info.bsi = atr_req->bsi;
	info->dep_info.lri = ST21NFCA_PP2LRI(atr_req->ppi);

	return nfc_hci_send_cmd_async(hdev, ST21NFCA_RF_READER_F_GATE,
				ST21NFCA_WR_XCHG_DATA, skb->data,
				skb->len, info->async_cb, info);
}
EXPORT_SYMBOL(st21nfca_im_send_atr_req);

static void st21nfca_im_recv_dep_res_cb(void *context, struct sk_buff *skb,
					int err)
{
	struct st21nfca_hci_info *info = context;
	struct st21nfca_dep_req_res *dep_res;

	int size;

	if (err != 0)
		return;

	if (IS_ERR(skb))
		return;

	switch (info->async_cb_type) {
	case ST21NFCA_CB_TYPE_READER_F:
		dep_res = (struct st21nfca_dep_req_res *)skb->data;

		size = 3;
		if (skb->len < size)
			goto exit;

		if (ST21NFCA_NFC_DEP_DID_BIT_SET(dep_res->pfb))
			size++;
		if (ST21NFCA_NFC_DEP_NAD_BIT_SET(dep_res->pfb))
			size++;

		if (skb->len < size)
			goto exit;

		skb_trim(skb, skb->len - 1);

		/* Receiving DEP_REQ - Decoding */
		switch (ST21NFCA_NFC_DEP_PFB_TYPE(dep_res->pfb)) {
		case ST21NFCA_NFC_DEP_PFB_ACK_NACK_PDU:
			pr_err("Received a ACK/NACK PDU\n");
		case ST21NFCA_NFC_DEP_PFB_I_PDU:
			info->dep_info.curr_nfc_dep_pni =
			    ST21NFCA_NFC_DEP_PFB_PNI(dep_res->pfb + 1);
			size++;
			skb_pull(skb, size);
			nfc_tm_data_received(info->hdev->ndev, skb);
			break;
		case ST21NFCA_NFC_DEP_PFB_SUPERVISOR_PDU:
			pr_err("Received a SUPERVISOR PDU\n");
			skb_pull(skb, size);
			*skb_push(skb, 1) = ST21NFCA_NFCIP1_DEP_REQ;
			*skb_push(skb, 1) = ST21NFCA_NFCIP1_REQ;
			*skb_push(skb, 1) = skb->len;
			*skb_push(skb, 1) = info->dep_info.to | 0x10;

			st21nfca_im_send_pdu(info, skb);
			break;
		}

		return;
	default:
		break;
	}

exit:
	kfree_skb(skb);
}

int st21nfca_im_send_dep_req(struct nfc_hci_dev *hdev, struct sk_buff *skb)
{
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);

	info->async_cb_type = ST21NFCA_CB_TYPE_READER_F;
	info->async_cb_context = info;
	info->async_cb = st21nfca_im_recv_dep_res_cb;

	*skb_push(skb, 1) = info->dep_info.curr_nfc_dep_pni;
	*skb_push(skb, 1) = ST21NFCA_NFCIP1_DEP_REQ;
	*skb_push(skb, 1) = ST21NFCA_NFCIP1_REQ;
	*skb_push(skb, 1) = skb->len;

	*skb_push(skb, 1) = info->dep_info.to | 0x10;

	return nfc_hci_send_cmd_async(hdev, ST21NFCA_RF_READER_F_GATE,
				      ST21NFCA_WR_XCHG_DATA,
				      skb->data, skb->len,
				      info->async_cb, info);
}
EXPORT_SYMBOL(st21nfca_im_send_dep_req);

void st21nfca_dep_init(struct nfc_hci_dev *hdev)
{
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);

	INIT_WORK(&info->dep_info.tx_work, st21nfca_tx_work);
	info->dep_info.curr_nfc_dep_pni = 0;
	info->dep_info.idx = 0;
	info->dep_info.to = ST21NFCA_DEFAULT_TIMEOUT;
}
EXPORT_SYMBOL(st21nfca_dep_init);

void st21nfca_dep_deinit(struct nfc_hci_dev *hdev)
{
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);

	cancel_work_sync(&info->dep_info.tx_work);
}
EXPORT_SYMBOL(st21nfca_dep_deinit);
