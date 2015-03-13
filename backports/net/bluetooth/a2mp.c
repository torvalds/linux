/*
   Copyright (c) 2010,2011 Code Aurora Forum.  All rights reserved.
   Copyright (c) 2011,2012 Intel Corp.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 and
   only version 2 as published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*/

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>

#include "a2mp.h"
#include "amp.h"

/* Global AMP Manager list */
LIST_HEAD(amp_mgr_list);
DEFINE_MUTEX(amp_mgr_list_lock);

/* A2MP build & send command helper functions */
static struct a2mp_cmd *__a2mp_build(u8 code, u8 ident, u16 len, void *data)
{
	struct a2mp_cmd *cmd;
	int plen;

	plen = sizeof(*cmd) + len;
	cmd = kzalloc(plen, GFP_KERNEL);
	if (!cmd)
		return NULL;

	cmd->code = code;
	cmd->ident = ident;
	cmd->len = cpu_to_le16(len);

	memcpy(cmd->data, data, len);

	return cmd;
}

void a2mp_send(struct amp_mgr *mgr, u8 code, u8 ident, u16 len, void *data)
{
	struct l2cap_chan *chan = mgr->a2mp_chan;
	struct a2mp_cmd *cmd;
	u16 total_len = len + sizeof(*cmd);
	struct kvec iv;
	struct msghdr msg;

	cmd = __a2mp_build(code, ident, len, data);
	if (!cmd)
		return;

	iv.iov_base = cmd;
	iv.iov_len = total_len;

	memset(&msg, 0, sizeof(msg));

	iov_iter_kvec(&msg.msg_iter, WRITE | ITER_KVEC, &iv, 1, total_len);

	l2cap_chan_send(chan, &msg, total_len);

	kfree(cmd);
}

u8 __next_ident(struct amp_mgr *mgr)
{
	if (++mgr->ident == 0)
		mgr->ident = 1;

	return mgr->ident;
}

/* hci_dev_list shall be locked */
static void __a2mp_add_cl(struct amp_mgr *mgr, struct a2mp_cl *cl)
{
	struct hci_dev *hdev;
	int i = 1;

	cl[0].id = AMP_ID_BREDR;
	cl[0].type = AMP_TYPE_BREDR;
	cl[0].status = AMP_STATUS_BLUETOOTH_ONLY;

	list_for_each_entry(hdev, &hci_dev_list, list) {
		if (hdev->dev_type == HCI_AMP) {
			cl[i].id = hdev->id;
			cl[i].type = hdev->amp_type;
			if (test_bit(HCI_UP, &hdev->flags))
				cl[i].status = hdev->amp_status;
			else
				cl[i].status = AMP_STATUS_POWERED_DOWN;
			i++;
		}
	}
}

/* Processing A2MP messages */
static int a2mp_command_rej(struct amp_mgr *mgr, struct sk_buff *skb,
			    struct a2mp_cmd *hdr)
{
	struct a2mp_cmd_rej *rej = (void *) skb->data;

	if (le16_to_cpu(hdr->len) < sizeof(*rej))
		return -EINVAL;

	BT_DBG("ident %d reason %d", hdr->ident, le16_to_cpu(rej->reason));

	skb_pull(skb, sizeof(*rej));

	return 0;
}

static int a2mp_discover_req(struct amp_mgr *mgr, struct sk_buff *skb,
			     struct a2mp_cmd *hdr)
{
	struct a2mp_discov_req *req = (void *) skb->data;
	u16 len = le16_to_cpu(hdr->len);
	struct a2mp_discov_rsp *rsp;
	u16 ext_feat;
	u8 num_ctrl;
	struct hci_dev *hdev;

	if (len < sizeof(*req))
		return -EINVAL;

	skb_pull(skb, sizeof(*req));

	ext_feat = le16_to_cpu(req->ext_feat);

	BT_DBG("mtu %d efm 0x%4.4x", le16_to_cpu(req->mtu), ext_feat);

