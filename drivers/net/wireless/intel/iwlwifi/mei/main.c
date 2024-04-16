// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2022 Intel Corporation
 */

#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/ieee80211.h>
#include <linux/rtnetlink.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mei_cl_bus.h>
#include <linux/rcupdate.h>
#include <linux/debugfs.h>
#include <linux/skbuff.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <net/cfg80211.h>

#include "internal.h"
#include "iwl-mei.h"
#include "trace.h"
#include "trace-data.h"
#include "sap.h"

MODULE_DESCRIPTION("The Intel(R) wireless / CSME firmware interface");
MODULE_LICENSE("GPL");

#define MEI_WLAN_UUID UUID_LE(0x13280904, 0x7792, 0x4fcb, \
			      0xa1, 0xaa, 0x5e, 0x70, 0xcb, 0xb1, 0xe8, 0x65)

/*
 * Since iwlwifi calls iwlmei without any context, hold a pointer to the
 * mei_cl_device structure here.
 * Define a mutex that will synchronize all the flows between iwlwifi and
 * iwlmei.
 * Note that iwlmei can't have several instances, so it ok to have static
 * variables here.
 */
static struct mei_cl_device *iwl_mei_global_cldev;
static DEFINE_MUTEX(iwl_mei_mutex);
static unsigned long iwl_mei_status;

enum iwl_mei_status_bits {
	IWL_MEI_STATUS_SAP_CONNECTED,
};

bool iwl_mei_is_connected(void)
{
	return test_bit(IWL_MEI_STATUS_SAP_CONNECTED, &iwl_mei_status);
}
EXPORT_SYMBOL_GPL(iwl_mei_is_connected);

#define SAP_VERSION	3
#define SAP_CONTROL_BLOCK_ID 0x21504153 /* SAP! in ASCII */

struct iwl_sap_q_ctrl_blk {
	__le32 wr_ptr;
	__le32 rd_ptr;
	__le32 size;
};

enum iwl_sap_q_idx {
	SAP_QUEUE_IDX_NOTIF = 0,
	SAP_QUEUE_IDX_DATA,
	SAP_QUEUE_IDX_MAX,
};

struct iwl_sap_dir {
	__le32 reserved;
	struct iwl_sap_q_ctrl_blk q_ctrl_blk[SAP_QUEUE_IDX_MAX];
};

enum iwl_sap_dir_idx {
	SAP_DIRECTION_HOST_TO_ME = 0,
	SAP_DIRECTION_ME_TO_HOST,
	SAP_DIRECTION_MAX,
};

struct iwl_sap_shared_mem_ctrl_blk {
	__le32 sap_id;
	__le32 size;
	struct iwl_sap_dir dir[SAP_DIRECTION_MAX];
};

/*
 * The shared area has the following layout:
 *
 * +-----------------------------------+
 * |struct iwl_sap_shared_mem_ctrl_blk |
 * +-----------------------------------+
 * |Host -> ME data queue              |
 * +-----------------------------------+
 * |Host -> ME notif queue             |
 * +-----------------------------------+
 * |ME -> Host data queue              |
 * +-----------------------------------+
 * |ME -> host notif queue             |
 * +-----------------------------------+
 * |SAP control block id (SAP!)        |
 * +-----------------------------------+
 */

#define SAP_H2M_DATA_Q_SZ	48256
#define SAP_M2H_DATA_Q_SZ	24128
#define SAP_H2M_NOTIF_Q_SZ	2240
#define SAP_M2H_NOTIF_Q_SZ	62720

#define _IWL_MEI_SAP_SHARED_MEM_SZ \
	(sizeof(struct iwl_sap_shared_mem_ctrl_blk) + \
	 SAP_H2M_DATA_Q_SZ + SAP_H2M_NOTIF_Q_SZ + \
	 SAP_M2H_DATA_Q_SZ + SAP_M2H_NOTIF_Q_SZ + 4)

#define IWL_MEI_SAP_SHARED_MEM_SZ \
	(roundup(_IWL_MEI_SAP_SHARED_MEM_SZ, PAGE_SIZE))

struct iwl_mei_shared_mem_ptrs {
	struct iwl_sap_shared_mem_ctrl_blk *ctrl;
	void *q_head[SAP_DIRECTION_MAX][SAP_QUEUE_IDX_MAX];
	size_t q_size[SAP_DIRECTION_MAX][SAP_QUEUE_IDX_MAX];
};

struct iwl_mei_filters {
	struct rcu_head rcu_head;
	struct iwl_sap_oob_filters filters;
};

/**
 * struct iwl_mei - holds the private date for iwl_mei
 *
 * @get_nvm_wq: the wait queue for the get_nvm flow
 * @send_csa_msg_wk: used to defer the transmission of the CHECK_SHARED_AREA
 *	message. Used so that we can send CHECK_SHARED_AREA from atomic
 *	contexts.
 * @get_ownership_wq: the wait queue for the get_ownership_flow
 * @shared_mem: the memory that is shared between CSME and the host
 * @cldev: the pointer to the MEI client device
 * @nvm: the data returned by the CSME for the NVM
 * @filters: the filters sent by CSME
 * @got_ownership: true if we own the device
 * @amt_enabled: true if CSME has wireless enabled
 * @csa_throttled: when true, we can't send CHECK_SHARED_AREA over the MEI
 *	bus, but rather need to wait until send_csa_msg_wk runs
 * @csme_taking_ownership: true when CSME is taking ownership. Used to remember
 *	to send CSME_OWNERSHIP_CONFIRMED when the driver completes its down
 *	flow.
 * @link_prot_state: true when we are in link protection PASSIVE
 * @device_down: true if the device is down. Used to remember to send
 *	CSME_OWNERSHIP_CONFIRMED when the driver is already down.
 * @csa_throttle_end_wk: used when &csa_throttled is true
 * @data_q_lock: protects the access to the data queues which are
 *	accessed without the mutex.
 * @netdev_work: used to defer registering and unregistering of the netdev to
 *	avoid taking the rtnl lock in the SAP messages handlers.
 * @sap_seq_no: the sequence number for the SAP messages
 * @seq_no: the sequence number for the SAP messages
 * @dbgfs_dir: the debugfs dir entry
 */
struct iwl_mei {
	wait_queue_head_t get_nvm_wq;
	struct work_struct send_csa_msg_wk;
	wait_queue_head_t get_ownership_wq;
	struct iwl_mei_shared_mem_ptrs shared_mem;
	struct mei_cl_device *cldev;
	struct iwl_mei_nvm *nvm;
	struct iwl_mei_filters __rcu *filters;
	bool got_ownership;
	bool amt_enabled;
	bool csa_throttled;
	bool csme_taking_ownership;
	bool link_prot_state;
	bool device_down;
	struct delayed_work csa_throttle_end_wk;
	spinlock_t data_q_lock;
	struct work_struct netdev_work;

	atomic_t sap_seq_no;
	atomic_t seq_no;

	struct dentry *dbgfs_dir;
};

/**
 * struct iwl_mei_cache - cache for the parameters from iwlwifi
 * @ops: Callbacks to iwlwifi.
 * @netdev: The netdev that will be used to transmit / receive packets.
 * @conn_info: The connection info message triggered by iwlwifi's association.
 * @power_limit: pointer to an array of 10 elements (le16) represents the power
 *	restrictions per chain.
 * @rf_kill: rf kill state.
 * @mcc: MCC info
 * @mac_address: interface MAC address.
 * @nvm_address: NVM MAC address.
 * @priv: A pointer to iwlwifi.
 *
 * This used to cache the configurations coming from iwlwifi's way. The data
 * is cached here so that we can buffer the configuration even if we don't have
 * a bind from the mei bus and hence, on iwl_mei structure.
 */
struct iwl_mei_cache {
	const struct iwl_mei_ops *ops;
	struct net_device __rcu *netdev;
	const struct iwl_sap_notif_connection_info *conn_info;
	const __le16 *power_limit;
	u32 rf_kill;
	u16 mcc;
	u8 mac_address[6];
	u8 nvm_address[6];
	void *priv;
};

static struct iwl_mei_cache iwl_mei_cache = {
	.rf_kill = SAP_HW_RFKILL_DEASSERTED | SAP_SW_RFKILL_DEASSERTED
};

static void iwl_mei_free_shared_mem(struct mei_cl_device *cldev)
{
	struct iwl_mei *mei = mei_cldev_get_drvdata(cldev);

	if (mei_cldev_dma_unmap(cldev))
		dev_err(&cldev->dev, "Couldn't unmap the shared mem properly\n");
	memset(&mei->shared_mem, 0, sizeof(mei->shared_mem));
}

#define HBM_DMA_BUF_ID_WLAN 1

static int iwl_mei_alloc_shared_mem(struct mei_cl_device *cldev)
{
	struct iwl_mei *mei = mei_cldev_get_drvdata(cldev);
	struct iwl_mei_shared_mem_ptrs *mem = &mei->shared_mem;

	mem->ctrl = mei_cldev_dma_map(cldev, HBM_DMA_BUF_ID_WLAN,
				       IWL_MEI_SAP_SHARED_MEM_SZ);

	if (IS_ERR(mem->ctrl)) {
		int ret = PTR_ERR(mem->ctrl);

		mem->ctrl = NULL;

		return ret;
	}

	memset(mem->ctrl, 0, IWL_MEI_SAP_SHARED_MEM_SZ);

	return 0;
}

