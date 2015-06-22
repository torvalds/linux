/*
 * NCI based Driver for STMicroelectronics NFC Chip
 *
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

#include <linux/module.h>
#include <linux/nfc.h>
#include <linux/delay.h>
#include <net/nfc/nci.h>
#include <net/nfc/nci_core.h>

#include "st21nfcb.h"
#include "st21nfcb_se.h"

struct st21nfcb_pipe_info {
	u8 pipe_state;
	u8 src_host_id;
	u8 src_gate_id;
	u8 dst_host_id;
	u8 dst_gate_id;
} __packed;

/* Hosts */
#define ST21NFCB_HOST_CONTROLLER_ID     0x00
#define ST21NFCB_TERMINAL_HOST_ID       0x01
#define ST21NFCB_UICC_HOST_ID           0x02
#define ST21NFCB_ESE_HOST_ID            0xc0

/* Gates */
#define ST21NFCB_DEVICE_MGNT_GATE       0x01
#define ST21NFCB_APDU_READER_GATE       0xf0
#define ST21NFCB_CONNECTIVITY_GATE      0x41

/* Pipes */
#define ST21NFCB_DEVICE_MGNT_PIPE               0x02

/* Connectivity pipe only */
#define ST21NFCB_SE_COUNT_PIPE_UICC             0x01
/* Connectivity + APDU Reader pipe */
#define ST21NFCB_SE_COUNT_PIPE_EMBEDDED         0x02

#define ST21NFCB_SE_TO_HOT_PLUG			1000 /* msecs */
#define ST21NFCB_SE_TO_PIPES			2000

#define ST21NFCB_EVT_HOT_PLUG_IS_INHIBITED(x)   (x->data[0] & 0x80)

#define NCI_HCI_APDU_PARAM_ATR                     0x01
#define NCI_HCI_ADMIN_PARAM_SESSION_IDENTITY       0x01
#define NCI_HCI_ADMIN_PARAM_WHITELIST              0x03
#define NCI_HCI_ADMIN_PARAM_HOST_LIST              0x04

#define ST21NFCB_EVT_SE_HARD_RESET		0x20
#define ST21NFCB_EVT_TRANSMIT_DATA		0x10
#define ST21NFCB_EVT_WTX_REQUEST		0x11
#define ST21NFCB_EVT_SE_SOFT_RESET		0x11
#define ST21NFCB_EVT_SE_END_OF_APDU_TRANSFER	0x21
#define ST21NFCB_EVT_HOT_PLUG			0x03

#define ST21NFCB_SE_MODE_OFF                    0x00
#define ST21NFCB_SE_MODE_ON                     0x01

#define ST21NFCB_EVT_CONNECTIVITY       0x10
#define ST21NFCB_EVT_TRANSACTION        0x12

#define ST21NFCB_DM_GETINFO             0x13
#define ST21NFCB_DM_GETINFO_PIPE_LIST   0x02
#define ST21NFCB_DM_GETINFO_PIPE_INFO   0x01
#define ST21NFCB_DM_PIPE_CREATED        0x02
#define ST21NFCB_DM_PIPE_OPEN           0x04
#define ST21NFCB_DM_RF_ACTIVE           0x80
#define ST21NFCB_DM_DISCONNECT          0x30

#define ST21NFCB_DM_IS_PIPE_OPEN(p) \
	((p & 0x0f) == (ST21NFCB_DM_PIPE_CREATED | ST21NFCB_DM_PIPE_OPEN))

#define ST21NFCB_ATR_DEFAULT_BWI        0x04

/*
 * WT = 2^BWI/10[s], convert into msecs and add a secure
 * room by increasing by 2 this timeout
 */
#define ST21NFCB_BWI_TO_TIMEOUT(x)      ((1 << x) * 200)
#define ST21NFCB_ATR_GET_Y_FROM_TD(x)   (x >> 4)