	/* check that packet is not broken for now */
	while (ext_feat & A2MP_FEAT_EXT) {
		if (len < sizeof(ext_feat))
			return -EINVAL;

		ext_feat = get_unaligned_le16(skb->data);
		BT_DBG("efm 0x%4.4x", ext_feat);
		len -= sizeof(ext_feat);
		skb_pull(skb, sizeof(ext_feat));
	}

	read_lock(&hci_dev_list_lock);

	/* at minimum the BR/EDR needs to be listed */
	num_ctrl = 1;

	list_for_each_entry(hdev, &hci_dev_list, list) {
		if (hdev->dev_type == HCI_AMP)
			num_ctrl++;
	}

	len = num_ctrl * sizeof(struct a2mp_cl) + sizeof(*rsp);
	rsp = kmalloc(len, GFP_ATOMIC);
	if (!rsp) {
		read_unlock(&hci_dev_list_lock);
		return -ENOMEM;
	}

	rsp->mtu = cpu_to_le16(L2CAP_A2MP_DEFAULT_MTU);
	rsp->ext_feat = 0;

	__a2mp_add_cl(mgr, rsp->cl);

	read_unlock(&hci_dev_list_lock);

	a2mp_send(mgr, A2MP_DISCOVER_RSP, hdr->ident, len, rsp);

	kfree(rsp);
	return 0;
}

static int a2mp_discover_rsp(struct amp_mgr *mgr, struct sk_buff *skb,
			     struct a2mp_cmd *hdr)
{
	struct a2mp_discov_rsp *rsp = (void *) skb->data;
	u16 len = le16_to_cpu(hdr->len);
	struct a2mp_cl *cl;
	u16 ext_feat;
	bool found = false;

	if (len < sizeof(*rsp))
		return -EINVAL;

	len -= sizeof(*rsp);
	skb_pull(skb, sizeof(*rsp));

	ext_feat = le16_to_cpu(rsp->ext_feat);

	BT_DBG("mtu %d efm 0x%4.4x", le16_to_cpu(rsp->mtu), ext_feat);

	/* check that packet is not broken for now */
	while (ext_feat & A2MP_FEAT_EXT) {
		if (len < sizeof(ext_feat))
			return -EINVAL;

		ext_feat = get_unaligned_le16(skb->data);
		BT_DBG("efm 0x%4.4x", ext_feat);
		len -= sizeof(ext_feat);
		skb_pull(skb, sizeof(ext_feat));
	}

	cl = (void *) skb->data;
	while (len >= sizeof(*cl)) {
		BT_DBG("Remote AMP id %d type %d status %d", cl->id, cl->type,
		       cl->status);

		if (cl->id != AMP_ID_BREDR && cl->type != AMP_TYPE_BREDR) {
			struct a2mp_info_req req;

			found = true;
			req.id = cl->id;
			a2mp_send(mgr, A2MP_GETINFO_REQ, __next_ident(mgr),
				  sizeof(req), &req);
		}

		len -= sizeof(*cl);
		cl = (void *) skb_pull(skb, sizeof(*cl));
	}

	/* Fall back to L2CAP init sequence */
	if (!found) {
		struct l2cap_conn *conn = mgr->l2cap_conn;
		struct l2cap_chan *chan;

		mutex_lock(&conn->chan_lock);

		list_for_each_entry(chan, &conn->chan_l, list) {

			BT_DBG("chan %p state %s", chan,
			       state_to_string(chan->state));

			if (chan->scid == L2CAP_CID_A2MP)
				continue;

			l2cap_chan_lock(chan);

			if (chan->state == BT_CONNECT)
				l2cap_send_conn_req(chan);

			l2cap_chan_unlock(chan);
		}

		mutex_unlock(&conn->chan_lock);
	}

	return 0;
}

static int a2mp_change_notify(struct amp_mgr *mgr, struct sk_buff *skb,
			      struct a2mp_cmd *hdr)
{
	struct a2mp_cl *cl = (void *) skb->data;

	while (skb->len >= sizeof(*cl)) {
		BT_DBG("Controller id %d type %d status %d", cl->id, cl->type,
		       cl->status);
		cl = (struct a2mp_cl *) skb_pull(skb, sizeof(*cl));
	}

	/* TODO send A2MP_CHANGE_RSP */

	return 0;
}