static void iwl_mei_init_shared_mem(struct iwl_mei *mei)
{
	struct iwl_mei_shared_mem_ptrs *mem = &mei->shared_mem;
	struct iwl_sap_dir *h2m;
	struct iwl_sap_dir *m2h;
	int dir, queue;
	u8 *q_head;

	mem->ctrl->sap_id = cpu_to_le32(SAP_CONTROL_BLOCK_ID);

	mem->ctrl->size = cpu_to_le32(sizeof(*mem->ctrl));

	h2m = &mem->ctrl->dir[SAP_DIRECTION_HOST_TO_ME];
	m2h = &mem->ctrl->dir[SAP_DIRECTION_ME_TO_HOST];

	h2m->q_ctrl_blk[SAP_QUEUE_IDX_DATA].size =
		cpu_to_le32(SAP_H2M_DATA_Q_SZ);
	h2m->q_ctrl_blk[SAP_QUEUE_IDX_NOTIF].size =
		cpu_to_le32(SAP_H2M_NOTIF_Q_SZ);
	m2h->q_ctrl_blk[SAP_QUEUE_IDX_DATA].size =
		cpu_to_le32(SAP_M2H_DATA_Q_SZ);
	m2h->q_ctrl_blk[SAP_QUEUE_IDX_NOTIF].size =
		cpu_to_le32(SAP_M2H_NOTIF_Q_SZ);

	/* q_head points to the start of the first queue */
	q_head = (void *)(mem->ctrl + 1);

	/* Initialize the queue heads */
	for (dir = 0; dir < SAP_DIRECTION_MAX; dir++) {
		for (queue = 0; queue < SAP_QUEUE_IDX_MAX; queue++) {
			mem->q_head[dir][queue] = q_head;
			q_head +=
				le32_to_cpu(mem->ctrl->dir[dir].q_ctrl_blk[queue].size);
			mem->q_size[dir][queue] =
				le32_to_cpu(mem->ctrl->dir[dir].q_ctrl_blk[queue].size);
		}
	}

	*(__le32 *)q_head = cpu_to_le32(SAP_CONTROL_BLOCK_ID);
}

static ssize_t iwl_mei_write_cyclic_buf(struct mei_cl_device *cldev,
					struct iwl_sap_q_ctrl_blk *notif_q,
					u8 *q_head,
					const struct iwl_sap_hdr *hdr,
					u32 q_sz)
{
	u32 rd = le32_to_cpu(READ_ONCE(notif_q->rd_ptr));
	u32 wr = le32_to_cpu(READ_ONCE(notif_q->wr_ptr));
	size_t room_in_buf;
	size_t tx_sz = sizeof(*hdr) + le16_to_cpu(hdr->len);

	if (rd > q_sz || wr > q_sz) {
		dev_err(&cldev->dev,
			"Pointers are past the end of the buffer\n");
		return -EINVAL;
	}

	room_in_buf = wr >= rd ? q_sz - wr + rd : rd - wr;

	/* we don't have enough room for the data to write */
	if (room_in_buf < tx_sz) {
		dev_err(&cldev->dev,
			"Not enough room in the buffer\n");
		return -ENOSPC;
	}

	if (wr + tx_sz <= q_sz) {
		memcpy(q_head + wr, hdr, tx_sz);
	} else {
		memcpy(q_head + wr, hdr, q_sz - wr);
		memcpy(q_head, (const u8 *)hdr + q_sz - wr, tx_sz - (q_sz - wr));
	}

	WRITE_ONCE(notif_q->wr_ptr, cpu_to_le32((wr + tx_sz) % q_sz));
	return 0;
}

static bool iwl_mei_host_to_me_data_pending(const struct iwl_mei *mei)
{
	struct iwl_sap_q_ctrl_blk *notif_q;
	struct iwl_sap_dir *dir;

	dir = &mei->shared_mem.ctrl->dir[SAP_DIRECTION_HOST_TO_ME];
	notif_q = &dir->q_ctrl_blk[SAP_QUEUE_IDX_DATA];

	if (READ_ONCE(notif_q->wr_ptr) != READ_ONCE(notif_q->rd_ptr))
		return true;

	notif_q = &dir->q_ctrl_blk[SAP_QUEUE_IDX_NOTIF];
	return READ_ONCE(notif_q->wr_ptr) != READ_ONCE(notif_q->rd_ptr);
}

static int iwl_mei_send_check_shared_area(struct mei_cl_device *cldev)
{
	struct iwl_mei *mei = mei_cldev_get_drvdata(cldev);
	struct iwl_sap_me_msg_start msg = {
		.hdr.type = cpu_to_le32(SAP_ME_MSG_CHECK_SHARED_AREA),
		.hdr.seq_num = cpu_to_le32(atomic_inc_return(&mei->seq_no)),
	};
	int ret;

	lockdep_assert_held(&iwl_mei_mutex);

	if (mei->csa_throttled)
		return 0;

	trace_iwlmei_me_msg(&msg.hdr, true);
	ret = mei_cldev_send(cldev, (void *)&msg, sizeof(msg));
	if (ret != sizeof(msg)) {
		dev_err(&cldev->dev,
			"failed to send the SAP_ME_MSG_CHECK_SHARED_AREA message %d\n",
			ret);
		return ret;
	}

	mei->csa_throttled = true;

	schedule_delayed_work(&mei->csa_throttle_end_wk,
			      msecs_to_jiffies(100));

	return 0;
}

static void iwl_mei_csa_throttle_end_wk(struct work_struct *wk)
{
	struct iwl_mei *mei =
		container_of(wk, struct iwl_mei, csa_throttle_end_wk.work);

	mutex_lock(&iwl_mei_mutex);

	mei->csa_throttled = false;

	if (iwl_mei_host_to_me_data_pending(mei))
		iwl_mei_send_check_shared_area(mei->cldev);

	mutex_unlock(&iwl_mei_mutex);
}

static int iwl_mei_send_sap_msg_payload(struct mei_cl_device *cldev,
					struct iwl_sap_hdr *hdr)
{
	struct iwl_mei *mei = mei_cldev_get_drvdata(cldev);
	struct iwl_sap_q_ctrl_blk *notif_q;
	struct iwl_sap_dir *dir;
	void *q_head;
	u32 q_sz;
	int ret;

	lockdep_assert_held(&iwl_mei_mutex);

	if (!mei->shared_mem.ctrl) {
		dev_err(&cldev->dev,
			"No shared memory, can't send any SAP message\n");
		return -EINVAL;
	}

	if (!iwl_mei_is_connected()) {
		dev_err(&cldev->dev,
			"Can't send a SAP message if we're not connected\n");
		return -ENODEV;
	}

	hdr->seq_num = cpu_to_le32(atomic_inc_return(&mei->sap_seq_no));
	dev_dbg(&cldev->dev, "Sending %d\n", hdr->type);

	dir = &mei->shared_mem.ctrl->dir[SAP_DIRECTION_HOST_TO_ME];
	notif_q = &dir->q_ctrl_blk[SAP_QUEUE_IDX_NOTIF];
	q_head = mei->shared_mem.q_head[SAP_DIRECTION_HOST_TO_ME][SAP_QUEUE_IDX_NOTIF];
	q_sz = mei->shared_mem.q_size[SAP_DIRECTION_HOST_TO_ME][SAP_QUEUE_IDX_NOTIF];
	ret = iwl_mei_write_cyclic_buf(q_head, notif_q, q_head, hdr, q_sz);

	if (ret < 0)
		return ret;

	trace_iwlmei_sap_cmd(hdr, true);

	return iwl_mei_send_check_shared_area(cldev);
}

