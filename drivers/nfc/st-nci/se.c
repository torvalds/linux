/*
 * Secure Element driver for STMicroelectronics NFC NCI chip
 *
 * Copyright (C) 2014-2015 STMicroelectronics SAS. All rights reserved.
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

#include "st-nci.h"

struct st_nci_pipe_info {
	u8 pipe_state;
	u8 src_host_id;
	u8 src_gate_id;
	u8 dst_host_id;
	u8 dst_gate_id;
} __packed;

/* Hosts */
#define ST_NCI_HOST_CONTROLLER_ID     0x00
#define ST_NCI_TERMINAL_HOST_ID       0x01
#define ST_NCI_UICC_HOST_ID           0x02
#define ST_NCI_ESE_HOST_ID            0xc0

/* Gates */
#define ST_NCI_APDU_READER_GATE       0xf0
#define ST_NCI_CONNECTIVITY_GATE      0x41

/* Pipes */
#define ST_NCI_DEVICE_MGNT_PIPE               0x02

/* Connectivity pipe only */
#define ST_NCI_SE_COUNT_PIPE_UICC             0x01
/* Connectivity + APDU Reader pipe */
#define ST_NCI_SE_COUNT_PIPE_EMBEDDED         0x02

#define ST_NCI_SE_TO_HOT_PLUG			1000 /* msecs */
#define ST_NCI_SE_TO_PIPES			2000

#define ST_NCI_EVT_HOT_PLUG_IS_INHIBITED(x)   (x->data[0] & 0x80)

#define NCI_HCI_APDU_PARAM_ATR                     0x01
#define NCI_HCI_ADMIN_PARAM_SESSION_IDENTITY       0x01
#define NCI_HCI_ADMIN_PARAM_WHITELIST              0x03
#define NCI_HCI_ADMIN_PARAM_HOST_LIST              0x04

#define ST_NCI_EVT_SE_HARD_RESET		0x20
#define ST_NCI_EVT_TRANSMIT_DATA		0x10
#define ST_NCI_EVT_WTX_REQUEST			0x11
#define ST_NCI_EVT_SE_SOFT_RESET		0x11
#define ST_NCI_EVT_SE_END_OF_APDU_TRANSFER	0x21
#define ST_NCI_EVT_HOT_PLUG			0x03

#define ST_NCI_SE_MODE_OFF                    0x00
#define ST_NCI_SE_MODE_ON                     0x01

#define ST_NCI_EVT_CONNECTIVITY       0x10
#define ST_NCI_EVT_TRANSACTION        0x12

#define ST_NCI_DM_GETINFO             0x13
#define ST_NCI_DM_GETINFO_PIPE_LIST   0x02
#define ST_NCI_DM_GETINFO_PIPE_INFO   0x01
#define ST_NCI_DM_PIPE_CREATED        0x02
#define ST_NCI_DM_PIPE_OPEN           0x04
#define ST_NCI_DM_RF_ACTIVE           0x80
#define ST_NCI_DM_DISCONNECT          0x30

#define ST_NCI_DM_IS_PIPE_OPEN(p) \
	((p & 0x0f) == (ST_NCI_DM_PIPE_CREATED | ST_NCI_DM_PIPE_OPEN))

#define ST_NCI_ATR_DEFAULT_BWI        0x04

/*
 * WT = 2^BWI/10[s], convert into msecs and add a secure
 * room by increasing by 2 this timeout
 */
#define ST_NCI_BWI_TO_TIMEOUT(x)      ((1 << x) * 200)
#define ST_NCI_ATR_GET_Y_FROM_TD(x)   (x >> 4)

/* If TA is present bit 0 is set */
#define ST_NCI_ATR_TA_PRESENT(x) (x & 0x01)
/* If TB is present bit 1 is set */
#define ST_NCI_ATR_TB_PRESENT(x) (x & 0x02)

#define ST_NCI_NUM_DEVICES           256

static DECLARE_BITMAP(dev_mask, ST_NCI_NUM_DEVICES);

/* Here are the mandatory pipe for st_nci */
static struct nci_hci_gate st_nci_gates[] = {
	{NCI_HCI_ADMIN_GATE, NCI_HCI_ADMIN_PIPE,
					ST_NCI_HOST_CONTROLLER_ID},
	{NCI_HCI_LINK_MGMT_GATE, NCI_HCI_LINK_MGMT_PIPE,
					ST_NCI_HOST_CONTROLLER_ID},
	{ST_NCI_DEVICE_MGNT_GATE, ST_NCI_DEVICE_MGNT_PIPE,
					ST_NCI_HOST_CONTROLLER_ID},