static int a2mp_getinfo_req(struct amp_mgr *mgr, struct sk_buff *skb,
			    struct a2mp_cmd *hdr)
{
	struct a2mp_info_req *req  = (void *) skb->data;
	struct hci_dev *hdev;

	if (le16_to_cpu(hdr->len) < sizeof(*req))
		return -EINVAL;

	BT_DBG("id %d", req->id);

	hdev = hci_dev_get(req->id);
	if (!hdev || hdev->dev_type != HCI_AMP) {
		struct a2mp_info_rsp rsp;

		rsp.id = req->id;
		rsp.status = A2MP_STATUS_INVALID_CTRL_ID;

		a2mp_send(mgr, A2MP_GETINFO_RSP, hdr->ident, sizeof(rsp),
			  &rsp);

		goto done;
	}

	set_bit(READ_LOC_AMP_INFO, &mgr->state);
	hci_send_cmd(hdev, HCI_OP_READ_LOCAL_AMP_INFO, 0, NULL);

done:
	if (hdev)
		hci_dev_put(hdev);

	skb_pull(skb, sizeof(*req));
	return 0;
}

static int a2mp_getinfo_rsp(struct amp_mgr *mgr, struct sk_buff *skb,
			    struct a2mp_cmd *hdr)
{
	struct a2mp_info_rsp *rsp = (struct a2mp_info_rsp *) skb->data;
	struct a2mp_amp_assoc_req req;
	struct amp_ctrl *ctrl;

	if (le16_to_cpu(hdr->len) < sizeof(*rsp))
		return -EINVAL;

	BT_DBG("id %d status 0x%2.2x", rsp->id, rsp->status);

	if (rsp->status)
		return -EINVAL;

	ctrl = amp_ctrl_add(mgr, rsp->id);
	if (!ctrl)
		return -ENOMEM;

	req.id = rsp->id;
	a2mp_send(mgr, A2MP_GETAMPASSOC_REQ, __next_ident(mgr), sizeof(req),
		  &req);

	skb_pull(skb, sizeof(*rsp));
	return 0;
}

static int a2mp_getampassoc_req(struct amp_mgr *mgr, struct sk_buff *skb,
				struct a2mp_cmd *hdr)
{
	struct a2mp_amp_assoc_req *req = (void *) skb->data;
	struct hci_dev *hdev;
	struct amp_mgr *tmp;

	if (le16_to_cpu(hdr->len) < sizeof(*req))
		return -EINVAL;

	BT_DBG("id %d", req->id);

	/* Make sure that other request is not processed */
	tmp = amp_mgr_lookup_by_state(READ_LOC_AMP_ASSOC);

	hdev = hci_dev_get(req->id);
	if (!hdev || hdev->amp_type == AMP_TYPE_BREDR || tmp) {
		struct a2mp_amp_assoc_rsp rsp;
		rsp.id = req->id;

		if (tmp) {
			rsp.status = A2MP_STATUS_COLLISION_OCCURED;
			amp_mgr_put(tmp);
		} else {
			rsp.status = A2MP_STATUS_INVALID_CTRL_ID;
		}

		a2mp_send(mgr, A2MP_GETAMPASSOC_RSP, hdr->ident, sizeof(rsp),
			  &rsp);

		goto done;
	}

	amp_read_loc_assoc(hdev, mgr);

done:
	if (hdev)
		hci_dev_put(hdev);

	skb_pull(skb, sizeof(*req));
	return 0;
}

static int a2mp_getampassoc_rsp(struct amp_mgr *mgr, struct sk_buff *skb,
				struct a2mp_cmd *hdr)
{
	struct a2mp_amp_assoc_rsp *rsp = (void *) skb->data;
	u16 len = le16_to_cpu(hdr->len);
	struct hci_dev *hdev;
	struct amp_ctrl *ctrl;
	struct hci_conn *hcon;
	size_t assoc_len;

	if (len < sizeof(*rsp))
		return -EINVAL;

	assoc_len = len - sizeof(*rsp);

	BT_DBG("id %d status 0x%2.2x assoc len %zu", rsp->id, rsp->status,
	       assoc_len);

