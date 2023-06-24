// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014  STMicroelectronics SAS. All rights reserved.
 */

#include <net/nfc/hci.h>

#include "st21nfca.h"

#define ST21NFCA_EVT_UICC_ACTIVATE		0x10
#define ST21NFCA_EVT_UICC_DEACTIVATE		0x13
#define ST21NFCA_EVT_SE_HARD_RESET		0x20
#define ST21NFCA_EVT_SE_SOFT_RESET		0x11
#define ST21NFCA_EVT_SE_END_OF_APDU_TRANSFER	0x21
#define ST21NFCA_EVT_SE_ACTIVATE		0x22
#define ST21NFCA_EVT_SE_DEACTIVATE		0x23

#define ST21NFCA_EVT_TRANSMIT_DATA		0x10
#define ST21NFCA_EVT_WTX_REQUEST		0x11

#define ST21NFCA_EVT_CONNECTIVITY		0x10
#define ST21NFCA_EVT_TRANSACTION		0x12

#define ST21NFCA_SE_TO_HOT_PLUG			1000
/* Connectivity pipe only */
#define ST21NFCA_SE_COUNT_PIPE_UICC		0x01
/* Connectivity + APDU Reader pipe */
#define ST21NFCA_SE_COUNT_PIPE_EMBEDDED	0x02

#define ST21NFCA_SE_MODE_OFF			0x00
#define ST21NFCA_SE_MODE_ON				0x01

#define ST21NFCA_PARAM_ATR				0x01
#define ST21NFCA_ATR_DEFAULT_BWI		0x04

/*
 * WT = 2^BWI/10[s], convert into msecs and add a secure
 * room by increasing by 2 this timeout
 */
#define ST21NFCA_BWI_TO_TIMEOUT(x)		((1 << x) * 200)
#define ST21NFCA_ATR_GET_Y_FROM_TD(x)	(x >> 4)

/* If TA is present bit 0 is set */
#define ST21NFCA_ATR_TA_PRESENT(x) (x & 0x01)
/* If TB is present bit 1 is set */
#define ST21NFCA_ATR_TB_PRESENT(x) (x & 0x02)

static u8 st21nfca_se_get_bwi(struct nfc_hci_dev *hdev)
{
	int i;
	u8 td;
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);

	/* Bits 8 to 5 of the first TB for T=1 encode BWI from zero to nine */
	for (i = 1; i < ST21NFCA_ESE_MAX_LENGTH; i++) {
		td = ST21NFCA_ATR_GET_Y_FROM_TD(info->se_info.atr[i]);
		if (ST21NFCA_ATR_TA_PRESENT(td))
			i++;
		if (ST21NFCA_ATR_TB_PRESENT(td)) {
			i++;
			return info->se_info.atr[i] >> 4;
		}
	}
	return ST21NFCA_ATR_DEFAULT_BWI;
}

static void st21nfca_se_get_atr(struct nfc_hci_dev *hdev)
{
	int r;
	struct sk_buff *skb;
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);

	r = nfc_hci_get_param(hdev, ST21NFCA_APDU_READER_GATE,
			ST21NFCA_PARAM_ATR, &skb);
	if (r < 0)
		return;

	if (skb->len <= ST21NFCA_ESE_MAX_LENGTH) {
		memcpy(info->se_info.atr, skb->data, skb->len);
		info->se_info.wt_timeout =
			ST21NFCA_BWI_TO_TIMEOUT(st21nfca_se_get_bwi(hdev));
	}
	kfree_skb(skb);
}