void iwl_mei_add_data_to_ring(struct sk_buff *skb, bool cb_tx)
{
	struct iwl_sap_q_ctrl_blk *notif_q;
	struct iwl_sap_dir *dir;
	struct iwl_mei *mei;
	size_t room_in_buf;
	size_t tx_sz;
	size_t hdr_sz;
	u32 q_sz;
	u32 rd;
	u32 wr;
	u8 *q_head;

	if (!iwl_mei_global_cldev)
		return;

	mei = mei_cldev_get_drvdata(iwl_mei_global_cldev);

	/*
	 * We access this path for Rx packets (the more common case)
	 * and from Tx path when we send DHCP packets, the latter is
	 * very unlikely.
	 * Take the lock already here to make sure we see that remove()
	 * might have cleared the IWL_MEI_STATUS_SAP_CONNECTED bit.
	 */
	spin_lock_bh(&mei->data_q_lock);

	if (!iwl_mei_is_connected()) {
		spin_unlock_bh(&mei->data_q_lock);
		return;
	}

	/*
	 * We are in a RCU critical section and the remove from the CSME bus
	 * which would free this memory waits for the readers to complete (this
	 * is done in netdev_rx_handler_unregister).
	 */
	dir = &mei->shared_mem.ctrl->dir[SAP_DIRECTION_HOST_TO_ME];
	notif_q = &dir->q_ctrl_blk[SAP_QUEUE_IDX_DATA];
	q_head = mei->shared_mem.q_head[SAP_DIRECTION_HOST_TO_ME][SAP_QUEUE_IDX_DATA];
	q_sz = mei->shared_mem.q_size[SAP_DIRECTION_HOST_TO_ME][SAP_QUEUE_IDX_DATA];

	rd = le32_to_cpu(READ_ONCE(notif_q->rd_ptr));
	wr = le32_to_cpu(READ_ONCE(notif_q->wr_ptr));
	hdr_sz = cb_tx ? sizeof(struct iwl_sap_cb_data) :
			 sizeof(struct iwl_sap_hdr);
	tx_sz = skb->len + hdr_sz;

	if (rd > q_sz || wr > q_sz) {
		dev_err(&mei->cldev->dev,
			"can't write the data: pointers are past the end of the buffer\n");
		goto out;
	}

	room_in_buf = wr >= rd ? q_sz - wr + rd : rd - wr;

	/* we don't have enough room for the data to write */
	if (room_in_buf < tx_sz) {
		dev_err(&mei->cldev->dev,
			"Not enough room in the buffer for this data\n");
		goto out;
	}

	if (skb_headroom(skb) < hdr_sz) {
		dev_err(&mei->cldev->dev,
			"Not enough headroom in the skb to write the SAP header\n");
		goto out;
	}

	if (cb_tx) {
		struct iwl_sap_cb_data *cb_hdr = skb_push(skb, sizeof(*cb_hdr));

		memset(cb_hdr, 0, sizeof(*cb_hdr));
		cb_hdr->hdr.type = cpu_to_le16(SAP_MSG_CB_DATA_PACKET);
		cb_hdr->hdr.len = cpu_to_le16(skb->len - sizeof(cb_hdr->hdr));
		cb_hdr->hdr.seq_num = cpu_to_le32(atomic_inc_return(&mei->sap_seq_no));
		cb_hdr->to_me_filt_status = cpu_to_le32(BIT(CB_TX_DHCP_FILT_IDX));
		cb_hdr->data_len = cpu_to_le32(skb->len - sizeof(*cb_hdr));
		trace_iwlmei_sap_data(skb, IWL_SAP_TX_DHCP);
	} else {
		struct iwl_sap_hdr *hdr = skb_push(skb, sizeof(*hdr));

		hdr->type = cpu_to_le16(SAP_MSG_DATA_PACKET);
		hdr->len = cpu_to_le16(skb->len - sizeof(*hdr));
		hdr->seq_num = cpu_to_le32(atomic_inc_return(&mei->sap_seq_no));
		trace_iwlmei_sap_data(skb, IWL_SAP_TX_DATA_FROM_AIR);
	}

	if (wr + tx_sz <= q_sz) {
		skb_copy_bits(skb, 0, q_head + wr, tx_sz);
	} else {
		skb_copy_bits(skb, 0, q_head + wr, q_sz - wr);
		skb_copy_bits(skb, q_sz - wr, q_head, tx_sz - (q_sz - wr));
	}

	WRITE_ONCE(notif_q->wr_ptr, cpu_to_le32((wr + tx_sz) % q_sz));

out:
	spin_unlock_bh(&mei->data_q_lock);
}

static int
iwl_mei_send_sap_msg(struct mei_cl_device *cldev, u16 type)
{
	struct iwl_sap_hdr msg = {
		.type = cpu_to_le16(type),
	};

	return iwl_mei_send_sap_msg_payload(cldev, &msg);
}

static void iwl_mei_send_csa_msg_wk(struct work_struct *wk)
{
	struct iwl_mei *mei =
		container_of(wk, struct iwl_mei, send_csa_msg_wk);

	if (!iwl_mei_is_connected())
		return;

	mutex_lock(&iwl_mei_mutex);

	iwl_mei_send_check_shared_area(mei->cldev);

	mutex_unlock(&iwl_mei_mutex);
}

/* Called in a RCU read critical section from netif_receive_skb */
static rx_handler_result_t iwl_mei_rx_handler(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct iwl_mei *mei =
		rcu_dereference(skb->dev->rx_handler_data);
	struct iwl_mei_filters *filters = rcu_dereference(mei->filters);
	bool rx_for_csme = false;
	rx_handler_result_t res;

	/*
	 * remove() unregisters this handler and synchronize_net, so this
	 * should never happen.
	 */
	if (!iwl_mei_is_connected()) {
		dev_err(&mei->cldev->dev,
			"Got an Rx packet, but we're not connected to SAP?\n");
		return RX_HANDLER_PASS;
	}

	if (filters)
		res = iwl_mei_rx_filter(skb, &filters->filters, &rx_for_csme);
	else
		res = RX_HANDLER_PASS;

	/*
	 * The data is already on the ring of the shared area, all we
	 * need to do is to tell the CSME firmware to check what we have
	 * there.
	 */
	if (rx_for_csme)
		schedule_work(&mei->send_csa_msg_wk);

	if (res != RX_HANDLER_PASS) {
		trace_iwlmei_sap_data(skb, IWL_SAP_RX_DATA_DROPPED_FROM_AIR);
		dev_kfree_skb(skb);
	}

	return res;
}

static void iwl_mei_netdev_work(struct work_struct *wk)
{
	struct iwl_mei *mei =
		container_of(wk, struct iwl_mei, netdev_work);
	struct net_device *netdev;

	/*
	 * First take rtnl and only then the mutex to avoid an ABBA
	 * with iwl_mei_set_netdev()
	 */
	rtnl_lock();
	mutex_lock(&iwl_mei_mutex);

	netdev = rcu_dereference_protected(iwl_mei_cache.netdev,
					   lockdep_is_held(&iwl_mei_mutex));
	if (netdev) {
		if (mei->amt_enabled)
			netdev_rx_handler_register(netdev, iwl_mei_rx_handler,
						   mei);
		else
			netdev_rx_handler_unregister(netdev);
	}

	mutex_unlock(&iwl_mei_mutex);
	rtnl_unlock();
}

static void
iwl_mei_handle_rx_start_ok(struct mei_cl_device *cldev,
			   const struct iwl_sap_me_msg_start_ok *rsp,
			   ssize_t len)
{
	if (len != sizeof(*rsp)) {
		dev_err(&cldev->dev,
			"got invalid SAP_ME_MSG_START_OK from CSME firmware\n");
		dev_err(&cldev->dev,
			"size is incorrect: %zd instead of %zu\n",
			len, sizeof(*rsp));
		return;
	}

	if (rsp->supported_version != SAP_VERSION) {
		dev_err(&cldev->dev,
			"didn't get the expected version: got %d\n",
			rsp->supported_version);
		return;
	}

	mutex_lock(&iwl_mei_mutex);
	set_bit(IWL_MEI_STATUS_SAP_CONNECTED, &iwl_mei_status);
	/*
	 * We'll receive AMT_STATE SAP message in a bit and
	 * that will continue the flow
	 */
	mutex_unlock(&iwl_mei_mutex);
}

static void iwl_mei_handle_csme_filters(struct mei_cl_device *cldev,
					const struct iwl_sap_csme_filters *filters)
{
	struct iwl_mei *mei = mei_cldev_get_drvdata(iwl_mei_global_cldev);
	struct iwl_mei_filters *new_filters;
	struct iwl_mei_filters *old_filters;

	old_filters =
		rcu_dereference_protected(mei->filters,
					  lockdep_is_held(&iwl_mei_mutex));

	new_filters = kzalloc(sizeof(*new_filters), GFP_KERNEL);
	if (!new_filters)
		return;

	/* Copy the OOB filters */
	new_filters->filters = filters->filters;

	rcu_assign_pointer(mei->filters, new_filters);

	if (old_filters)
		kfree_rcu(old_filters, rcu_head);
}

static void
iwl_mei_handle_conn_status(struct mei_cl_device *cldev,
			   const struct iwl_sap_notif_conn_status *status)
{
	struct iwl_mei *mei = mei_cldev_get_drvdata(cldev);
	struct iwl_mei_conn_info conn_info = {
		.lp_state = le32_to_cpu(status->link_prot_state),
		.ssid_len = le32_to_cpu(status->conn_info.ssid_len),
		.channel = status->conn_info.channel,
		.band = status->conn_info.band,
		.auth_mode = le32_to_cpu(status->conn_info.auth_mode),
		.pairwise_cipher = le32_to_cpu(status->conn_info.pairwise_cipher),
	};

	if (!iwl_mei_cache.ops ||
	    conn_info.ssid_len > ARRAY_SIZE(conn_info.ssid))
		return;

	memcpy(conn_info.ssid, status->conn_info.ssid, conn_info.ssid_len);
	ether_addr_copy(conn_info.bssid, status->conn_info.bssid);

	iwl_mei_cache.ops->me_conn_status(iwl_mei_cache.priv, &conn_info);

	mei->link_prot_state = status->link_prot_state;

	/*
	 * Update the Rfkill state in case the host does not own the device:
	 * if we are in Link Protection, ask to not touch the device, else,
	 * unblock rfkill.
	 * If the host owns the device, inform the user space whether it can
	 * roam.
	 */
	if (mei->got_ownership)
		iwl_mei_cache.ops->roaming_forbidden(iwl_mei_cache.priv,
						     status->link_prot_state);
	else
		iwl_mei_cache.ops->rfkill(iwl_mei_cache.priv,
					  status->link_prot_state);
}