	{NCI_HCI_IDENTITY_MGMT_GATE, NCI_HCI_INVALID_PIPE,
					ST_NCI_HOST_CONTROLLER_ID},

	/* Secure element pipes are created by secure element host */
	{ST_NCI_CONNECTIVITY_GATE, NCI_HCI_DO_NOT_OPEN_PIPE,
					ST_NCI_HOST_CONTROLLER_ID},
	{ST_NCI_APDU_READER_GATE, NCI_HCI_DO_NOT_OPEN_PIPE,
					ST_NCI_HOST_CONTROLLER_ID},
};

static u8 st_nci_se_get_bwi(struct nci_dev *ndev)
{
	int i;
	u8 td;
	struct st_nci_info *info = nci_get_drvdata(ndev);

	/* Bits 8 to 5 of the first TB for T=1 encode BWI from zero to nine */
	for (i = 1; i < ST_NCI_ESE_MAX_LENGTH; i++) {
		td = ST_NCI_ATR_GET_Y_FROM_TD(info->se_info.atr[i]);
		if (ST_NCI_ATR_TA_PRESENT(td))
			i++;
		if (ST_NCI_ATR_TB_PRESENT(td)) {
			i++;
			return info->se_info.atr[i] >> 4;
		}
	}
	return ST_NCI_ATR_DEFAULT_BWI;
}

static void st_nci_se_get_atr(struct nci_dev *ndev)
{
	struct st_nci_info *info = nci_get_drvdata(ndev);
	int r;
	struct sk_buff *skb;

	r = nci_hci_get_param(ndev, ST_NCI_APDU_READER_GATE,
				NCI_HCI_APDU_PARAM_ATR, &skb);
	if (r < 0)
		return;

	if (skb->len <= ST_NCI_ESE_MAX_LENGTH) {
		memcpy(info->se_info.atr, skb->data, skb->len);

		info->se_info.wt_timeout =
			ST_NCI_BWI_TO_TIMEOUT(st_nci_se_get_bwi(ndev));
	}
	kfree_skb(skb);
}