static int st21nfca_hci_control_se(struct nfc_hci_dev *hdev, u32 se_idx,
				u8 state)
{
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);
	int r, i;
	struct sk_buff *sk_host_list;
	u8 se_event, host_id;

	switch (se_idx) {
	case NFC_HCI_UICC_HOST_ID:
		se_event = (state == ST21NFCA_SE_MODE_ON ?
					ST21NFCA_EVT_UICC_ACTIVATE :
					ST21NFCA_EVT_UICC_DEACTIVATE);

		info->se_info.count_pipes = 0;
		info->se_info.expected_pipes = ST21NFCA_SE_COUNT_PIPE_UICC;
		break;
	case ST21NFCA_ESE_HOST_ID:
		se_event = (state == ST21NFCA_SE_MODE_ON ?
					ST21NFCA_EVT_SE_ACTIVATE :
					ST21NFCA_EVT_SE_DEACTIVATE);

		info->se_info.count_pipes = 0;
		info->se_info.expected_pipes = ST21NFCA_SE_COUNT_PIPE_EMBEDDED;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Wait for an EVT_HOT_PLUG in order to
	 * retrieve a relevant host list.
	 */
	reinit_completion(&info->se_info.req_completion);
	r = nfc_hci_send_event(hdev, ST21NFCA_DEVICE_MGNT_GATE, se_event,
			       NULL, 0);
	if (r < 0)
		return r;

	mod_timer(&info->se_info.se_active_timer, jiffies +
		msecs_to_jiffies(ST21NFCA_SE_TO_HOT_PLUG));
	info->se_info.se_active = true;

	/* Ignore return value and check in any case the host_list */
	wait_for_completion_interruptible(&info->se_info.req_completion);

	r = nfc_hci_get_param(hdev, NFC_HCI_ADMIN_GATE,
			NFC_HCI_ADMIN_HOST_LIST,
			&sk_host_list);
	if (r < 0)
		return r;

	for (i = 0; i < sk_host_list->len &&
		sk_host_list->data[i] != se_idx; i++)
		;
	host_id = sk_host_list->data[i];
	kfree_skb(sk_host_list);

	if (state == ST21NFCA_SE_MODE_ON && host_id == se_idx)
		return se_idx;
	else if (state == ST21NFCA_SE_MODE_OFF && host_id != se_idx)
		return se_idx;

	return -1;
}

int st21nfca_hci_discover_se(struct nfc_hci_dev *hdev)
{
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);
	int se_count = 0;

	if (test_bit(ST21NFCA_FACTORY_MODE, &hdev->quirks))
		return 0;

	if (info->se_status->is_uicc_present) {
		nfc_add_se(hdev->ndev, NFC_HCI_UICC_HOST_ID, NFC_SE_UICC);
		se_count++;
	}

	if (info->se_status->is_ese_present) {
		nfc_add_se(hdev->ndev, ST21NFCA_ESE_HOST_ID, NFC_SE_EMBEDDED);
		se_count++;
	}

	return !se_count;
}
EXPORT_SYMBOL(st21nfca_hci_discover_se);

int st21nfca_hci_enable_se(struct nfc_hci_dev *hdev, u32 se_idx)
{
	int r;

	/*
	 * According to upper layer, se_idx == NFC_SE_UICC when
	 * info->se_status->is_uicc_enable is true should never happen.
	 * Same for eSE.
	 */
	r = st21nfca_hci_control_se(hdev, se_idx, ST21NFCA_SE_MODE_ON);
	if (r == ST21NFCA_ESE_HOST_ID) {
		st21nfca_se_get_atr(hdev);
		r = nfc_hci_send_event(hdev, ST21NFCA_APDU_READER_GATE,
				ST21NFCA_EVT_SE_SOFT_RESET, NULL, 0);
		if (r < 0)
			return r;
	} else if (r < 0) {
		/*
		 * The activation tentative failed, the secure element
		 * is not connected. Remove from the list.
		 */
		nfc_remove_se(hdev->ndev, se_idx);
		return r;
	}

	return 0;
}
EXPORT_SYMBOL(st21nfca_hci_enable_se);

int st21nfca_hci_disable_se(struct nfc_hci_dev *hdev, u32 se_idx)
{
	int r;

	/*
	 * According to upper layer, se_idx == NFC_SE_UICC when
	 * info->se_status->is_uicc_enable is true should never happen
	 * Same for eSE.
	 */
	r = st21nfca_hci_control_se(hdev, se_idx, ST21NFCA_SE_MODE_OFF);
	if (r < 0)
		return r;

	return 0;
}
EXPORT_SYMBOL(st21nfca_hci_disable_se);