static void iwl_mei_set_init_conf(struct iwl_mei *mei)
{
	struct iwl_sap_notif_host_link_up link_msg = {
		.hdr.type = cpu_to_le16(SAP_MSG_NOTIF_HOST_LINK_UP),
		.hdr.len = cpu_to_le16(sizeof(link_msg) - sizeof(link_msg.hdr)),
	};
	struct iwl_sap_notif_country_code mcc_msg = {
		.hdr.type = cpu_to_le16(SAP_MSG_NOTIF_COUNTRY_CODE),
		.hdr.len = cpu_to_le16(sizeof(mcc_msg) - sizeof(mcc_msg.hdr)),
		.mcc = cpu_to_le16(iwl_mei_cache.mcc),
	};
	struct iwl_sap_notif_sar_limits sar_msg = {
		.hdr.type = cpu_to_le16(SAP_MSG_NOTIF_SAR_LIMITS),
		.hdr.len = cpu_to_le16(sizeof(sar_msg) - sizeof(sar_msg.hdr)),
	};
	struct iwl_sap_notif_host_nic_info nic_info_msg = {
		.hdr.type = cpu_to_le16(SAP_MSG_NOTIF_NIC_INFO),
		.hdr.len = cpu_to_le16(sizeof(nic_info_msg) - sizeof(nic_info_msg.hdr)),
	};
	struct iwl_sap_msg_dw rfkill_msg = {
		.hdr.type = cpu_to_le16(SAP_MSG_NOTIF_RADIO_STATE),
		.hdr.len = cpu_to_le16(sizeof(rfkill_msg) - sizeof(rfkill_msg.hdr)),
		.val = cpu_to_le32(iwl_mei_cache.rf_kill),
	};

	/* wifi driver has registered already */
	if (iwl_mei_cache.ops) {
		iwl_mei_send_sap_msg(mei->cldev,
				     SAP_MSG_NOTIF_WIFIDR_UP);
		iwl_mei_cache.ops->sap_connected(iwl_mei_cache.priv);
	}

	iwl_mei_send_sap_msg(mei->cldev, SAP_MSG_NOTIF_WHO_OWNS_NIC);

	if (iwl_mei_cache.conn_info) {
		link_msg.conn_info = *iwl_mei_cache.conn_info;
		iwl_mei_send_sap_msg_payload(mei->cldev, &link_msg.hdr);
	}

	iwl_mei_send_sap_msg_payload(mei->cldev, &mcc_msg.hdr);

	if (iwl_mei_cache.power_limit) {
		memcpy(sar_msg.sar_chain_info_table, iwl_mei_cache.power_limit,
		       sizeof(sar_msg.sar_chain_info_table));
		iwl_mei_send_sap_msg_payload(mei->cldev, &sar_msg.hdr);
	}

	ether_addr_copy(nic_info_msg.mac_address, iwl_mei_cache.mac_address);
	ether_addr_copy(nic_info_msg.nvm_address, iwl_mei_cache.nvm_address);
	iwl_mei_send_sap_msg_payload(mei->cldev, &nic_info_msg.hdr);

	iwl_mei_send_sap_msg_payload(mei->cldev, &rfkill_msg.hdr);
}

static void iwl_mei_handle_amt_state(struct mei_cl_device *cldev,
				     const struct iwl_sap_msg_dw *dw)
{
	struct iwl_mei *mei = mei_cldev_get_drvdata(cldev);

	mutex_lock(&iwl_mei_mutex);

	if (mei->amt_enabled == !!le32_to_cpu(dw->val))
		goto out;

	mei->amt_enabled = dw->val;

	if (mei->amt_enabled)
		iwl_mei_set_init_conf(mei);
	else if (iwl_mei_cache.ops)
		iwl_mei_cache.ops->rfkill(iwl_mei_cache.priv, false);

	schedule_work(&mei->netdev_work);

out:
	mutex_unlock(&iwl_mei_mutex);
}

static void iwl_mei_handle_nic_owner(struct mei_cl_device *cldev,
				     const struct iwl_sap_msg_dw *dw)
{
	struct iwl_mei *mei = mei_cldev_get_drvdata(cldev);

	mei->got_ownership = dw->val != cpu_to_le32(SAP_NIC_OWNER_ME);
}

static void iwl_mei_handle_can_release_ownership(struct mei_cl_device *cldev,
						 const void *payload)
{
	/* We can get ownership and driver is registered, go ahead */
	if (iwl_mei_cache.ops)
		iwl_mei_send_sap_msg(cldev,
				     SAP_MSG_NOTIF_HOST_ASKS_FOR_NIC_OWNERSHIP);
}

static void iwl_mei_handle_csme_taking_ownership(struct mei_cl_device *cldev,
						 const void *payload)
{
	struct iwl_mei *mei = mei_cldev_get_drvdata(cldev);

	dev_info(&cldev->dev, "CSME takes ownership\n");

	mei->got_ownership = false;

	if (iwl_mei_cache.ops && !mei->device_down) {
		/*
		 * Remember to send CSME_OWNERSHIP_CONFIRMED when the wifi
		 * driver is finished taking the device down.
		 */
		mei->csme_taking_ownership = true;

		iwl_mei_cache.ops->rfkill(iwl_mei_cache.priv, true);
	} else {
		iwl_mei_send_sap_msg(cldev,
				     SAP_MSG_NOTIF_CSME_OWNERSHIP_CONFIRMED);
	}
}

static void iwl_mei_handle_nvm(struct mei_cl_device *cldev,
			       const struct iwl_sap_nvm *sap_nvm)
{
	struct iwl_mei *mei = mei_cldev_get_drvdata(cldev);
	const struct iwl_mei_nvm *mei_nvm = (const void *)sap_nvm;
	int i;

	kfree(mei->nvm);
	mei->nvm = kzalloc(sizeof(*mei_nvm), GFP_KERNEL);
	if (!mei->nvm)
		return;

	ether_addr_copy(mei->nvm->hw_addr, sap_nvm->hw_addr);
	mei->nvm->n_hw_addrs = sap_nvm->n_hw_addrs;
	mei->nvm->radio_cfg = le32_to_cpu(sap_nvm->radio_cfg);
	mei->nvm->caps = le32_to_cpu(sap_nvm->caps);
	mei->nvm->nvm_version = le32_to_cpu(sap_nvm->nvm_version);

	for (i = 0; i < ARRAY_SIZE(mei->nvm->channels); i++)
		mei->nvm->channels[i] = le32_to_cpu(sap_nvm->channels[i]);

	wake_up_all(&mei->get_nvm_wq);
}

static void iwl_mei_handle_rx_host_own_req(struct mei_cl_device *cldev,
					   const struct iwl_sap_msg_dw *dw)
{
	struct iwl_mei *mei = mei_cldev_get_drvdata(cldev);

	/*
	 * This means that we can't use the wifi device right now, CSME is not
	 * ready to let us use it.
	 */
	if (!dw->val) {
		dev_info(&cldev->dev, "Ownership req denied\n");
		return;
	}

	mei->got_ownership = true;
	wake_up_all(&mei->get_ownership_wq);

	iwl_mei_send_sap_msg(cldev,
			     SAP_MSG_NOTIF_HOST_OWNERSHIP_CONFIRMED);

	/* We can now start the connection, unblock rfkill */
	if (iwl_mei_cache.ops)
		iwl_mei_cache.ops->rfkill(iwl_mei_cache.priv, false);
}

static void iwl_mei_handle_ping(struct mei_cl_device *cldev,
				const struct iwl_sap_hdr *hdr)
{
	iwl_mei_send_sap_msg(cldev, SAP_MSG_NOTIF_PONG);
}

static void iwl_mei_handle_sap_msg(struct mei_cl_device *cldev,
				   const struct iwl_sap_hdr *hdr)
{
	u16 len = le16_to_cpu(hdr->len) + sizeof(*hdr);
	u16 type = le16_to_cpu(hdr->type);

	dev_dbg(&cldev->dev,
		"Got a new SAP message: type %d, len %d, seq %d\n",
		le16_to_cpu(hdr->type), len,
		le32_to_cpu(hdr->seq_num));

#define SAP_MSG_HANDLER(_cmd, _handler, _sz)				\
	case SAP_MSG_NOTIF_ ## _cmd:					\
		if (len < _sz) {					\
			dev_err(&cldev->dev,				\
				"Bad size for %d: %u < %u\n",		\
				le16_to_cpu(hdr->type),			\
				(unsigned int)len,			\
				(unsigned int)_sz);			\
			break;						\
		}							\
		mutex_lock(&iwl_mei_mutex);				\
		_handler(cldev, (const void *)hdr);			\
		mutex_unlock(&iwl_mei_mutex);				\
		break

#define SAP_MSG_HANDLER_NO_LOCK(_cmd, _handler, _sz)			\
	case SAP_MSG_NOTIF_ ## _cmd:					\
		if (len < _sz) {					\
			dev_err(&cldev->dev,				\
				"Bad size for %d: %u < %u\n",		\
				le16_to_cpu(hdr->type),			\
				(unsigned int)len,			\
				(unsigned int)_sz);			\
			break;						\
		}							\
		_handler(cldev, (const void *)hdr);			\
		break

#define SAP_MSG_HANDLER_NO_HANDLER(_cmd, _sz)				\
	case SAP_MSG_NOTIF_ ## _cmd:					\
		if (len < _sz) {					\
			dev_err(&cldev->dev,				\
				"Bad size for %d: %u < %u\n",		\
				le16_to_cpu(hdr->type),			\
				(unsigned int)len,			\
				(unsigned int)_sz);			\
			break;						\
		}							\
		break

	switch (type) {
	SAP_MSG_HANDLER(PING, iwl_mei_handle_ping, 0);
	SAP_MSG_HANDLER(CSME_FILTERS,
			iwl_mei_handle_csme_filters,
			sizeof(struct iwl_sap_csme_filters));
	SAP_MSG_HANDLER(CSME_CONN_STATUS,
			iwl_mei_handle_conn_status,
			sizeof(struct iwl_sap_notif_conn_status));
	SAP_MSG_HANDLER_NO_LOCK(AMT_STATE,
				iwl_mei_handle_amt_state,
				sizeof(struct iwl_sap_msg_dw));
	SAP_MSG_HANDLER_NO_HANDLER(PONG, 0);
	SAP_MSG_HANDLER(NVM, iwl_mei_handle_nvm,
			sizeof(struct iwl_sap_nvm));
	SAP_MSG_HANDLER(CSME_REPLY_TO_HOST_OWNERSHIP_REQ,
			iwl_mei_handle_rx_host_own_req,
			sizeof(struct iwl_sap_msg_dw));
	SAP_MSG_HANDLER(NIC_OWNER, iwl_mei_handle_nic_owner,
			sizeof(struct iwl_sap_msg_dw));
	SAP_MSG_HANDLER(CSME_CAN_RELEASE_OWNERSHIP,
			iwl_mei_handle_can_release_ownership, 0);
	SAP_MSG_HANDLER(CSME_TAKING_OWNERSHIP,
			iwl_mei_handle_csme_taking_ownership, 0);
	default:
	/*
	 * This is not really an error, there are message that we decided
	 * to ignore, yet, it is useful to be able to leave a note if debug
	 * is enabled.
	 */
	dev_dbg(&cldev->dev, "Unsupported message: type %d, len %d\n",
		le16_to_cpu(hdr->type), len);
	}

#undef SAP_MSG_HANDLER
#undef SAP_MSG_HANDLER_NO_LOCK
}