/* If TA is present bit 0 is set */
#define ST21NFCB_ATR_TA_PRESENT(x) (x & 0x01)
/* If TB is present bit 1 is set */
#define ST21NFCB_ATR_TB_PRESENT(x) (x & 0x02)

#define ST21NFCB_NUM_DEVICES           256

static DECLARE_BITMAP(dev_mask, ST21NFCB_NUM_DEVICES);

/* Here are the mandatory pipe for st21nfcb */
static struct nci_hci_gate st21nfcb_gates[] = {
	{NCI_HCI_ADMIN_GATE, NCI_HCI_ADMIN_PIPE,
					ST21NFCB_HOST_CONTROLLER_ID},
	{NCI_HCI_LINK_MGMT_GATE, NCI_HCI_LINK_MGMT_PIPE,
					ST21NFCB_HOST_CONTROLLER_ID},
	{ST21NFCB_DEVICE_MGNT_GATE, ST21NFCB_DEVICE_MGNT_PIPE,
					ST21NFCB_HOST_CONTROLLER_ID},

	/* Secure element pipes are created by secure element host */
	{ST21NFCB_CONNECTIVITY_GATE, NCI_HCI_DO_NOT_OPEN_PIPE,
					ST21NFCB_HOST_CONTROLLER_ID},
	{ST21NFCB_APDU_READER_GATE, NCI_HCI_DO_NOT_OPEN_PIPE,
					ST21NFCB_HOST_CONTROLLER_ID},
};

static u8 st21nfcb_se_get_bwi(struct nci_dev *ndev)
{
	int i;
	u8 td;
	struct st21nfcb_nci_info *info = nci_get_drvdata(ndev);

	/* Bits 8 to 5 of the first TB for T=1 encode BWI from zero to nine */
	for (i = 1; i < ST21NFCB_ESE_MAX_LENGTH; i++) {
		td = ST21NFCB_ATR_GET_Y_FROM_TD(info->se_info.atr[i]);
		if (ST21NFCB_ATR_TA_PRESENT(td))
			i++;
		if (ST21NFCB_ATR_TB_PRESENT(td)) {
			i++;
			return info->se_info.atr[i] >> 4;
		}
	}
	return ST21NFCB_ATR_DEFAULT_BWI;
}

static void st21nfcb_se_get_atr(struct nci_dev *ndev)
{
	struct st21nfcb_nci_info *info = nci_get_drvdata(ndev);
	int r;
	struct sk_buff *skb;

	r = nci_hci_get_param(ndev, ST21NFCB_APDU_READER_GATE,
				NCI_HCI_APDU_PARAM_ATR, &skb);
	if (r < 0)
		return;

	if (skb->len <= ST21NFCB_ESE_MAX_LENGTH) {
		memcpy(info->se_info.atr, skb->data, skb->len);

		info->se_info.wt_timeout =
			ST21NFCB_BWI_TO_TIMEOUT(st21nfcb_se_get_bwi(ndev));
	}
	kfree_skb(skb);
}