	if (rsp->status)
		return -EINVAL;

	/* Save remote ASSOC data */
	ctrl = amp_ctrl_lookup(mgr, rsp->id);
	if (ctrl) {
		u8 *assoc;

		assoc = kmemdup(rsp->amp_assoc, assoc_len, GFP_KERNEL);
		if (!assoc) {
			amp_ctrl_put(ctrl);
			return -ENOMEM;
		}

		ctrl->assoc = assoc;
		ctrl->assoc_len = assoc_len;
		ctrl->assoc_rem_len = assoc_len;
		ctrl->assoc_len_so_far = 0;

		amp_ctrl_put(ctrl);
	}

	/* Create Phys Link */
	hdev = hci_dev_get(rsp->id);
	if (!hdev)
		return -EINVAL;

	hcon = phylink_add(hdev, mgr, rsp->id, true);
	if (!hcon)
		goto done;

	BT_DBG("Created hcon %p: loc:%d -> rem:%d", hcon, hdev->id, rsp->id);

	mgr->bredr_chan->remote_amp_id = rsp->id;

	amp_create_phylink(hdev, mgr, hcon);

done:
	hci_dev_put(hdev);
	skb_pull(skb, len);
	return 0;
}

static int a2mp_createphyslink_req(struct amp_mgr *mgr, struct sk_buff *skb,
				   struct a2mp_cmd *hdr)
{
	struct a2mp_physlink_req *req = (void *) skb->data;

	struct a2mp_physlink_rsp rsp;
	struct hci_dev *hdev;
	struct hci_conn *hcon;
	struct amp_ctrl *ctrl;

	if (le16_to_cpu(hdr->len) < sizeof(*req))
		return -EINVAL;

	BT_DBG("local_id %d, remote_id %d", req->local_id, req->remote_id);

	rsp.local_id = req->remote_id;
	rsp.remote_id = req->local_id;

	hdev = hci_dev_get(req->remote_id);
	if (!hdev || hdev->amp_type == AMP_TYPE_BREDR) {
		rsp.status = A2MP_STATUS_INVALID_CTRL_ID;
		goto send_rsp;
	}

	ctrl = amp_ctrl_lookup(mgr, rsp.remote_id);
	if (!ctrl) {
		ctrl = amp_ctrl_add(mgr, rsp.remote_id);
		if (ctrl) {
			amp_ctrl_get(ctrl);
		} else {
			rsp.status = A2MP_STATUS_UNABLE_START_LINK_CREATION;
			goto send_rsp;
		}
	}

	if (ctrl) {
		size_t assoc_len = le16_to_cpu(hdr->len) - sizeof(*req);
		u8 *assoc;

		assoc = kmemdup(req->amp_assoc, assoc_len, GFP_KERNEL);
		if (!assoc) {
			amp_ctrl_put(ctrl);
			return -ENOMEM;
		}

		ctrl->assoc = assoc;
		ctrl->assoc_len = assoc_len;
		ctrl->assoc_rem_len = assoc_len;
		ctrl->assoc_len_so_far = 0;

		amp_ctrl_put(ctrl);
	}

	hcon = phylink_add(hdev, mgr, req->local_id, false);
	if (hcon) {
		amp_accept_phylink(hdev, mgr, hcon);
		rsp.status = A2MP_STATUS_SUCCESS;
	} else {
		rsp.status = A2MP_STATUS_UNABLE_START_LINK_CREATION;
	}

send_rsp:
	if (hdev)
		hci_dev_put(hdev);

	/* Reply error now and success after HCI Write Remote AMP Assoc
	   command complete with success status
	 */
	if (rsp.status != A2MP_STATUS_SUCCESS) {
		a2mp_send(mgr, A2MP_CREATEPHYSLINK_RSP, hdr->ident,
			  sizeof(rsp), &rsp);
	} else {
		set_bit(WRITE_REMOTE_AMP_ASSOC, &mgr->state);
		mgr->ident = hdr->ident;
	}

	skb_pull(skb, le16_to_cpu(hdr->len));
	return 0;
}