static void iwl_mei_read_from_q(const u8 *q_head, u32 q_sz,
				u32 *_rd, u32 wr,
				void *_buf, u32 len)
{
	u8 *buf = _buf;
	u32 rd = *_rd;

	if (rd + len <= q_sz) {
		memcpy(buf, q_head + rd, len);
		rd += len;
	} else {
		memcpy(buf, q_head + rd, q_sz - rd);
		memcpy(buf + q_sz - rd, q_head, len - (q_sz - rd));
		rd = len - (q_sz - rd);
	}

	*_rd = rd;
}

#define QOS_HDR_IV_SNAP_LEN (sizeof(struct ieee80211_qos_hdr) +      \
			     IEEE80211_TKIP_IV_LEN +                 \
			     sizeof(rfc1042_header) + ETH_TLEN)

static void iwl_mei_handle_sap_data(struct mei_cl_device *cldev,
				    const u8 *q_head, u32 q_sz,
				    u32 rd, u32 wr, ssize_t valid_rx_sz,
				    struct sk_buff_head *tx_skbs)
{
	struct iwl_sap_hdr hdr;
	struct net_device *netdev =
		rcu_dereference_protected(iwl_mei_cache.netdev,
					  lockdep_is_held(&iwl_mei_mutex));

	if (!netdev)
		return;

	while (valid_rx_sz >= sizeof(hdr)) {
		struct ethhdr *ethhdr;
		unsigned char *data;
		struct sk_buff *skb;
		u16 len;

		iwl_mei_read_from_q(q_head, q_sz, &rd, wr, &hdr, sizeof(hdr));
		valid_rx_sz -= sizeof(hdr);
		len = le16_to_cpu(hdr.len);

		if (valid_rx_sz < len) {
			dev_err(&cldev->dev,
				"Data queue is corrupted: valid data len %zd, len %d\n",
				valid_rx_sz, len);
			break;
		}

		if (len < sizeof(*ethhdr)) {
			dev_err(&cldev->dev,
				"Data len is smaller than an ethernet header? len = %d\n",
				len);
		}

		valid_rx_sz -= len;

		if (le16_to_cpu(hdr.type) != SAP_MSG_DATA_PACKET) {
			dev_err(&cldev->dev, "Unsupported Rx data: type %d, len %d\n",
				le16_to_cpu(hdr.type), len);
			continue;
		}

		/* We need enough room for the WiFi header + SNAP + IV */
		skb = netdev_alloc_skb(netdev, len + QOS_HDR_IV_SNAP_LEN);
		if (!skb)
			continue;

		skb_reserve(skb, QOS_HDR_IV_SNAP_LEN);
		ethhdr = skb_push(skb, sizeof(*ethhdr));

		iwl_mei_read_from_q(q_head, q_sz, &rd, wr,
				    ethhdr, sizeof(*ethhdr));
		len -= sizeof(*ethhdr);

		skb_reset_mac_header(skb);
		skb_reset_network_header(skb);
		skb->protocol = ethhdr->h_proto;

		data = skb_put(skb, len);
		iwl_mei_read_from_q(q_head, q_sz, &rd, wr, data, len);

		/*
		 * Enqueue the skb here so that it can be sent later when we
		 * do not hold the mutex. TX'ing a packet with a mutex held is
		 * possible, but it wouldn't be nice to forbid the TX path to
		 * call any of iwlmei's functions, since every API from iwlmei
		 * needs the mutex.
		 */
		__skb_queue_tail(tx_skbs, skb);
	}
}

static void iwl_mei_handle_sap_rx_cmd(struct mei_cl_device *cldev,
				      const u8 *q_head, u32 q_sz,
				      u32 rd, u32 wr, ssize_t valid_rx_sz)
{
	struct page *p = alloc_page(GFP_KERNEL);
	struct iwl_sap_hdr *hdr;

	if (!p)
		return;

	hdr = page_address(p);

	while (valid_rx_sz >= sizeof(*hdr)) {
		u16 len;

		iwl_mei_read_from_q(q_head, q_sz, &rd, wr, hdr, sizeof(*hdr));
		valid_rx_sz -= sizeof(*hdr);
		len = le16_to_cpu(hdr->len);

		if (valid_rx_sz < len)
			break;

		iwl_mei_read_from_q(q_head, q_sz, &rd, wr, hdr + 1, len);

		trace_iwlmei_sap_cmd(hdr, false);
		iwl_mei_handle_sap_msg(cldev, hdr);
		valid_rx_sz -= len;
	}

	/* valid_rx_sz must be 0 now... */
	if (valid_rx_sz)
		dev_err(&cldev->dev,
			"More data in the buffer although we read it all\n");

	__free_page(p);
}

static void iwl_mei_handle_sap_rx(struct mei_cl_device *cldev,
				  struct iwl_sap_q_ctrl_blk *notif_q,
				  const u8 *q_head,
				  struct sk_buff_head *skbs,
				  u32 q_sz)
{
	u32 rd = le32_to_cpu(READ_ONCE(notif_q->rd_ptr));
	u32 wr = le32_to_cpu(READ_ONCE(notif_q->wr_ptr));
	ssize_t valid_rx_sz;

	if (rd > q_sz || wr > q_sz) {
		dev_err(&cldev->dev,
			"Pointers are past the buffer limit\n");
		return;
	}

	if (rd == wr)
		return;

	valid_rx_sz = wr > rd ? wr - rd : q_sz - rd + wr;

	if (skbs)
		iwl_mei_handle_sap_data(cldev, q_head, q_sz, rd, wr,
					valid_rx_sz, skbs);
	else
		iwl_mei_handle_sap_rx_cmd(cldev, q_head, q_sz, rd, wr,
					  valid_rx_sz);

	/* Increment the read pointer to point to the write pointer */
	WRITE_ONCE(notif_q->rd_ptr, cpu_to_le32(wr));
}

static void iwl_mei_handle_check_shared_area(struct mei_cl_device *cldev)
{
	struct iwl_mei *mei = mei_cldev_get_drvdata(cldev);
	struct iwl_sap_q_ctrl_blk *notif_q;
	struct sk_buff_head tx_skbs;
	struct iwl_sap_dir *dir;
	void *q_head;
	u32 q_sz;

	if (!mei->shared_mem.ctrl)
		return;

	dir = &mei->shared_mem.ctrl->dir[SAP_DIRECTION_ME_TO_HOST];
	notif_q = &dir->q_ctrl_blk[SAP_QUEUE_IDX_NOTIF];
	q_head = mei->shared_mem.q_head[SAP_DIRECTION_ME_TO_HOST][SAP_QUEUE_IDX_NOTIF];
	q_sz = mei->shared_mem.q_size[SAP_DIRECTION_ME_TO_HOST][SAP_QUEUE_IDX_NOTIF];

	/*
	 * Do not hold the mutex here, but rather each and every message
	 * handler takes it.
	 * This allows message handlers to take it at a certain time.
	 */
	iwl_mei_handle_sap_rx(cldev, notif_q, q_head, NULL, q_sz);

	mutex_lock(&iwl_mei_mutex);
	dir = &mei->shared_mem.ctrl->dir[SAP_DIRECTION_ME_TO_HOST];
	notif_q = &dir->q_ctrl_blk[SAP_QUEUE_IDX_DATA];
	q_head = mei->shared_mem.q_head[SAP_DIRECTION_ME_TO_HOST][SAP_QUEUE_IDX_DATA];
	q_sz = mei->shared_mem.q_size[SAP_DIRECTION_ME_TO_HOST][SAP_QUEUE_IDX_DATA];

	__skb_queue_head_init(&tx_skbs);

	iwl_mei_handle_sap_rx(cldev, notif_q, q_head, &tx_skbs, q_sz);

	if (skb_queue_empty(&tx_skbs)) {
		mutex_unlock(&iwl_mei_mutex);
		return;
	}

	/*
	 * Take the RCU read lock before we unlock the mutex to make sure that
	 * even if the netdev is replaced by another non-NULL netdev right after
	 * we unlock the mutex, the old netdev will still be valid when we
	 * transmit the frames. We can't allow to replace the netdev here because
	 * the skbs hold a pointer to the netdev.
	 */
	rcu_read_lock();

	mutex_unlock(&iwl_mei_mutex);

	if (!rcu_access_pointer(iwl_mei_cache.netdev)) {
		dev_err(&cldev->dev, "Can't Tx without a netdev\n");
		skb_queue_purge(&tx_skbs);
		goto out;
	}

	while (!skb_queue_empty(&tx_skbs)) {
		struct sk_buff *skb = __skb_dequeue(&tx_skbs);

		trace_iwlmei_sap_data(skb, IWL_SAP_RX_DATA_TO_AIR);
		dev_queue_xmit(skb);
	}

out:
	rcu_read_unlock();
}