int st21nfcb_hci_load_session(struct nci_dev *ndev)
{
	int i, j, r;
	struct sk_buff *skb_pipe_list, *skb_pipe_info;
	struct st21nfcb_pipe_info *dm_pipe_info;
	u8 pipe_list[] = { ST21NFCB_DM_GETINFO_PIPE_LIST,
			ST21NFCB_TERMINAL_HOST_ID};
	u8 pipe_info[] = { ST21NFCB_DM_GETINFO_PIPE_INFO,
			ST21NFCB_TERMINAL_HOST_ID, 0};

	/* On ST21NFCB device pipes number are dynamics
	 * If pipes are already created, hci_dev_up will fail.
	 * Doing a clear all pipe is a bad idea because:
	 * - It does useless EEPROM cycling
	 * - It might cause issue for secure elements support
	 * (such as removing connectivity or APDU reader pipe)
	 * A better approach on ST21NFCB is to:
	 * - get a pipe list for each host.
	 * (eg: ST21NFCB_HOST_CONTROLLER_ID for now).
	 * (TODO Later on UICC HOST and eSE HOST)
	 * - get pipe information
	 * - match retrieved pipe list in st21nfcb_gates
	 * ST21NFCB_DEVICE_MGNT_GATE is a proprietary gate
	 * with ST21NFCB_DEVICE_MGNT_PIPE.
	 * Pipe can be closed and need to be open.
	 */
	r = nci_hci_connect_gate(ndev, ST21NFCB_HOST_CONTROLLER_ID,
				ST21NFCB_DEVICE_MGNT_GATE,
				ST21NFCB_DEVICE_MGNT_PIPE);
	if (r < 0)
		goto free_info;

	/* Get pipe list */
	r = nci_hci_send_cmd(ndev, ST21NFCB_DEVICE_MGNT_GATE,
			ST21NFCB_DM_GETINFO, pipe_list, sizeof(pipe_list),
			&skb_pipe_list);
	if (r < 0)
		goto free_info;

	/* Complete the existing gate_pipe table */
	for (i = 0; i < skb_pipe_list->len; i++) {
		pipe_info[2] = skb_pipe_list->data[i];
		r = nci_hci_send_cmd(ndev, ST21NFCB_DEVICE_MGNT_GATE,
					ST21NFCB_DM_GETINFO, pipe_info,
					sizeof(pipe_info), &skb_pipe_info);

		if (r)
			continue;

		/*
		 * Match pipe ID and gate ID
		 * Output format from ST21NFC_DM_GETINFO is:
		 * - pipe state (1byte)
		 * - source hid (1byte)
		 * - source gid (1byte)
		 * - destination hid (1byte)
		 * - destination gid (1byte)
		 */
		dm_pipe_info = (struct st21nfcb_pipe_info *)skb_pipe_info->data;
		if (dm_pipe_info->dst_gate_id == ST21NFCB_APDU_READER_GATE &&
		    dm_pipe_info->src_host_id != ST21NFCB_ESE_HOST_ID) {
			pr_err("Unexpected apdu_reader pipe on host %x\n",
			       dm_pipe_info->src_host_id);
			continue;
		}

		for (j = 0; (j < ARRAY_SIZE(st21nfcb_gates)) &&
		     (st21nfcb_gates[j].gate != dm_pipe_info->dst_gate_id); j++)
			;

		if (j < ARRAY_SIZE(st21nfcb_gates) &&
		    st21nfcb_gates[j].gate == dm_pipe_info->dst_gate_id &&
		    ST21NFCB_DM_IS_PIPE_OPEN(dm_pipe_info->pipe_state)) {
			st21nfcb_gates[j].pipe = pipe_info[2];

			ndev->hci_dev->gate2pipe[st21nfcb_gates[j].gate] =
						st21nfcb_gates[j].pipe;
			ndev->hci_dev->pipes[st21nfcb_gates[j].pipe].gate =
						st21nfcb_gates[j].gate;
			ndev->hci_dev->pipes[st21nfcb_gates[j].pipe].host =
						dm_pipe_info->src_host_id;
		}
	}

	memcpy(ndev->hci_dev->init_data.gates, st21nfcb_gates,
	       sizeof(st21nfcb_gates));

free_info:
	kfree_skb(skb_pipe_info);
	kfree_skb(skb_pipe_list);
	return r;
}
EXPORT_SYMBOL_GPL(st21nfcb_hci_load_session);

static void st21nfcb_hci_admin_event_received(struct nci_dev *ndev,
					      u8 event, struct sk_buff *skb)
{
	struct st21nfcb_nci_info *info = nci_get_drvdata(ndev);

	switch (event) {
	case ST21NFCB_EVT_HOT_PLUG:
		if (info->se_info.se_active) {
			if (!ST21NFCB_EVT_HOT_PLUG_IS_INHIBITED(skb)) {
				del_timer_sync(&info->se_info.se_active_timer);
				info->se_info.se_active = false;
				complete(&info->se_info.req_completion);
			} else {
				mod_timer(&info->se_info.se_active_timer,
				      jiffies +
				      msecs_to_jiffies(ST21NFCB_SE_TO_PIPES));
			}
		}
	break;
	}
}