static int a2mp_discphyslink_req(struct amp_mgr *mgr, struct sk_buff *skb,
				 struct a2mp_cmd *hdr)
{
	struct a2mp_physlink_req *req = (void *) skb->data;
	struct a2mp_physlink_rsp rsp;
	struct hci_dev *hdev;
	struct hci_conn *hcon;

	if (le16_to_cpu(hdr->len) < sizeof(*req))
		return -EINVAL;

	BT_DBG("local_id %d remote_id %d", req->local_id, req->remote_id);

	rsp.local_id = req->remote_id;
	rsp.remote_id = req->local_id;
	rsp.status = A2MP_STATUS_SUCCESS;

	hdev = hci_dev_get(req->remote_id);
	if (!hdev) {
		rsp.status = A2MP_STATUS_INVALID_CTRL_ID;
		goto send_rsp;
	}

	hcon = hci_conn_hash_lookup_ba(hdev, AMP_LINK,
				       &mgr->l2cap_conn->hcon->dst);
	if (!hcon) {
		BT_ERR("No phys link exist");
		rsp.status = A2MP_STATUS_NO_PHYSICAL_LINK_EXISTS;
		goto clean;
	}

	/* TODO Disconnect Phys Link here */

clean:
	hci_dev_put(hdev);

send_rsp:
	a2mp_send(mgr, A2MP_DISCONNPHYSLINK_RSP, hdr->ident, sizeof(rsp), &rsp);

	skb_pull(skb, sizeof(*req));
	return 0;
}

static inline int a2mp_cmd_rsp(struct amp_mgr *mgr, struct sk_buff *skb,
			       struct a2mp_cmd *hdr)
{
	BT_DBG("ident %d code 0x%2.2x", hdr->ident, hdr->code);

	skb_pull(skb, le16_to_cpu(hdr->len));
	return 0;
}

/* Handle A2MP signalling */
static int a2mp_chan_recv_cb(struct l2cap_chan *chan, struct sk_buff *skb)
{
	struct a2mp_cmd *hdr;
	struct amp_mgr *mgr = chan->data;
	int err = 0;

	amp_mgr_get(mgr);

	while (skb->len >= sizeof(*hdr)) {
		u16 len;

		hdr = (void *) skb->data;
		len = le16_to_cpu(hdr->len);

		BT_DBG("code 0x%2.2x id %d len %u", hdr->code, hdr->ident, len);

		skb_pull(skb, sizeof(*hdr));

		if (len > skb->len || !hdr->ident) {
			err = -EINVAL;
			break;
		}

		mgr->ident = hdr->ident;

		switch (hdr->code) {
		case A2MP_COMMAND_REJ:
			a2mp_command_rej(mgr, skb, hdr);
			break;

		case A2MP_DISCOVER_REQ:
			err = a2mp_discover_req(mgr, skb, hdr);
			break;

		case A2MP_CHANGE_NOTIFY:
			err = a2mp_change_notify(mgr, skb, hdr);
			break;

		case A2MP_GETINFO_REQ:
			err = a2mp_getinfo_req(mgr, skb, hdr);
			break;

		case A2MP_GETAMPASSOC_REQ:
			err = a2mp_getampassoc_req(mgr, skb, hdr);
			break;

		case A2MP_CREATEPHYSLINK_REQ:
			err = a2mp_createphyslink_req(mgr, skb, hdr);
			break;

		case A2MP_DISCONNPHYSLINK_REQ:
			err = a2mp_discphyslink_req(mgr, skb, hdr);
			break;

		case A2MP_DISCOVER_RSP:
			err = a2mp_discover_rsp(mgr, skb, hdr);
			break;

		case A2MP_GETINFO_RSP:
			err = a2mp_getinfo_rsp(mgr, skb, hdr);
			break;

		case A2MP_GETAMPASSOC_RSP:
			err = a2mp_getampassoc_rsp(mgr, skb, hdr);
			break;

		case A2MP_CHANGE_RSP:
		case A2MP_CREATEPHYSLINK_RSP:
		case A2MP_DISCONNPHYSLINK_RSP:
			err = a2mp_cmd_rsp(mgr, skb, hdr);
			break;

		default:
			BT_ERR("Unknown A2MP sig cmd 0x%2.2x", hdr->code);
			err = -EINVAL;
			break;
		}
	}

	if (err) {
		struct a2mp_cmd_rej rej;

		rej.reason = cpu_to_le16(0);
		hdr = (void *) skb->data;

		BT_DBG("Send A2MP Rej: cmd 0x%2.2x err %d", hdr->code, err);

		a2mp_send(mgr, A2MP_COMMAND_REJ, hdr->ident, sizeof(rej),
			  &rej);
	}

	/* Always free skb and return success error code to prevent
	   from sending L2CAP Disconnect over A2MP channel */
	kfree_skb(skb);

	amp_mgr_put(mgr);

	return 0;
}