int st21nfca_hci_se_io(struct nfc_hci_dev *hdev, u32 se_idx,
			u8 *apdu, size_t apdu_length,
			se_io_cb_t cb, void *cb_context)
{
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);

	pr_debug("se_io %x\n", se_idx);

	switch (se_idx) {
	case ST21NFCA_ESE_HOST_ID:
		info->se_info.cb = cb;
		info->se_info.cb_context = cb_context;
		mod_timer(&info->se_info.bwi_timer, jiffies +
			  msecs_to_jiffies(info->se_info.wt_timeout));
		info->se_info.bwi_active = true;
		return nfc_hci_send_event(hdev, ST21NFCA_APDU_READER_GATE,
					ST21NFCA_EVT_TRANSMIT_DATA,
					apdu, apdu_length);
	default:
		return -ENODEV;
	}
}
EXPORT_SYMBOL(st21nfca_hci_se_io);

static void st21nfca_se_wt_work(struct work_struct *work)
{
	/*
	 * No answer from the secure element
	 * within the defined timeout.
	 * Let's send a reset request as recovery procedure.
	 * According to the situation, we first try to send a software reset
	 * to the secure element. If the next command is still not
	 * answering in time, we send to the CLF a secure element hardware
	 * reset request.
	 */
	/* hardware reset managed through VCC_UICC_OUT power supply */
	u8 param = 0x01;
	struct st21nfca_hci_info *info = container_of(work,
						struct st21nfca_hci_info,
						se_info.timeout_work);

	info->se_info.bwi_active = false;

	if (!info->se_info.xch_error) {
		info->se_info.xch_error = true;
		nfc_hci_send_event(info->hdev, ST21NFCA_APDU_READER_GATE,
				ST21NFCA_EVT_SE_SOFT_RESET, NULL, 0);
	} else {
		info->se_info.xch_error = false;
		nfc_hci_send_event(info->hdev, ST21NFCA_DEVICE_MGNT_GATE,
				ST21NFCA_EVT_SE_HARD_RESET, &param, 1);
	}
	info->se_info.cb(info->se_info.cb_context, NULL, 0, -ETIME);
}

static void st21nfca_se_wt_timeout(struct timer_list *t)
{
	struct st21nfca_hci_info *info = from_timer(info, t, se_info.bwi_timer);

	schedule_work(&info->se_info.timeout_work);
}

static void st21nfca_se_activation_timeout(struct timer_list *t)
{
	struct st21nfca_hci_info *info = from_timer(info, t,
						    se_info.se_active_timer);

	info->se_info.se_active = false;

	complete(&info->se_info.req_completion);
}

/*
 * Returns:
 * <= 0: driver handled the event, skb consumed
 *    1: driver does not handle the event, please do standard processing
 */