int st_nci_hci_load_session(struct nci_dev *ndev)
{
	int i, j, r;
	struct sk_buff *skb_pipe_list, *skb_pipe_info;
	struct st_nci_pipe_info *dm_pipe_info;
	u8 pipe_list[] = { ST_NCI_DM_GETINFO_PIPE_LIST,
			ST_NCI_TERMINAL_HOST_ID};
	u8 pipe_info[] = { ST_NCI_DM_GETINFO_PIPE_INFO,
			ST_NCI_TERMINAL_HOST_ID, 0};

	/* On ST_NCI device pipes number are dynamics
	 * If pipes are already created, hci_dev_up will fail.
	 * Doing a clear all pipe is a bad idea because:
	 * - It does useless EEPROM cycling
	 * - It might cause issue for secure elements support
	 * (such as removing connectivity or APDU reader pipe)
	 * A better approach on ST_NCI is to:
	 * - get a pipe list for each host.
	 * (eg: ST_NCI_HOST_CONTROLLER_ID for now).
	 * (TODO Later on UICC HOST and eSE HOST)
	 * - get pipe information
	 * - match retrieved pipe list in st_nci_gates
	 * ST_NCI_DEVICE_MGNT_GATE is a proprietary gate
	 * with ST_NCI_DEVICE_MGNT_PIPE.
	 * Pipe can be closed and need to be open.
	 */
	r = nci_hci_connect_gate(ndev, ST_NCI_HOST_CONTROLLER_ID,
				ST_NCI_DEVICE_MGNT_GATE,
				ST_NCI_DEVICE_MGNT_PIPE);
	if (r < 0)
		return r;

	/* Get pipe list */
	r = nci_hci_send_cmd(ndev, ST_NCI_DEVICE_MGNT_GATE,
			ST_NCI_DM_GETINFO, pipe_list, sizeof(pipe_list),
			&skb_pipe_list);
	if (r < 0)
		return r;

	/* Complete the existing gate_pipe table */
	for (i = 0; i < skb_pipe_list->len; i++) {
		pipe_info[2] = skb_pipe_list->data[i];
		r = nci_hci_send_cmd(ndev, ST_NCI_DEVICE_MGNT_GATE,
					ST_NCI_DM_GETINFO, pipe_info,
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
		dm_pipe_info = (struct st_nci_pipe_info *)skb_pipe_info->data;
		if (dm_pipe_info->dst_gate_id == ST_NCI_APDU_READER_GATE &&
		    dm_pipe_info->src_host_id == ST_NCI_UICC_HOST_ID) {
			pr_err("Unexpected apdu_reader pipe on host %x\n",
			       dm_pipe_info->src_host_id);
			kfree_skb(skb_pipe_info);
			continue;
		}

		for (j = 3; (j < ARRAY_SIZE(st_nci_gates)) &&
		     (st_nci_gates[j].gate != dm_pipe_info->dst_gate_id); j++)
			;

		if (j < ARRAY_SIZE(st_nci_gates) &&
		    st_nci_gates[j].gate == dm_pipe_info->dst_gate_id &&
		    ST_NCI_DM_IS_PIPE_OPEN(dm_pipe_info->pipe_state)) {
			ndev->hci_dev->init_data.gates[j].pipe = pipe_info[2];

			ndev->hci_dev->gate2pipe[st_nci_gates[j].gate] =
						pipe_info[2];
			ndev->hci_dev->pipes[pipe_info[2]].gate =
						st_nci_gates[j].gate;
			ndev->hci_dev->pipes[pipe_info[2]].host =
						dm_pipe_info->src_host_id;
		}
		kfree_skb(skb_pipe_info);
	}

	/*
	 * 3 gates have a well known pipe ID. Only NCI_HCI_LINK_MGMT_GATE
	 * is not yet open at this stage.
	 */
	r = nci_hci_connect_gate(ndev, ST_NCI_HOST_CONTROLLER_ID,
				 NCI_HCI_LINK_MGMT_GATE,
				 NCI_HCI_LINK_MGMT_PIPE);

	kfree_skb(skb_pipe_list);
	return r;
}
EXPORT_SYMBOL_GPL(st_nci_hci_load_session);

static void st_nci_hci_admin_event_received(struct nci_dev *ndev,
					      u8 event, struct sk_buff *skb)
{
	struct st_nci_info *info = nci_get_drvdata(ndev);

	switch (event) {
	case ST_NCI_EVT_HOT_PLUG:
		if (info->se_info.se_active) {
			if (!ST_NCI_EVT_HOT_PLUG_IS_INHIBITED(skb)) {
				del_timer_sync(&info->se_info.se_active_timer);
				info->se_info.se_active = false;
				complete(&info->se_info.req_completion);
			} else {
				mod_timer(&info->se_info.se_active_timer,
				      jiffies +
				      msecs_to_jiffies(ST_NCI_SE_TO_PIPES));
			}
		}
	break;
	default:
		nfc_err(&ndev->nfc_dev->dev, "Unexpected event on admin gate\n");
	}
}

static int st_nci_hci_apdu_reader_event_received(struct nci_dev *ndev,
						   u8 event,
						   struct sk_buff *skb)
{
	int r = 0;
	struct st_nci_info *info = nci_get_drvdata(ndev);

	pr_debug("apdu reader gate event: %x\n", event);

	switch (event) {
	case ST_NCI_EVT_TRANSMIT_DATA:
		del_timer_sync(&info->se_info.bwi_timer);
		info->se_info.bwi_active = false;
		info->se_info.cb(info->se_info.cb_context,
				 skb->data, skb->len, 0);
	break;
	case ST_NCI_EVT_WTX_REQUEST:
		mod_timer(&info->se_info.bwi_timer, jiffies +
			  msecs_to_jiffies(info->se_info.wt_timeout));
	break;
	default:
		nfc_err(&ndev->nfc_dev->dev, "Unexpected event on apdu reader gate\n");
		return 1;
	}

	kfree_skb(skb);
	return r;
}

/*
 * Returns:
 * <= 0: driver handled the event, skb consumed
 *    1: driver does not handle the event, please do standard processing
 */
static int st_nci_hci_connectivity_event_received(struct nci_dev *ndev,
						u8 host, u8 event,
						struct sk_buff *skb)
{
	int r = 0;
	struct device *dev = &ndev->nfc_dev->dev;
	struct nfc_evt_transaction *transaction;

	pr_debug("connectivity gate event: %x\n", event);

	switch (event) {
	case ST_NCI_EVT_CONNECTIVITY:
		r = nfc_se_connectivity(ndev->nfc_dev, host);
	break;
	case ST_NCI_EVT_TRANSACTION:
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
		if (!transaction)
			return -ENOMEM;

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
		nfc_err(&ndev->nfc_dev->dev, "Unexpected event on connectivity gate\n");
		return 1;
	}
	kfree_skb(skb);
	return r;
}

void st_nci_hci_event_received(struct nci_dev *ndev, u8 pipe,
				 u8 event, struct sk_buff *skb)
{
	u8 gate = ndev->hci_dev->pipes[pipe].gate;
	u8 host = ndev->hci_dev->pipes[pipe].host;

	switch (gate) {
	case NCI_HCI_ADMIN_GATE:
		st_nci_hci_admin_event_received(ndev, event, skb);
	break;
	case ST_NCI_APDU_READER_GATE:
		st_nci_hci_apdu_reader_event_received(ndev, event, skb);
	break;
	case ST_NCI_CONNECTIVITY_GATE:
		st_nci_hci_connectivity_event_received(ndev, host, event, skb);
	break;
	}
}
EXPORT_SYMBOL_GPL(st_nci_hci_event_received);

void st_nci_hci_cmd_received(struct nci_dev *ndev, u8 pipe, u8 cmd,
			       struct sk_buff *skb)
{
	struct st_nci_info *info = nci_get_drvdata(ndev);
	u8 gate = ndev->hci_dev->pipes[pipe].gate;

	pr_debug("cmd: %x\n", cmd);

	switch (cmd) {
	case NCI_HCI_ANY_OPEN_PIPE:
		if (gate != ST_NCI_APDU_READER_GATE &&
		    ndev->hci_dev->pipes[pipe].host != ST_NCI_UICC_HOST_ID)
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
EXPORT_SYMBOL_GPL(st_nci_hci_cmd_received);

static int st_nci_control_se(struct nci_dev *ndev, u8 se_idx,
			     u8 state)
{
	struct st_nci_info *info = nci_get_drvdata(ndev);
	int r, i;
	struct sk_buff *sk_host_list;
	u8 host_id;

	switch (se_idx) {
	case ST_NCI_UICC_HOST_ID:
		ndev->hci_dev->count_pipes = 0;
		ndev->hci_dev->expected_pipes = ST_NCI_SE_COUNT_PIPE_UICC;
		break;
	case ST_NCI_ESE_HOST_ID:
		ndev->hci_dev->count_pipes = 0;
		ndev->hci_dev->expected_pipes = ST_NCI_SE_COUNT_PIPE_EMBEDDED;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Wait for an EVT_HOT_PLUG in order to
	 * retrieve a relevant host list.
	 */
	reinit_completion(&info->se_info.req_completion);
	r = nci_nfcee_mode_set(ndev, se_idx, state);
	if (r != NCI_STATUS_OK)
		return r;

	mod_timer(&info->se_info.se_active_timer, jiffies +
		msecs_to_jiffies(ST_NCI_SE_TO_HOT_PLUG));
	info->se_info.se_active = true;

	/* Ignore return value and check in any case the host_list */
	wait_for_completion_interruptible(&info->se_info.req_completion);

	/* There might be some "collision" after receiving a HOT_PLUG event
	 * This may cause the CLF to not answer to the next hci command.
	 * There is no possible synchronization to prevent this.
	 * Adding a small delay is the only way to solve the issue.
	 */
	if (info->se_info.se_status->is_ese_present &&
	    info->se_info.se_status->is_uicc_present)
		usleep_range(15000, 20000);

	r = nci_hci_get_param(ndev, NCI_HCI_ADMIN_GATE,
			NCI_HCI_ADMIN_PARAM_HOST_LIST, &sk_host_list);
	if (r != NCI_HCI_ANY_OK)
		return r;

	for (i = 0; i < sk_host_list->len &&
		sk_host_list->data[i] != se_idx; i++)
		;
	host_id = sk_host_list->data[i];
	kfree_skb(sk_host_list);
	if (state == ST_NCI_SE_MODE_ON && host_id == se_idx)
		return se_idx;
	else if (state == ST_NCI_SE_MODE_OFF && host_id != se_idx)
		return se_idx;

	return -1;
}

int st_nci_disable_se(struct nci_dev *ndev, u32 se_idx)
{
	int r;

	pr_debug("st_nci_disable_se\n");

	/*
	 * According to upper layer, se_idx == NFC_SE_UICC when
	 * info->se_info.se_status->is_uicc_enable is true should never happen
	 * Same for eSE.
	 */
	r = st_nci_control_se(ndev, se_idx, ST_NCI_SE_MODE_OFF);
	if (r < 0) {
		/* Do best effort to release SWP */
		if (se_idx == NFC_SE_EMBEDDED) {
			r = nci_hci_send_event(ndev, ST_NCI_APDU_READER_GATE,
					ST_NCI_EVT_SE_END_OF_APDU_TRANSFER,
					NULL, 0);
		}
		return r;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(st_nci_disable_se);

int st_nci_enable_se(struct nci_dev *ndev, u32 se_idx)
{
	int r;

	pr_debug("st_nci_enable_se\n");

	/*
	 * According to upper layer, se_idx == NFC_SE_UICC when
	 * info->se_info.se_status->is_uicc_enable is true should never happen.
	 * Same for eSE.
	 */
	r = st_nci_control_se(ndev, se_idx, ST_NCI_SE_MODE_ON);
	if (r == ST_NCI_ESE_HOST_ID) {
		st_nci_se_get_atr(ndev);
		r = nci_hci_send_event(ndev, ST_NCI_APDU_READER_GATE,
				ST_NCI_EVT_SE_SOFT_RESET, NULL, 0);
	}

	if (r < 0) {
		/*
		 * The activation procedure failed, the secure element
		 * is not connected. Remove from the list.
		 */
		nfc_remove_se(ndev->nfc_dev, se_idx);
		return r;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(st_nci_enable_se);

static int st_nci_hci_network_init(struct nci_dev *ndev)
{
	struct st_nci_info *info = nci_get_drvdata(ndev);
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
	memcpy(dest_params->value, &spec_params,
	       sizeof(struct dest_spec_params));
	r = nci_core_conn_create(ndev, NCI_DESTINATION_NFCEE, 1,
				 sizeof(struct core_conn_create_dest_spec_params) +
				 sizeof(struct dest_spec_params),
				 dest_params);
	if (r != NCI_STATUS_OK)
		goto free_dest_params;

	conn_info = ndev->hci_dev->conn_info;
	if (!conn_info)
		goto free_dest_params;

	ndev->hci_dev->init_data.gate_count = ARRAY_SIZE(st_nci_gates);
	memcpy(ndev->hci_dev->init_data.gates, st_nci_gates,
	       sizeof(st_nci_gates));

	/*
	 * Session id must include the driver name + i2c bus addr
	 * persistent info to discriminate 2 identical chips
	 */
	dev_num = find_first_zero_bit(dev_mask, ST_NCI_NUM_DEVICES);
	if (dev_num >= ST_NCI_NUM_DEVICES) {
		r = -ENODEV;
		goto free_dest_params;
	}

	scnprintf(ndev->hci_dev->init_data.session_id,
		  sizeof(ndev->hci_dev->init_data.session_id),
		  "%s%2x", "ST21BH", dev_num);

	r = nci_hci_dev_session_init(ndev);
	if (r != NCI_HCI_ANY_OK)
		goto free_dest_params;

	/*
	 * In factory mode, we prevent secure elements activation
	 * by disabling nfcee on the current HCI connection id.
	 * HCI will be used here only for proprietary commands.
	 */
	if (test_bit(ST_NCI_FACTORY_MODE, &info->flags))
		r = nci_nfcee_mode_set(ndev,
				       ndev->hci_dev->conn_info->dest_params->id,
				       NCI_NFCEE_DISABLE);
	else
		r = nci_nfcee_mode_set(ndev,
				       ndev->hci_dev->conn_info->dest_params->id,
				       NCI_NFCEE_ENABLE);

free_dest_params:
	kfree(dest_params);

exit:
	return r;
}

int st_nci_discover_se(struct nci_dev *ndev)
{
	u8 white_list[2];
	int r, wl_size = 0;
	int se_count = 0;
	struct st_nci_info *info = nci_get_drvdata(ndev);

	pr_debug("st_nci_discover_se\n");

	r = st_nci_hci_network_init(ndev);
	if (r != 0)
		return r;

	if (test_bit(ST_NCI_FACTORY_MODE, &info->flags))
		return 0;

	if (info->se_info.se_status->is_uicc_present)
		white_list[wl_size++] = ST_NCI_UICC_HOST_ID;
	if (info->se_info.se_status->is_ese_present)
		white_list[wl_size++] = ST_NCI_ESE_HOST_ID;

	if (wl_size) {
		r = nci_hci_set_param(ndev, NCI_HCI_ADMIN_GATE,
				      NCI_HCI_ADMIN_PARAM_WHITELIST,
				      white_list, wl_size);
		if (r != NCI_HCI_ANY_OK)
			return r;
	}

	if (info->se_info.se_status->is_uicc_present) {
		nfc_add_se(ndev->nfc_dev, ST_NCI_UICC_HOST_ID, NFC_SE_UICC);
		se_count++;
	}

	if (info->se_info.se_status->is_ese_present) {
		nfc_add_se(ndev->nfc_dev, ST_NCI_ESE_HOST_ID, NFC_SE_EMBEDDED);
		se_count++;
	}

	return !se_count;
}
EXPORT_SYMBOL_GPL(st_nci_discover_se);

int st_nci_se_io(struct nci_dev *ndev, u32 se_idx,
		       u8 *apdu, size_t apdu_length,
		       se_io_cb_t cb, void *cb_context)
{
	struct st_nci_info *info = nci_get_drvdata(ndev);

	pr_debug("\n");

	switch (se_idx) {
	case ST_NCI_ESE_HOST_ID:
		info->se_info.cb = cb;
		info->se_info.cb_context = cb_context;
		mod_timer(&info->se_info.bwi_timer, jiffies +
			  msecs_to_jiffies(info->se_info.wt_timeout));
		info->se_info.bwi_active = true;
		return nci_hci_send_event(ndev, ST_NCI_APDU_READER_GATE,
					ST_NCI_EVT_TRANSMIT_DATA, apdu,
					apdu_length);
	default:
		return -ENODEV;
	}
}
EXPORT_SYMBOL(st_nci_se_io);

static void st_nci_se_wt_timeout(struct timer_list *t)
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
	struct st_nci_info *info = from_timer(info, t, se_info.bwi_timer);

	pr_debug("\n");

	info->se_info.bwi_active = false;

	if (!info->se_info.xch_error) {
		info->se_info.xch_error = true;
		nci_hci_send_event(info->ndlc->ndev, ST_NCI_APDU_READER_GATE,
				ST_NCI_EVT_SE_SOFT_RESET, NULL, 0);
	} else {
		info->se_info.xch_error = false;
		nci_hci_send_event(info->ndlc->ndev, ST_NCI_DEVICE_MGNT_GATE,
				ST_NCI_EVT_SE_HARD_RESET, &param, 1);
	}
	info->se_info.cb(info->se_info.cb_context, NULL, 0, -ETIME);
}

static void st_nci_se_activation_timeout(struct timer_list *t)
{
	struct st_nci_info *info = from_timer(info, t,
					      se_info.se_active_timer);

	pr_debug("\n");

	info->se_info.se_active = false;

	complete(&info->se_info.req_completion);
}

int st_nci_se_init(struct nci_dev *ndev, struct st_nci_se_status *se_status)
{
	struct st_nci_info *info = nci_get_drvdata(ndev);

	init_completion(&info->se_info.req_completion);
	/* initialize timers */
	timer_setup(&info->se_info.bwi_timer, st_nci_se_wt_timeout, 0);
	info->se_info.bwi_active = false;

	timer_setup(&info->se_info.se_active_timer,
		    st_nci_se_activation_timeout, 0);
	info->se_info.se_active = false;

	info->se_info.xch_error = false;

	info->se_info.wt_timeout =
		ST_NCI_BWI_TO_TIMEOUT(ST_NCI_ATR_DEFAULT_BWI);

	info->se_info.se_status = se_status;

	return 0;
}
EXPORT_SYMBOL(st_nci_se_init);

void st_nci_se_deinit(struct nci_dev *ndev)
{
	struct st_nci_info *info = nci_get_drvdata(ndev);

	if (info->se_info.bwi_active)
		del_timer_sync(&info->se_info.bwi_timer);
	if (info->se_info.se_active)
		del_timer_sync(&info->se_info.se_active_timer);

	info->se_info.se_active = false;
	info->se_info.bwi_active = false;
}
EXPORT_SYMBOL(st_nci_se_deinit);