static int st21nfcb_hci_apdu_reader_event_received(struct nci_dev *ndev,
						   u8 event,
						   struct sk_buff *skb)
{
	int r = 0;
	struct st21nfcb_nci_info *info = nci_get_drvdata(ndev);

	pr_debug("apdu reader gate event: %x\n", event);

	switch (event) {
	case ST21NFCB_EVT_TRANSMIT_DATA:
		del_timer_sync(&info->se_info.bwi_timer);
		info->se_info.bwi_active = false;
		info->se_info.cb(info->se_info.cb_context,
				 skb->data, skb->len, 0);
	break;
	case ST21NFCB_EVT_WTX_REQUEST:
		mod_timer(&info->se_info.bwi_timer, jiffies +
			  msecs_to_jiffies(info->se_info.wt_timeout));
	break;
	}

	kfree_skb(skb);
	return r;
}

/*
 * Returns:
 * <= 0: driver handled the event, skb consumed
 *    1: driver does not handle the event, please do standard processing
 */
static int st21nfcb_hci_connectivity_event_received(struct nci_dev *ndev,
						u8 host, u8 event,
						struct sk_buff *skb)
{
	int r = 0;
	struct device *dev = &ndev->nfc_dev->dev;
	struct nfc_evt_transaction *transaction;

	pr_debug("connectivity gate event: %x\n", event);

	switch (event) {
	case ST21NFCB_EVT_CONNECTIVITY:

	break;
	case ST21NFCB_EVT_TRANSACTION:
		/* According to specification etsi 102 622
		 * 11.2.2.4 EVT_TRANSACTION Table 52
		 * Description  Tag     Length
		 * AID          81      5 to 16
		 * PARAMETERS   82      0 to 255
		 */
		if (skb->len < NFC_MIN_AID_LENGTH + 2 &&
		    skb->data[0] != NFC_EVT_TRANSACTION_AID_TAG)
			return -EPROTO;

		transaction = (struct nfc_evt_transaction *)devm_kzalloc(dev,
					    skb->len - 2, GFP_KERNEL);

		transaction->aid_len = skb->data[1];
		memcpy(transaction->aid, &skb->data[2], transaction->aid_len);

		/* Check next byte is PARAMETERS tag (82) */
		if (skb->data[transaction->aid_len + 2] !=
		    NFC_EVT_TRANSACTION_PARAMS_TAG)
			return -EPROTO;

		transaction->params_len = skb->data[transaction->aid_len + 3];
		memcpy(transaction->params, skb->data +
		       transaction->aid_len + 4, transaction->params_len);

		r = nfc_se_transaction(ndev->nfc_dev, host, transaction);
		break;
	default:
		return 1;
	}
	kfree_skb(skb);
	return r;
}

void st21nfcb_hci_event_received(struct nci_dev *ndev, u8 pipe,
				 u8 event, struct sk_buff *skb)
{
	u8 gate = ndev->hci_dev->pipes[pipe].gate;
	u8 host = ndev->hci_dev->pipes[pipe].host;

	switch (gate) {
	case NCI_HCI_ADMIN_GATE:
		st21nfcb_hci_admin_event_received(ndev, event, skb);
	break;
	case ST21NFCB_APDU_READER_GATE:
		st21nfcb_hci_apdu_reader_event_received(ndev, event, skb);
	break;
	case ST21NFCB_CONNECTIVITY_GATE:
		st21nfcb_hci_connectivity_event_received(ndev, host, event,
							 skb);
	break;
	}
}
EXPORT_SYMBOL_GPL(st21nfcb_hci_event_received);


void st21nfcb_hci_cmd_received(struct nci_dev *ndev, u8 pipe, u8 cmd,
			       struct sk_buff *skb)
{
	struct st21nfcb_nci_info *info = nci_get_drvdata(ndev);
	u8 gate = ndev->hci_dev->pipes[pipe].gate;