static void a2mp_chan_close_cb(struct l2cap_chan *chan)
{
	l2cap_chan_put(chan);
}

static void a2mp_chan_state_change_cb(struct l2cap_chan *chan, int state,
				      int err)
{
	struct amp_mgr *mgr = chan->data;

	if (!mgr)
		return;

	BT_DBG("chan %p state %s", chan, state_to_string(state));

	chan->state = state;

	switch (state) {
	case BT_CLOSED:
		if (mgr)
			amp_mgr_put(mgr);
		break;
	}
}

static struct sk_buff *a2mp_chan_alloc_skb_cb(struct l2cap_chan *chan,
					      unsigned long hdr_len,
					      unsigned long len, int nb)
{
	struct sk_buff *skb;

	skb = bt_skb_alloc(hdr_len + len, GFP_KERNEL);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	return skb;
}

static const struct l2cap_ops a2mp_chan_ops = {
	.name = "L2CAP A2MP channel",
	.recv = a2mp_chan_recv_cb,
	.close = a2mp_chan_close_cb,
	.state_change = a2mp_chan_state_change_cb,
	.alloc_skb = a2mp_chan_alloc_skb_cb,

	/* Not implemented for A2MP */
	.new_connection = l2cap_chan_no_new_connection,
	.teardown = l2cap_chan_no_teardown,
	.ready = l2cap_chan_no_ready,
	.defer = l2cap_chan_no_defer,
	.resume = l2cap_chan_no_resume,
	.set_shutdown = l2cap_chan_no_set_shutdown,
	.get_sndtimeo = l2cap_chan_no_get_sndtimeo,
};

static struct l2cap_chan *a2mp_chan_open(struct l2cap_conn *conn, bool locked)
{
	struct l2cap_chan *chan;
	int err;

	chan = l2cap_chan_create();
	if (!chan)
		return NULL;

	BT_DBG("chan %p", chan);

	chan->chan_type = L2CAP_CHAN_FIXED;
	chan->scid = L2CAP_CID_A2MP;
	chan->dcid = L2CAP_CID_A2MP;
	chan->omtu = L2CAP_A2MP_DEFAULT_MTU;
	chan->imtu = L2CAP_A2MP_DEFAULT_MTU;
	chan->flush_to = L2CAP_DEFAULT_FLUSH_TO;

	chan->ops = &a2mp_chan_ops;

	l2cap_chan_set_defaults(chan);
	chan->remote_max_tx = chan->max_tx;
	chan->remote_tx_win = chan->tx_win;

	chan->retrans_timeout = L2CAP_DEFAULT_RETRANS_TO;
	chan->monitor_timeout = L2CAP_DEFAULT_MONITOR_TO;

	skb_queue_head_init(&chan->tx_q);

	chan->mode = L2CAP_MODE_ERTM;

	err = l2cap_ertm_init(chan);
	if (err < 0) {
		l2cap_chan_del(chan, 0);
		return NULL;
	}

	chan->conf_state = 0;

	if (locked)
		__l2cap_chan_add(conn, chan);
	else
		l2cap_chan_add(conn, chan);

	chan->remote_mps = chan->omtu;
	chan->mps = chan->omtu;

	chan->state = BT_CONNECTED;

	return chan;
}

/* AMP Manager functions */
struct amp_mgr *amp_mgr_get(struct amp_mgr *mgr)
{
	BT_DBG("mgr %p orig refcnt %d", mgr, atomic_read(&mgr->kref.refcount));