int st21nfca_connectivity_event_received(struct nfc_hci_dev *hdev, u8 host,
				u8 event, struct sk_buff *skb)
{
	int r = 0;
	struct device *dev = &hdev->ndev->dev;
	struct nfc_evt_transaction *transaction;
	u32 aid_len;
	u8 params_len;

	pr_debug("connectivity gate event: %x\n", event);

	switch (event) {
	case ST21NFCA_EVT_CONNECTIVITY:
		r = nfc_se_connectivity(hdev->ndev, host);
	break;
	case ST21NFCA_EVT_TRANSACTION:
		/* According to specification etsi 102 622
		 * 11.2.2.4 EVT_TRANSACTION Table 52
		 * Description	Tag	Length
		 * AID		81	5 to 16
		 * PARAMETERS	82	0 to 255
		 *
		 * The key differences are aid storage length is variably sized
		 * in the packet, but fixed in nfc_evt_transaction, and that the aid_len
		 * is u8 in the packet, but u32 in the structure, and the tags in
		 * the packet are not included in nfc_evt_transaction.
		 *
		 * size in bytes: 1          1       5-16 1             1           0-255
		 * offset:        0          1       2    aid_len + 2   aid_len + 3 aid_len + 4
		 * member name:   aid_tag(M) aid_len aid  params_tag(M) params_len  params
		 * example:       0x81       5-16    X    0x82 0-255    X
		 */
		if (skb->len < 2 || skb->data[0] != NFC_EVT_TRANSACTION_AID_TAG)
			return -EPROTO;

		aid_len = skb->data[1];

		if (skb->len < aid_len + 4 || aid_len > sizeof(transaction->aid))
			return -EPROTO;

		params_len = skb->data[aid_len + 3];

		/* Verify PARAMETERS tag is (82), and final check that there is enough
		 * space in the packet to read everything.
		 */
		if ((skb->data[aid_len + 2] != NFC_EVT_TRANSACTION_PARAMS_TAG) ||
		    (skb->len < aid_len + 4 + params_len))
			return -EPROTO;

		transaction = devm_kzalloc(dev, sizeof(*transaction) + params_len, GFP_KERNEL);
		if (!transaction)
			return -ENOMEM;

		transaction->aid_len = aid_len;
		transaction->params_len = params_len;

		memcpy(transaction->aid, &skb->data[2], aid_len);
		memcpy(transaction->params, &skb->data[aid_len + 4], params_len);

		r = nfc_se_transaction(hdev->ndev, host, transaction);
	break;
	default:
		nfc_err(&hdev->ndev->dev, "Unexpected event on connectivity gate\n");
		return 1;
	}
	kfree_skb(skb);
	return r;
}
EXPORT_SYMBOL(st21nfca_connectivity_event_received);

int st21nfca_apdu_reader_event_received(struct nfc_hci_dev *hdev,
					u8 event, struct sk_buff *skb)
{
	int r = 0;
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);

	pr_debug("apdu reader gate event: %x\n", event);

	switch (event) {
	case ST21NFCA_EVT_TRANSMIT_DATA:
		del_timer_sync(&info->se_info.bwi_timer);
		cancel_work_sync(&info->se_info.timeout_work);
		info->se_info.bwi_active = false;
		r = nfc_hci_send_event(hdev, ST21NFCA_DEVICE_MGNT_GATE,
				ST21NFCA_EVT_SE_END_OF_APDU_TRANSFER, NULL, 0);
		if (r < 0)
			goto exit;

		info->se_info.cb(info->se_info.cb_context,
			skb->data, skb->len, 0);
		break;
	case ST21NFCA_EVT_WTX_REQUEST:
		mod_timer(&info->se_info.bwi_timer, jiffies +
				msecs_to_jiffies(info->se_info.wt_timeout));
		break;
	default:
		nfc_err(&hdev->ndev->dev, "Unexpected event on apdu reader gate\n");
		return 1;
	}

exit:
	kfree_skb(skb);
	return r;
}
EXPORT_SYMBOL(st21nfca_apdu_reader_event_received);

void st21nfca_se_init(struct nfc_hci_dev *hdev)
{
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);

	init_completion(&info->se_info.req_completion);
	INIT_WORK(&info->se_info.timeout_work, st21nfca_se_wt_work);
	/* initialize timers */
	timer_setup(&info->se_info.bwi_timer, st21nfca_se_wt_timeout, 0);
	info->se_info.bwi_active = false;

	timer_setup(&info->se_info.se_active_timer,
		    st21nfca_se_activation_timeout, 0);
	info->se_info.se_active = false;

	info->se_info.count_pipes = 0;
	info->se_info.expected_pipes = 0;

	info->se_info.xch_error = false;

	info->se_info.wt_timeout =
			ST21NFCA_BWI_TO_TIMEOUT(ST21NFCA_ATR_DEFAULT_BWI);
}
EXPORT_SYMBOL(st21nfca_se_init);

void st21nfca_se_deinit(struct nfc_hci_dev *hdev)
{
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);

	if (info->se_info.bwi_active)
		del_timer_sync(&info->se_info.bwi_timer);
	if (info->se_info.se_active)
		del_timer_sync(&info->se_info.se_active_timer);

	cancel_work_sync(&info->se_info.timeout_work);
	info->se_info.bwi_active = false;
	info->se_info.se_active = false;
}
EXPORT_SYMBOL(st21nfca_se_deinit);