	pr_debug("cmd: %x\n", cmd);

	switch (cmd) {
	case NCI_HCI_ANY_OPEN_PIPE:
		if (gate != ST21NFCB_APDU_READER_GATE &&
		    ndev->hci_dev->pipes[pipe].host != ST21NFCB_UICC_HOST_ID)
			ndev->hci_dev->count_pipes++;

		if (ndev->hci_dev->count_pipes ==
		    ndev->hci_dev->expected_pipes) {
			del_timer_sync(&info->se_info.se_active_timer);
			info->se_info.se_active = false;
			ndev->hci_dev->count_pipes = 0;
			complete(&info->se_info.req_completion);
		}
	break;
	}
}
EXPORT_SYMBOL_GPL(st21nfcb_hci_cmd_received);

/*
 * Remarks: On some early st21nfcb firmware, nci_nfcee_mode_set(0)
 * is rejected
 */
static int st21nfcb_nci_control_se(struct nci_dev *ndev, u8 se_idx,
				   u8 state)
{
	struct st21nfcb_nci_info *info = nci_get_drvdata(ndev);
	int r;
	struct sk_buff *sk_host_list;
	u8 host_id;

	switch (se_idx) {
	case ST21NFCB_UICC_HOST_ID:
		ndev->hci_dev->count_pipes = 0;
		ndev->hci_dev->expected_pipes = ST21NFCB_SE_COUNT_PIPE_UICC;
		break;
	case ST21NFCB_ESE_HOST_ID:
		ndev->hci_dev->count_pipes = 0;
		ndev->hci_dev->expected_pipes = ST21NFCB_SE_COUNT_PIPE_EMBEDDED;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Wait for an EVT_HOT_PLUG in order to
	 * retrieve a relevant host list.
	 */
	reinit_completion(&info->se_info.req_completion);
	r = nci_nfcee_mode_set(ndev, se_idx, NCI_NFCEE_ENABLE);
	if (r != NCI_STATUS_OK)
		return r;

	mod_timer(&info->se_info.se_active_timer, jiffies +
		msecs_to_jiffies(ST21NFCB_SE_TO_HOT_PLUG));
	info->se_info.se_active = true;

	/* Ignore return value and check in any case the host_list */
	wait_for_completion_interruptible(&info->se_info.req_completion);

	/* There might be some "collision" after receiving a HOT_PLUG event
	 * This may cause the CLF to not answer to the next hci command.
	 * There is no possible synchronization to prevent this.
	 * Adding a small delay is the only way to solve the issue.
	 */
	usleep_range(3000, 5000);

	r = nci_hci_get_param(ndev, NCI_HCI_ADMIN_GATE,
			NCI_HCI_ADMIN_PARAM_HOST_LIST, &sk_host_list);
	if (r != NCI_HCI_ANY_OK)
		return r;

	host_id = sk_host_list->data[sk_host_list->len - 1];
	kfree_skb(sk_host_list);
	if (state == ST21NFCB_SE_MODE_ON && host_id == se_idx)
		return se_idx;
	else if (state == ST21NFCB_SE_MODE_OFF && host_id != se_idx)
		return se_idx;

	return -1;
}

int st21nfcb_nci_disable_se(struct nci_dev *ndev, u32 se_idx)
{
	int r;

	pr_debug("st21nfcb_nci_disable_se\n");

	if (se_idx == NFC_SE_EMBEDDED) {
		r = nci_hci_send_event(ndev, ST21NFCB_APDU_READER_GATE,
				ST21NFCB_EVT_SE_END_OF_APDU_TRANSFER, NULL, 0);
		if (r < 0)
			return r;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(st21nfcb_nci_disable_se);

int st21nfcb_nci_enable_se(struct nci_dev *ndev, u32 se_idx)
{
	int r;

	pr_debug("st21nfcb_nci_enable_se\n");

	if (se_idx == ST21NFCB_HCI_HOST_ID_ESE) {
		r = nci_hci_send_event(ndev, ST21NFCB_APDU_READER_GATE,
				ST21NFCB_EVT_SE_SOFT_RESET, NULL, 0);
		if (r < 0)
			return r;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(st21nfcb_nci_enable_se);

static int st21nfcb_hci_network_init(struct nci_dev *ndev)
{
	struct core_conn_create_dest_spec_params *dest_params;
	struct dest_spec_params spec_params;
	struct nci_conn_info    *conn_info;
	int r, dev_num;

	dest_params =
		kzalloc(sizeof(struct core_conn_create_dest_spec_params) +
			sizeof(struct dest_spec_params), GFP_KERNEL);
	if (dest_params == NULL) {
		r = -ENOMEM;
		goto exit;
	}

	dest_params->type = NCI_DESTINATION_SPECIFIC_PARAM_NFCEE_TYPE;
	dest_params->length = sizeof(struct dest_spec_params);
	spec_params.id = ndev->hci_dev->nfcee_id;
	spec_params.protocol = NCI_NFCEE_INTERFACE_HCI_ACCESS;
	memcpy(dest_params->value, &spec_params, sizeof(struct dest_spec_params));
	r = nci_core_conn_create(ndev, NCI_DESTINATION_NFCEE, 1,
				 sizeof(struct core_conn_create_dest_spec_params) +
				 sizeof(struct dest_spec_params),
				 dest_params);
	if (r != NCI_STATUS_OK)
		goto free_dest_params;

	conn_info = ndev->hci_dev->conn_info;
	if (!conn_info)
		goto free_dest_params;

	memcpy(ndev->hci_dev->init_data.gates, st21nfcb_gates,
	       sizeof(st21nfcb_gates));

	/*
	 * Session id must include the driver name + i2c bus addr
	 * persistent info to discriminate 2 identical chips
	 */
	dev_num = find_first_zero_bit(dev_mask, ST21NFCB_NUM_DEVICES);
	if (dev_num >= ST21NFCB_NUM_DEVICES) {
		r = -ENODEV;
		goto free_dest_params;
	}

	scnprintf(ndev->hci_dev->init_data.session_id,
		  sizeof(ndev->hci_dev->init_data.session_id),
		  "%s%2x", "ST21BH", dev_num);

	r = nci_hci_dev_session_init(ndev);
	if (r != NCI_HCI_ANY_OK)
		goto free_dest_params;

	r = nci_nfcee_mode_set(ndev, ndev->hci_dev->conn_info->id,
			       NCI_NFCEE_ENABLE);
	if (r != NCI_STATUS_OK)
		goto free_dest_params;

free_dest_params:
	kfree(dest_params);

exit:
	return r;
}

int st21nfcb_nci_discover_se(struct nci_dev *ndev)
{
	u8 param[2];
	int r;
	int se_count = 0;

	pr_debug("st21nfcb_nci_discover_se\n");

	r = st21nfcb_hci_network_init(ndev);
	if (r != 0)
		return r;

	param[0] = ST21NFCB_UICC_HOST_ID;
	param[1] = ST21NFCB_HCI_HOST_ID_ESE;
	r = nci_hci_set_param(ndev, NCI_HCI_ADMIN_GATE,
				NCI_HCI_ADMIN_PARAM_WHITELIST,
				param, sizeof(param));
	if (r != NCI_HCI_ANY_OK)
		return r;

	r = st21nfcb_nci_control_se(ndev, ST21NFCB_UICC_HOST_ID,
				ST21NFCB_SE_MODE_ON);
	if (r == ST21NFCB_UICC_HOST_ID) {
		nfc_add_se(ndev->nfc_dev, ST21NFCB_UICC_HOST_ID, NFC_SE_UICC);
		se_count++;
	}

	/* Try to enable eSE in order to check availability */
	r = st21nfcb_nci_control_se(ndev, ST21NFCB_HCI_HOST_ID_ESE,
				ST21NFCB_SE_MODE_ON);
	if (r == ST21NFCB_HCI_HOST_ID_ESE) {
		nfc_add_se(ndev->nfc_dev, ST21NFCB_HCI_HOST_ID_ESE,
			   NFC_SE_EMBEDDED);
		se_count++;
		st21nfcb_se_get_atr(ndev);
	}

	return !se_count;
}
EXPORT_SYMBOL_GPL(st21nfcb_nci_discover_se);

int st21nfcb_nci_se_io(struct nci_dev *ndev, u32 se_idx,
		       u8 *apdu, size_t apdu_length,
		       se_io_cb_t cb, void *cb_context)
{
	struct st21nfcb_nci_info *info = nci_get_drvdata(ndev);

	pr_debug("\n");

	switch (se_idx) {
	case ST21NFCB_HCI_HOST_ID_ESE:
		info->se_info.cb = cb;
		info->se_info.cb_context = cb_context;
		mod_timer(&info->se_info.bwi_timer, jiffies +
			  msecs_to_jiffies(info->se_info.wt_timeout));
		info->se_info.bwi_active = true;
		return nci_hci_send_event(ndev, ST21NFCB_APDU_READER_GATE,
					ST21NFCB_EVT_TRANSMIT_DATA, apdu,
					apdu_length);
	default:
		return -ENODEV;
	}
}
EXPORT_SYMBOL(st21nfcb_nci_se_io);

static void st21nfcb_se_wt_timeout(unsigned long data)
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
	struct st21nfcb_nci_info *info = (struct st21nfcb_nci_info *) data;

	pr_debug("\n");

	info->se_info.bwi_active = false;

	if (!info->se_info.xch_error) {
		info->se_info.xch_error = true;
		nci_hci_send_event(info->ndlc->ndev, ST21NFCB_APDU_READER_GATE,
				ST21NFCB_EVT_SE_SOFT_RESET, NULL, 0);
	} else {
		info->se_info.xch_error = false;
		nci_hci_send_event(info->ndlc->ndev, ST21NFCB_DEVICE_MGNT_GATE,
				ST21NFCB_EVT_SE_HARD_RESET, &param, 1);
	}
	info->se_info.cb(info->se_info.cb_context, NULL, 0, -ETIME);
}

static void st21nfcb_se_activation_timeout(unsigned long data)
{
	struct st21nfcb_nci_info *info = (struct st21nfcb_nci_info *) data;

	pr_debug("\n");

	info->se_info.se_active = false;

	complete(&info->se_info.req_completion);
}

int st21nfcb_se_init(struct nci_dev *ndev)
{
	struct st21nfcb_nci_info *info = nci_get_drvdata(ndev);

	init_completion(&info->se_info.req_completion);
	/* initialize timers */
	init_timer(&info->se_info.bwi_timer);
	info->se_info.bwi_timer.data = (unsigned long)info;
	info->se_info.bwi_timer.function = st21nfcb_se_wt_timeout;
	info->se_info.bwi_active = false;

	init_timer(&info->se_info.se_active_timer);
	info->se_info.se_active_timer.data = (unsigned long)info;
	info->se_info.se_active_timer.function =
			st21nfcb_se_activation_timeout;
	info->se_info.se_active = false;

	info->se_info.xch_error = false;

	info->se_info.wt_timeout =
		ST21NFCB_BWI_TO_TIMEOUT(ST21NFCB_ATR_DEFAULT_BWI);

	return 0;
}
EXPORT_SYMBOL(st21nfcb_se_init);

void st21nfcb_se_deinit(struct nci_dev *ndev)
{
	struct st21nfcb_nci_info *info = nci_get_drvdata(ndev);

	if (info->se_info.bwi_active)
		del_timer_sync(&info->se_info.bwi_timer);
	if (info->se_info.se_active)
		del_timer_sync(&info->se_info.se_active_timer);

	info->se_info.se_active = false;
	info->se_info.bwi_active = false;
}
EXPORT_SYMBOL(st21nfcb_se_deinit);