	kref_get(&mgr->kref);

	return mgr;
}

static void amp_mgr_destroy(struct kref *kref)
{
	struct amp_mgr *mgr = container_of(kref, struct amp_mgr, kref);

	BT_DBG("mgr %p", mgr);

	mutex_lock(&amp_mgr_list_lock);
	list_del(&mgr->list);
	mutex_unlock(&amp_mgr_list_lock);

	amp_ctrl_list_flush(mgr);
	kfree(mgr);
}

int amp_mgr_put(struct amp_mgr *mgr)
{
	BT_DBG("mgr %p orig refcnt %d", mgr, atomic_read(&mgr->kref.refcount));

	return kref_put(&mgr->kref, &amp_mgr_destroy);
}

static struct amp_mgr *amp_mgr_create(struct l2cap_conn *conn, bool locked)
{
	struct amp_mgr *mgr;
	struct l2cap_chan *chan;

	mgr = kzalloc(sizeof(*mgr), GFP_KERNEL);
	if (!mgr)
		return NULL;

	BT_DBG("conn %p mgr %p", conn, mgr);

	mgr->l2cap_conn = conn;

	chan = a2mp_chan_open(conn, locked);
	if (!chan) {
		kfree(mgr);
		return NULL;
	}

	mgr->a2mp_chan = chan;
	chan->data = mgr;

	conn->hcon->amp_mgr = mgr;

	kref_init(&mgr->kref);

	/* Remote AMP ctrl list initialization */
	INIT_LIST_HEAD(&mgr->amp_ctrls);
	mutex_init(&mgr->amp_ctrls_lock);

	mutex_lock(&amp_mgr_list_lock);
	list_add(&mgr->list, &amp_mgr_list);
	mutex_unlock(&amp_mgr_list_lock);

	return mgr;
}

struct l2cap_chan *a2mp_channel_create(struct l2cap_conn *conn,
				       struct sk_buff *skb)
{
	struct amp_mgr *mgr;

	if (conn->hcon->type != ACL_LINK)
		return NULL;

	mgr = amp_mgr_create(conn, false);
	if (!mgr) {
		BT_ERR("Could not create AMP manager");
		return NULL;
	}

	BT_DBG("mgr: %p chan %p", mgr, mgr->a2mp_chan);

	return mgr->a2mp_chan;
}

struct amp_mgr *amp_mgr_lookup_by_state(u8 state)
{
	struct amp_mgr *mgr;

	mutex_lock(&amp_mgr_list_lock);
	list_for_each_entry(mgr, &amp_mgr_list, list) {
		if (test_and_clear_bit(state, &mgr->state)) {
			amp_mgr_get(mgr);
			mutex_unlock(&amp_mgr_list_lock);
			return mgr;
		}
	}
	mutex_unlock(&amp_mgr_list_lock);

	return NULL;
}

void a2mp_send_getinfo_rsp(struct hci_dev *hdev)
{
	struct amp_mgr *mgr;
	struct a2mp_info_rsp rsp;

	mgr = amp_mgr_lookup_by_state(READ_LOC_AMP_INFO);
	if (!mgr)
		return;

	BT_DBG("%s mgr %p", hdev->name, mgr);

	rsp.id = hdev->id;
	rsp.status = A2MP_STATUS_INVALID_CTRL_ID;

	if (hdev->amp_type != AMP_TYPE_BREDR) {
		rsp.status = 0;
		rsp.total_bw = cpu_to_le32(hdev->amp_total_bw);
		rsp.max_bw = cpu_to_le32(hdev->amp_max_bw);
		rsp.min_latency = cpu_to_le32(hdev->amp_min_latency);
		rsp.pal_cap = cpu_to_le16(hdev->amp_pal_cap);
		rsp.assoc_size = cpu_to_le16(hdev->amp_assoc_size);
	}

	a2mp_send(mgr, A2MP_GETINFO_RSP, mgr->ident, sizeof(rsp), &rsp);
	amp_mgr_put(mgr);
}