static void iwl_mei_rx(struct mei_cl_device *cldev)
{
	struct iwl_sap_me_msg_hdr *hdr;
	u8 msg[100];
	ssize_t ret;

	ret = mei_cldev_recv(cldev, (u8 *)&msg, sizeof(msg));
	if (ret < 0) {
		dev_err(&cldev->dev, "failed to receive data: %zd\n", ret);
		return;
	}

	if (ret == 0) {
		dev_err(&cldev->dev, "got an empty response\n");
		return;
	}

	hdr = (void *)msg;
	trace_iwlmei_me_msg(hdr, false);

	switch (le32_to_cpu(hdr->type)) {
	case SAP_ME_MSG_START_OK:
		BUILD_BUG_ON(sizeof(struct iwl_sap_me_msg_start_ok) >
			     sizeof(msg));

		iwl_mei_handle_rx_start_ok(cldev, (void *)msg, ret);
		break;
	case SAP_ME_MSG_CHECK_SHARED_AREA:
		iwl_mei_handle_check_shared_area(cldev);
		break;
	default:
		dev_err(&cldev->dev, "got a RX notification: %d\n",
			le32_to_cpu(hdr->type));
		break;
	}
}

static int iwl_mei_send_start(struct mei_cl_device *cldev)
{
	struct iwl_mei *mei = mei_cldev_get_drvdata(cldev);
	struct iwl_sap_me_msg_start msg = {
		.hdr.type = cpu_to_le32(SAP_ME_MSG_START),
		.hdr.seq_num = cpu_to_le32(atomic_inc_return(&mei->seq_no)),
		.hdr.len = cpu_to_le32(sizeof(msg)),
		.supported_versions[0] = SAP_VERSION,
		.init_data_seq_num = cpu_to_le16(0x100),
		.init_notif_seq_num = cpu_to_le16(0x800),
	};
	int ret;

	trace_iwlmei_me_msg(&msg.hdr, true);
	ret = mei_cldev_send(cldev, (void *)&msg, sizeof(msg));
	if (ret != sizeof(msg)) {
		dev_err(&cldev->dev,
			"failed to send the SAP_ME_MSG_START message %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int iwl_mei_enable(struct mei_cl_device *cldev)
{
	int ret;

	ret = mei_cldev_enable(cldev);
	if (ret < 0) {
		dev_err(&cldev->dev, "failed to enable the device: %d\n", ret);
		return ret;
	}

	ret = mei_cldev_register_rx_cb(cldev, iwl_mei_rx);
	if (ret) {
		dev_err(&cldev->dev,
			"failed to register to the rx cb: %d\n", ret);
		mei_cldev_disable(cldev);
		return ret;
	}

	return 0;
}

struct iwl_mei_nvm *iwl_mei_get_nvm(void)
{
	struct iwl_mei_nvm *nvm = NULL;
	struct iwl_mei *mei;
	int ret;

	mutex_lock(&iwl_mei_mutex);

	if (!iwl_mei_is_connected())
		goto out;

	mei = mei_cldev_get_drvdata(iwl_mei_global_cldev);

	if (!mei)
		goto out;

	ret = iwl_mei_send_sap_msg(iwl_mei_global_cldev,
				   SAP_MSG_NOTIF_GET_NVM);
	if (ret)
		goto out;

	mutex_unlock(&iwl_mei_mutex);

	ret = wait_event_timeout(mei->get_nvm_wq, mei->nvm, 2 * HZ);
	if (!ret)
		return NULL;

	mutex_lock(&iwl_mei_mutex);

	if (!iwl_mei_is_connected())
		goto out;

	mei = mei_cldev_get_drvdata(iwl_mei_global_cldev);

	if (!mei)
		goto out;

	if (mei->nvm)
		nvm = kmemdup(mei->nvm, sizeof(*mei->nvm), GFP_KERNEL);

out:
	mutex_unlock(&iwl_mei_mutex);
	return nvm;
}
EXPORT_SYMBOL_GPL(iwl_mei_get_nvm);

int iwl_mei_get_ownership(void)
{
	struct iwl_mei *mei;
	int ret;

	mutex_lock(&iwl_mei_mutex);

	/* In case we didn't have a bind */
	if (!iwl_mei_is_connected()) {
		ret = 0;
		goto out;
	}

	mei = mei_cldev_get_drvdata(iwl_mei_global_cldev);

	if (!mei) {
		ret = -ENODEV;
		goto out;
	}

	if (!mei->amt_enabled) {
		ret = 0;
		goto out;
	}

	if (mei->got_ownership) {
		ret = 0;
		goto out;
	}

	ret = iwl_mei_send_sap_msg(mei->cldev,
				   SAP_MSG_NOTIF_HOST_ASKS_FOR_NIC_OWNERSHIP);
	if (ret)
		goto out;

	mutex_unlock(&iwl_mei_mutex);

	ret = wait_event_timeout(mei->get_ownership_wq,
				 mei->got_ownership, HZ / 2);
	if (!ret)
		return -ETIMEDOUT;

	mutex_lock(&iwl_mei_mutex);

	/* In case we didn't have a bind */
	if (!iwl_mei_is_connected()) {
		ret = 0;
		goto out;
	}

	mei = mei_cldev_get_drvdata(iwl_mei_global_cldev);

	if (!mei) {
		ret = -ENODEV;
		goto out;
	}

	ret = !mei->got_ownership;

out:
	mutex_unlock(&iwl_mei_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(iwl_mei_get_ownership);

void iwl_mei_host_associated(const struct iwl_mei_conn_info *conn_info,
			     const struct iwl_mei_colloc_info *colloc_info)
{
	struct iwl_sap_notif_host_link_up msg = {
		.hdr.type = cpu_to_le16(SAP_MSG_NOTIF_HOST_LINK_UP),
		.hdr.len = cpu_to_le16(sizeof(msg) - sizeof(msg.hdr)),
		.conn_info = {
			.ssid_len = cpu_to_le32(conn_info->ssid_len),
			.channel = conn_info->channel,
			.band = conn_info->band,
			.pairwise_cipher = cpu_to_le32(conn_info->pairwise_cipher),
			.auth_mode = cpu_to_le32(conn_info->auth_mode),
		},
	};
	struct iwl_mei *mei;

	if (conn_info->ssid_len > ARRAY_SIZE(msg.conn_info.ssid))
		return;

	memcpy(msg.conn_info.ssid, conn_info->ssid, conn_info->ssid_len);
	memcpy(msg.conn_info.bssid, conn_info->bssid, ETH_ALEN);

	if (colloc_info) {
		msg.colloc_channel = colloc_info->channel;
		msg.colloc_band = colloc_info->channel <= 14 ? 0 : 1;
		memcpy(msg.colloc_bssid, colloc_info->bssid, ETH_ALEN);
	}

	mutex_lock(&iwl_mei_mutex);

	if (!iwl_mei_is_connected())
		goto out;

	mei = mei_cldev_get_drvdata(iwl_mei_global_cldev);

	if (!mei && !mei->amt_enabled)
		goto out;

	iwl_mei_send_sap_msg_payload(mei->cldev, &msg.hdr);

out:
	kfree(iwl_mei_cache.conn_info);
	iwl_mei_cache.conn_info =
		kmemdup(&msg.conn_info, sizeof(msg.conn_info), GFP_KERNEL);
	mutex_unlock(&iwl_mei_mutex);
}
EXPORT_SYMBOL_GPL(iwl_mei_host_associated);

void iwl_mei_host_disassociated(void)
{
	struct iwl_mei *mei;
	struct iwl_sap_notif_host_link_down msg = {
		.hdr.type = cpu_to_le16(SAP_MSG_NOTIF_HOST_LINK_DOWN),
		.hdr.len = cpu_to_le16(sizeof(msg) - sizeof(msg.hdr)),
		.type = HOST_LINK_DOWN_TYPE_LONG,
	};

	mutex_lock(&iwl_mei_mutex);

	if (!iwl_mei_is_connected())
		goto out;

	mei = mei_cldev_get_drvdata(iwl_mei_global_cldev);

	if (!mei && !mei->amt_enabled)
		goto out;

	iwl_mei_send_sap_msg_payload(mei->cldev, &msg.hdr);

out:
	kfree(iwl_mei_cache.conn_info);
	iwl_mei_cache.conn_info = NULL;
	mutex_unlock(&iwl_mei_mutex);
}
EXPORT_SYMBOL_GPL(iwl_mei_host_disassociated);

void iwl_mei_set_rfkill_state(bool hw_rfkill, bool sw_rfkill)
{
	struct iwl_mei *mei;
	u32 rfkill_state = 0;
	struct iwl_sap_msg_dw msg = {
		.hdr.type = cpu_to_le16(SAP_MSG_NOTIF_RADIO_STATE),
		.hdr.len = cpu_to_le16(sizeof(msg) - sizeof(msg.hdr)),
	};

	if (!sw_rfkill)
		rfkill_state |= SAP_SW_RFKILL_DEASSERTED;

	if (!hw_rfkill)
		rfkill_state |= SAP_HW_RFKILL_DEASSERTED;

	mutex_lock(&iwl_mei_mutex);

	if (!iwl_mei_is_connected())
		goto out;

	msg.val = cpu_to_le32(rfkill_state);

	mei = mei_cldev_get_drvdata(iwl_mei_global_cldev);

	if (!mei && !mei->amt_enabled)
		goto out;

	iwl_mei_send_sap_msg_payload(mei->cldev, &msg.hdr);

out:
	iwl_mei_cache.rf_kill = rfkill_state;
	mutex_unlock(&iwl_mei_mutex);
}
EXPORT_SYMBOL_GPL(iwl_mei_set_rfkill_state);

void iwl_mei_set_nic_info(const u8 *mac_address, const u8 *nvm_address)
{
	struct iwl_mei *mei;
	struct iwl_sap_notif_host_nic_info msg = {
		.hdr.type = cpu_to_le16(SAP_MSG_NOTIF_NIC_INFO),
		.hdr.len = cpu_to_le16(sizeof(msg) - sizeof(msg.hdr)),
	};

	mutex_lock(&iwl_mei_mutex);

	if (!iwl_mei_is_connected())
		goto out;

	ether_addr_copy(msg.mac_address, mac_address);
	ether_addr_copy(msg.nvm_address, nvm_address);

	mei = mei_cldev_get_drvdata(iwl_mei_global_cldev);

	if (!mei && !mei->amt_enabled)
		goto out;

	iwl_mei_send_sap_msg_payload(mei->cldev, &msg.hdr);

out:
	ether_addr_copy(iwl_mei_cache.mac_address, mac_address);
	ether_addr_copy(iwl_mei_cache.nvm_address, nvm_address);
	mutex_unlock(&iwl_mei_mutex);
}
EXPORT_SYMBOL_GPL(iwl_mei_set_nic_info);

void iwl_mei_set_country_code(u16 mcc)
{
	struct iwl_mei *mei;
	struct iwl_sap_notif_country_code msg = {
		.hdr.type = cpu_to_le16(SAP_MSG_NOTIF_COUNTRY_CODE),
		.hdr.len = cpu_to_le16(sizeof(msg) - sizeof(msg.hdr)),
		.mcc = cpu_to_le16(mcc),
	};

	mutex_lock(&iwl_mei_mutex);

	if (!iwl_mei_is_connected())
		goto out;

	mei = mei_cldev_get_drvdata(iwl_mei_global_cldev);

	if (!mei && !mei->amt_enabled)
		goto out;

	iwl_mei_send_sap_msg_payload(mei->cldev, &msg.hdr);

out:
	iwl_mei_cache.mcc = mcc;
	mutex_unlock(&iwl_mei_mutex);
}
EXPORT_SYMBOL_GPL(iwl_mei_set_country_code);

void iwl_mei_set_power_limit(const __le16 *power_limit)
{
	struct iwl_mei *mei;
	struct iwl_sap_notif_sar_limits msg = {
		.hdr.type = cpu_to_le16(SAP_MSG_NOTIF_SAR_LIMITS),
		.hdr.len = cpu_to_le16(sizeof(msg) - sizeof(msg.hdr)),
	};

	mutex_lock(&iwl_mei_mutex);

	if (!iwl_mei_is_connected())
		goto out;

	mei = mei_cldev_get_drvdata(iwl_mei_global_cldev);

	if (!mei && !mei->amt_enabled)
		goto out;

	memcpy(msg.sar_chain_info_table, power_limit, sizeof(msg.sar_chain_info_table));

	iwl_mei_send_sap_msg_payload(mei->cldev, &msg.hdr);

out:
	kfree(iwl_mei_cache.power_limit);
	iwl_mei_cache.power_limit = kmemdup(power_limit,
					    sizeof(msg.sar_chain_info_table), GFP_KERNEL);
	mutex_unlock(&iwl_mei_mutex);
}
EXPORT_SYMBOL_GPL(iwl_mei_set_power_limit);

void iwl_mei_set_netdev(struct net_device *netdev)
{
	struct iwl_mei *mei;

	mutex_lock(&iwl_mei_mutex);

	if (!iwl_mei_is_connected()) {
		rcu_assign_pointer(iwl_mei_cache.netdev, netdev);
		goto out;
	}

	mei = mei_cldev_get_drvdata(iwl_mei_global_cldev);

	if (!mei)
		goto out;

	if (!netdev) {
		struct net_device *dev =
			rcu_dereference_protected(iwl_mei_cache.netdev,
						  lockdep_is_held(&iwl_mei_mutex));

		if (!dev)
			goto out;

		netdev_rx_handler_unregister(dev);
	}

	rcu_assign_pointer(iwl_mei_cache.netdev, netdev);

	if (netdev && mei->amt_enabled)
		netdev_rx_handler_register(netdev, iwl_mei_rx_handler, mei);

out:
	mutex_unlock(&iwl_mei_mutex);
}
EXPORT_SYMBOL_GPL(iwl_mei_set_netdev);

void iwl_mei_device_state(bool up)
{
	struct iwl_mei *mei;

	mutex_lock(&iwl_mei_mutex);

	if (!iwl_mei_is_connected())
		goto out;

	mei = mei_cldev_get_drvdata(iwl_mei_global_cldev);

	if (!mei)
		goto out;

	mei->device_down = !up;

	if (up || !mei->csme_taking_ownership)
		goto out;

	iwl_mei_send_sap_msg(mei->cldev,
			     SAP_MSG_NOTIF_CSME_OWNERSHIP_CONFIRMED);
	mei->csme_taking_ownership = false;
out:
	mutex_unlock(&iwl_mei_mutex);
}
EXPORT_SYMBOL_GPL(iwl_mei_device_state);

int iwl_mei_register(void *priv, const struct iwl_mei_ops *ops)
{
	int ret;

	/*
	 * We must have a non-NULL priv pointer to not crash when there are
	 * multiple WiFi devices.
	 */
	if (!priv)
		return -EINVAL;

	mutex_lock(&iwl_mei_mutex);

	/* do not allow registration if someone else already registered */
	if (iwl_mei_cache.priv || iwl_mei_cache.ops) {
		ret = -EBUSY;
		goto out;
	}

	iwl_mei_cache.priv = priv;
	iwl_mei_cache.ops = ops;

	if (iwl_mei_global_cldev) {
		struct iwl_mei *mei =
			mei_cldev_get_drvdata(iwl_mei_global_cldev);

		/* we have already a SAP connection */
		if (iwl_mei_is_connected()) {
			if (mei->amt_enabled)
				iwl_mei_send_sap_msg(mei->cldev,
						     SAP_MSG_NOTIF_WIFIDR_UP);
			ops->rfkill(priv, mei->link_prot_state);
		}
	}
	ret = 0;

out:
	mutex_unlock(&iwl_mei_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(iwl_mei_register);

void iwl_mei_start_unregister(void)
{
	mutex_lock(&iwl_mei_mutex);

	/* At this point, the wifi driver should have removed the netdev */
	if (rcu_access_pointer(iwl_mei_cache.netdev))
		pr_err("Still had a netdev pointer set upon unregister\n");

	kfree(iwl_mei_cache.conn_info);
	iwl_mei_cache.conn_info = NULL;
	kfree(iwl_mei_cache.power_limit);
	iwl_mei_cache.power_limit = NULL;
	iwl_mei_cache.ops = NULL;
	/* leave iwl_mei_cache.priv non-NULL to prevent any new registration */

	mutex_unlock(&iwl_mei_mutex);
}
EXPORT_SYMBOL_GPL(iwl_mei_start_unregister);

void iwl_mei_unregister_complete(void)
{
	mutex_lock(&iwl_mei_mutex);

	iwl_mei_cache.priv = NULL;

	if (iwl_mei_global_cldev) {
		struct iwl_mei *mei =
			mei_cldev_get_drvdata(iwl_mei_global_cldev);

		iwl_mei_send_sap_msg(mei->cldev, SAP_MSG_NOTIF_WIFIDR_DOWN);
		mei->got_ownership = false;
	}

	mutex_unlock(&iwl_mei_mutex);
}
EXPORT_SYMBOL_GPL(iwl_mei_unregister_complete);

#if IS_ENABLED(CONFIG_DEBUG_FS)

static ssize_t
iwl_mei_dbgfs_send_start_message_write(struct file *file,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	int ret;

	mutex_lock(&iwl_mei_mutex);

	if (!iwl_mei_global_cldev) {
		ret = -ENODEV;
		goto out;
	}

	ret = iwl_mei_send_start(iwl_mei_global_cldev);

out:
	mutex_unlock(&iwl_mei_mutex);
	return ret ?: count;
}

static const struct file_operations iwl_mei_dbgfs_send_start_message_ops = {
	.write = iwl_mei_dbgfs_send_start_message_write,
	.open = simple_open,
	.llseek = default_llseek,
};

static ssize_t iwl_mei_dbgfs_req_ownership_write(struct file *file,
						 const char __user *user_buf,
						 size_t count, loff_t *ppos)
{
	iwl_mei_get_ownership();

	return count;
}

static const struct file_operations iwl_mei_dbgfs_req_ownership_ops = {
	.write = iwl_mei_dbgfs_req_ownership_write,
	.open = simple_open,
	.llseek = default_llseek,
};

static void iwl_mei_dbgfs_register(struct iwl_mei *mei)
{
	mei->dbgfs_dir = debugfs_create_dir(KBUILD_MODNAME, NULL);

	if (!mei->dbgfs_dir)
		return;

	debugfs_create_ulong("status", S_IRUSR,
			     mei->dbgfs_dir, &iwl_mei_status);
	debugfs_create_file("send_start_message", S_IWUSR, mei->dbgfs_dir,
			    mei, &iwl_mei_dbgfs_send_start_message_ops);
	debugfs_create_file("req_ownership", S_IWUSR, mei->dbgfs_dir,
			    mei, &iwl_mei_dbgfs_req_ownership_ops);
}

static void iwl_mei_dbgfs_unregister(struct iwl_mei *mei)
{
	debugfs_remove_recursive(mei->dbgfs_dir);
	mei->dbgfs_dir = NULL;
}

#else

static void iwl_mei_dbgfs_register(struct iwl_mei *mei) {}
static void iwl_mei_dbgfs_unregister(struct iwl_mei *mei) {}

#endif /* CONFIG_DEBUG_FS */

#define ALLOC_SHARED_MEM_RETRY_MAX_NUM	3

/*
 * iwl_mei_probe - the probe function called by the mei bus enumeration
 *
 * This allocates the data needed by iwlmei and sets a pointer to this data
 * into the mei_cl_device's drvdata.
 * It starts the SAP protocol by sending the SAP_ME_MSG_START without
 * waiting for the answer. The answer will be caught later by the Rx callback.
 */
static int iwl_mei_probe(struct mei_cl_device *cldev,
			 const struct mei_cl_device_id *id)
{
	int alloc_retry = ALLOC_SHARED_MEM_RETRY_MAX_NUM;
	struct iwl_mei *mei;
	int ret;

	mei = devm_kzalloc(&cldev->dev, sizeof(*mei), GFP_KERNEL);
	if (!mei)
		return -ENOMEM;

	init_waitqueue_head(&mei->get_nvm_wq);
	INIT_WORK(&mei->send_csa_msg_wk, iwl_mei_send_csa_msg_wk);
	INIT_DELAYED_WORK(&mei->csa_throttle_end_wk,
			  iwl_mei_csa_throttle_end_wk);
	init_waitqueue_head(&mei->get_ownership_wq);
	spin_lock_init(&mei->data_q_lock);
	INIT_WORK(&mei->netdev_work, iwl_mei_netdev_work);

	mei_cldev_set_drvdata(cldev, mei);
	mei->cldev = cldev;
	mei->device_down = true;

	do {
		ret = iwl_mei_alloc_shared_mem(cldev);
		if (!ret)
			break;
		/*
		 * The CSME firmware needs to boot the internal WLAN client.
		 * This can take time in certain configurations (usually
		 * upon resume and when the whole CSME firmware is shut down
		 * during suspend).
		 *
		 * Wait a bit before retrying and hope we'll succeed next time.
		 */

		dev_dbg(&cldev->dev,
			"Couldn't allocate the shared memory: %d, attempt %d / %d\n",
			ret, alloc_retry, ALLOC_SHARED_MEM_RETRY_MAX_NUM);
		msleep(100);
		alloc_retry--;
	} while (alloc_retry);

	if (ret) {
		dev_err(&cldev->dev, "Couldn't allocate the shared memory: %d\n",
			ret);
		goto free;
	}

	iwl_mei_init_shared_mem(mei);

	ret = iwl_mei_enable(cldev);
	if (ret)
		goto free_shared_mem;

	iwl_mei_dbgfs_register(mei);

	/*
	 * We now have a Rx function in place, start the SAP procotol
	 * we expect to get the SAP_ME_MSG_START_OK response later on.
	 */
	mutex_lock(&iwl_mei_mutex);
	ret = iwl_mei_send_start(cldev);
	mutex_unlock(&iwl_mei_mutex);
	if (ret)
		goto debugfs_unregister;

	/* must be last */
	iwl_mei_global_cldev = cldev;

	return 0;

debugfs_unregister:
	iwl_mei_dbgfs_unregister(mei);
	mei_cldev_disable(cldev);
free_shared_mem:
	iwl_mei_free_shared_mem(cldev);
free:
	mei_cldev_set_drvdata(cldev, NULL);
	devm_kfree(&cldev->dev, mei);

	return ret;
}

#define SEND_SAP_MAX_WAIT_ITERATION 10

static void iwl_mei_remove(struct mei_cl_device *cldev)
{
	struct iwl_mei *mei = mei_cldev_get_drvdata(cldev);
	int i;

	/*
	 * We are being removed while the bus is active, it means we are
	 * going to suspend/ shutdown, so the NIC will disappear.
	 */
	if (mei_cldev_enabled(cldev) && iwl_mei_cache.ops)
		iwl_mei_cache.ops->nic_stolen(iwl_mei_cache.priv);

	if (rcu_access_pointer(iwl_mei_cache.netdev)) {
		struct net_device *dev;

		/*
		 * First take rtnl and only then the mutex to avoid an ABBA
		 * with iwl_mei_set_netdev()
		 */
		rtnl_lock();
		mutex_lock(&iwl_mei_mutex);

		/*
		 * If we are suspending and the wifi driver hasn't removed it's netdev
		 * yet, do it now. In any case, don't change the cache.netdev pointer.
		 */
		dev = rcu_dereference_protected(iwl_mei_cache.netdev,
						lockdep_is_held(&iwl_mei_mutex));

		netdev_rx_handler_unregister(dev);
		mutex_unlock(&iwl_mei_mutex);
		rtnl_unlock();
	}

	mutex_lock(&iwl_mei_mutex);

	if (mei->amt_enabled) {
		/*
		 * Tell CSME that we are going down so that it won't access the
		 * memory anymore, make sure this message goes through immediately.
		 */
		mei->csa_throttled = false;
		iwl_mei_send_sap_msg(mei->cldev,
				     SAP_MSG_NOTIF_HOST_GOES_DOWN);

		for (i = 0; i < SEND_SAP_MAX_WAIT_ITERATION; i++) {
			if (!iwl_mei_host_to_me_data_pending(mei))
				break;

			msleep(20);
		}

		/*
		 * If we couldn't make sure that CSME saw the HOST_GOES_DOWN
		 * message, it means that it will probably keep reading memory
		 * that we are going to unmap and free, expect IOMMU error
		 * messages.
		 */
		if (i == SEND_SAP_MAX_WAIT_ITERATION)
			dev_err(&mei->cldev->dev,
				"Couldn't get ACK from CSME on HOST_GOES_DOWN message\n");
	}

	mutex_unlock(&iwl_mei_mutex);

	/*
	 * This looks strange, but this lock is taken here to make sure that
	 * iwl_mei_add_data_to_ring called from the Tx path sees that we
	 * clear the IWL_MEI_STATUS_SAP_CONNECTED bit.
	 * Rx isn't a problem because the rx_handler can't be called after
	 * having been unregistered.
	 */
	spin_lock_bh(&mei->data_q_lock);
	clear_bit(IWL_MEI_STATUS_SAP_CONNECTED, &iwl_mei_status);
	spin_unlock_bh(&mei->data_q_lock);

	if (iwl_mei_cache.ops)
		iwl_mei_cache.ops->rfkill(iwl_mei_cache.priv, false);

	/*
	 * mei_cldev_disable will return only after all the MEI Rx is done.
	 * It must be called when iwl_mei_mutex is *not* held, since it waits
	 * for our Rx handler to complete.
	 * After it returns, no new Rx will start.
	 */
	mei_cldev_disable(cldev);

	/*
	 * Since the netdev was already removed and the netdev's removal
	 * includes a call to synchronize_net() so that we know there won't be
	 * any new Rx that will trigger the following workers.
	 */
	cancel_work_sync(&mei->send_csa_msg_wk);
	cancel_delayed_work_sync(&mei->csa_throttle_end_wk);
	cancel_work_sync(&mei->netdev_work);

	/*
	 * If someone waits for the ownership, let him know that we are going
	 * down and that we are not connected anymore. He'll be able to take
	 * the device.
	 */
	wake_up_all(&mei->get_ownership_wq);

	mutex_lock(&iwl_mei_mutex);

	iwl_mei_global_cldev = NULL;

	wake_up_all(&mei->get_nvm_wq);

	iwl_mei_free_shared_mem(cldev);

	iwl_mei_dbgfs_unregister(mei);

	mei_cldev_set_drvdata(cldev, NULL);

	kfree(mei->nvm);

	kfree(rcu_access_pointer(mei->filters));

	devm_kfree(&cldev->dev, mei);

	mutex_unlock(&iwl_mei_mutex);
}

static const struct mei_cl_device_id iwl_mei_tbl[] = {
	{
		.name = KBUILD_MODNAME,
		.uuid = MEI_WLAN_UUID,
		.version = MEI_CL_VERSION_ANY,
	},

	/* required last entry */
	{ }
};

/*
 * Do not export the device table because this module is loaded by
 * iwlwifi's dependency.
 */

static struct mei_cl_driver iwl_mei_cl_driver = {
	.id_table = iwl_mei_tbl,
	.name = KBUILD_MODNAME,
	.probe = iwl_mei_probe,
	.remove = iwl_mei_remove,
};

module_mei_cl_driver(iwl_mei_cl_driver);