void a2mp_send_getampassoc_rsp(struct hci_dev *hdev, u8 status)
{
	struct amp_mgr *mgr;
	struct amp_assoc *loc_assoc = &hdev->loc_assoc;
	struct a2mp_amp_assoc_rsp *rsp;
	size_t len;

	mgr = amp_mgr_lookup_by_state(READ_LOC_AMP_ASSOC);
	if (!mgr)
		return;

	BT_DBG("%s mgr %p", hdev->name, mgr);

	len = sizeof(struct a2mp_amp_assoc_rsp) + loc_assoc->len;
	rsp = kzalloc(len, GFP_KERNEL);
	if (!rsp) {
		amp_mgr_put(mgr);
		return;
	}

	rsp->id = hdev->id;

	if (status) {
		rsp->status = A2MP_STATUS_INVALID_CTRL_ID;
	} else {
		rsp->status = A2MP_STATUS_SUCCESS;
		memcpy(rsp->amp_assoc, loc_assoc->data, loc_assoc->len);
	}

	a2mp_send(mgr, A2MP_GETAMPASSOC_RSP, mgr->ident, len, rsp);
	amp_mgr_put(mgr);
	kfree(rsp);
}

void a2mp_send_create_phy_link_req(struct hci_dev *hdev, u8 status)
{
	struct amp_mgr *mgr;
	struct amp_assoc *loc_assoc = &hdev->loc_assoc;
	struct a2mp_physlink_req *req;
	struct l2cap_chan *bredr_chan;
	size_t len;

	mgr = amp_mgr_lookup_by_state(READ_LOC_AMP_ASSOC_FINAL);
	if (!mgr)
		return;

	len = sizeof(*req) + loc_assoc->len;

	BT_DBG("%s mgr %p assoc_len %zu", hdev->name, mgr, len);

	req = kzalloc(len, GFP_KERNEL);
	if (!req) {
		amp_mgr_put(mgr);
		return;
	}

	bredr_chan = mgr->bredr_chan;
	if (!bredr_chan)
		goto clean;

	req->local_id = hdev->id;
	req->remote_id = bredr_chan->remote_amp_id;
	memcpy(req->amp_assoc, loc_assoc->data, loc_assoc->len);

	a2mp_send(mgr, A2MP_CREATEPHYSLINK_REQ, __next_ident(mgr), len, req);

clean:
	amp_mgr_put(mgr);
	kfree(req);
}

void a2mp_send_create_phy_link_rsp(struct hci_dev *hdev, u8 status)
{
	struct amp_mgr *mgr;
	struct a2mp_physlink_rsp rsp;
	struct hci_conn *hs_hcon;

	mgr = amp_mgr_lookup_by_state(WRITE_REMOTE_AMP_ASSOC);
	if (!mgr)
		return;

	hs_hcon = hci_conn_hash_lookup_state(hdev, AMP_LINK, BT_CONNECT);
	if (!hs_hcon) {
		rsp.status = A2MP_STATUS_UNABLE_START_LINK_CREATION;
	} else {
		rsp.remote_id = hs_hcon->remote_id;
		rsp.status = A2MP_STATUS_SUCCESS;
	}

	BT_DBG("%s mgr %p hs_hcon %p status %u", hdev->name, mgr, hs_hcon,
	       status);

	rsp.local_id = hdev->id;
	a2mp_send(mgr, A2MP_CREATEPHYSLINK_RSP, mgr->ident, sizeof(rsp), &rsp);
	amp_mgr_put(mgr);
}

void a2mp_discover_amp(struct l2cap_chan *chan)
{
	struct l2cap_conn *conn = chan->conn;
	struct amp_mgr *mgr = conn->hcon->amp_mgr;
	struct a2mp_discov_req req;

	BT_DBG("chan %p conn %p mgr %p", chan, conn, mgr);

	if (!mgr) {
		mgr = amp_mgr_create(conn, true);
		if (!mgr)
			return;
	}

	mgr->bredr_chan = chan;

	req.mtu = cpu_to_le16(L2CAP_A2MP_DEFAULT_MTU);
	req.ext_feat = 0;
	a2mp_send(mgr, A2MP_DISCOVER_REQ, 1, sizeof(req), &req);
}
