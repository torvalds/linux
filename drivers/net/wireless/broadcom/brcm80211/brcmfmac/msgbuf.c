// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2014 Broadcom Corporation
 */

/*******************************************************************************
 * Communicates with the dongle by using dcmd codes.
 * For certain dcmd codes, the dongle interprets string data from the host.
 ******************************************************************************/

#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include <brcmu_utils.h>
#include <brcmu_wifi.h>

#include "core.h"
#include "debug.h"
#include "proto.h"
#include "msgbuf.h"
#include "commonring.h"
#include "flowring.h"
#include "bus.h"
#include "tracepoint.h"


#define MSGBUF_IOCTL_RESP_TIMEOUT		msecs_to_jiffies(2000)

#define MSGBUF_TYPE_GEN_STATUS			0x1
#define MSGBUF_TYPE_RING_STATUS			0x2
#define MSGBUF_TYPE_FLOW_RING_CREATE		0x3
#define MSGBUF_TYPE_FLOW_RING_CREATE_CMPLT	0x4
#define MSGBUF_TYPE_FLOW_RING_DELETE		0x5
#define MSGBUF_TYPE_FLOW_RING_DELETE_CMPLT	0x6
#define MSGBUF_TYPE_FLOW_RING_FLUSH		0x7
#define MSGBUF_TYPE_FLOW_RING_FLUSH_CMPLT	0x8
#define MSGBUF_TYPE_IOCTLPTR_REQ		0x9
#define MSGBUF_TYPE_IOCTLPTR_REQ_ACK		0xA
#define MSGBUF_TYPE_IOCTLRESP_BUF_POST		0xB
#define MSGBUF_TYPE_IOCTL_CMPLT			0xC
#define MSGBUF_TYPE_EVENT_BUF_POST		0xD
#define MSGBUF_TYPE_WL_EVENT			0xE
#define MSGBUF_TYPE_TX_POST			0xF
#define MSGBUF_TYPE_TX_STATUS			0x10
#define MSGBUF_TYPE_RXBUF_POST			0x11
#define MSGBUF_TYPE_RX_CMPLT			0x12
#define MSGBUF_TYPE_LPBK_DMAXFER		0x13
#define MSGBUF_TYPE_LPBK_DMAXFER_CMPLT		0x14

#define NR_TX_PKTIDS				2048
#define NR_RX_PKTIDS				1024

#define BRCMF_IOCTL_REQ_PKTID			0xFFFE

#define BRCMF_MSGBUF_MAX_PKT_SIZE		2048
#define BRCMF_MSGBUF_RXBUFPOST_THRESHOLD	32
#define BRCMF_MSGBUF_MAX_IOCTLRESPBUF_POST	8
#define BRCMF_MSGBUF_MAX_EVENTBUF_POST		8

#define BRCMF_MSGBUF_PKT_FLAGS_FRAME_802_3	0x01
#define BRCMF_MSGBUF_PKT_FLAGS_FRAME_802_11	0x02
#define BRCMF_MSGBUF_PKT_FLAGS_FRAME_MASK	0x07
#define BRCMF_MSGBUF_PKT_FLAGS_PRIO_SHIFT	5

#define BRCMF_MSGBUF_TX_FLUSH_CNT1		32
#define BRCMF_MSGBUF_TX_FLUSH_CNT2		96

#define BRCMF_MSGBUF_DELAY_TXWORKER_THRS	96
#define BRCMF_MSGBUF_TRICKLE_TXWORKER_THRS	32
#define BRCMF_MSGBUF_UPDATE_RX_PTR_THRS		48


struct msgbuf_common_hdr {
	u8				msgtype;
	u8				ifidx;
	u8				flags;
	u8				rsvd0;
	__le32				request_id;
};

struct msgbuf_ioctl_req_hdr {
	struct msgbuf_common_hdr	msg;
	__le32				cmd;
	__le16				trans_id;
	__le16				input_buf_len;
	__le16				output_buf_len;
	__le16				rsvd0[3];
	struct msgbuf_buf_addr		req_buf_addr;
	__le32				rsvd1[2];
};

struct msgbuf_tx_msghdr {
	struct msgbuf_common_hdr	msg;
	u8				txhdr[ETH_HLEN];
	u8				flags;
	u8				seg_cnt;
	struct msgbuf_buf_addr		metadata_buf_addr;
	struct msgbuf_buf_addr		data_buf_addr;
	__le16				metadata_buf_len;
	__le16				data_len;
	__le32				rsvd0;
};

struct msgbuf_rx_bufpost {
	struct msgbuf_common_hdr	msg;
	__le16				metadata_buf_len;
	__le16				data_buf_len;
	__le32				rsvd0;
	struct msgbuf_buf_addr		metadata_buf_addr;
	struct msgbuf_buf_addr		data_buf_addr;
};

struct msgbuf_rx_ioctl_resp_or_event {
	struct msgbuf_common_hdr	msg;
	__le16				host_buf_len;
	__le16				rsvd0[3];
	struct msgbuf_buf_addr		host_buf_addr;
	__le32				rsvd1[4];
};

struct msgbuf_completion_hdr {
	__le16				status;
	__le16				flow_ring_id;
};

/* Data struct for the MSGBUF_TYPE_GEN_STATUS */
struct msgbuf_gen_status {
	struct msgbuf_common_hdr	msg;
	struct msgbuf_completion_hdr	compl_hdr;
	__le16				write_idx;
	__le32				rsvd0[3];
};

/* Data struct for the MSGBUF_TYPE_RING_STATUS */
struct msgbuf_ring_status {
	struct msgbuf_common_hdr	msg;
	struct msgbuf_completion_hdr	compl_hdr;
	__le16				write_idx;
	__le16				rsvd0[5];
};

struct msgbuf_rx_event {
	struct msgbuf_common_hdr	msg;
	struct msgbuf_completion_hdr	compl_hdr;
	__le16				event_data_len;
	__le16				seqnum;
	__le16				rsvd0[4];
};

struct msgbuf_ioctl_resp_hdr {
	struct msgbuf_common_hdr	msg;
	struct msgbuf_completion_hdr	compl_hdr;
	__le16				resp_len;
	__le16				trans_id;
	__le32				cmd;
	__le32				rsvd0;
};

struct msgbuf_tx_status {
	struct msgbuf_common_hdr	msg;
	struct msgbuf_completion_hdr	compl_hdr;
	__le16				metadata_len;
	__le16				tx_status;
};

struct msgbuf_rx_complete {
	struct msgbuf_common_hdr	msg;
	struct msgbuf_completion_hdr	compl_hdr;
	__le16				metadata_len;
	__le16				data_len;
	__le16				data_offset;
	__le16				flags;
	__le32				rx_status_0;
	__le32				rx_status_1;
	__le32				rsvd0;
};

struct msgbuf_tx_flowring_create_req {
	struct msgbuf_common_hdr	msg;
	u8				da[ETH_ALEN];
	u8				sa[ETH_ALEN];
	u8				tid;
	u8				if_flags;
	__le16				flow_ring_id;
	u8				tc;
	u8				priority;
	__le16				int_vector;
	__le16				max_items;
	__le16				len_item;
	struct msgbuf_buf_addr		flow_ring_addr;
};

struct msgbuf_tx_flowring_delete_req {
	struct msgbuf_common_hdr	msg;
	__le16				flow_ring_id;
	__le16				reason;
	__le32				rsvd0[7];
};

struct msgbuf_flowring_create_resp {
	struct msgbuf_common_hdr	msg;
	struct msgbuf_completion_hdr	compl_hdr;
	__le32				rsvd0[3];
};

struct msgbuf_flowring_delete_resp {
	struct msgbuf_common_hdr	msg;
	struct msgbuf_completion_hdr	compl_hdr;
	__le32				rsvd0[3];
};

struct msgbuf_flowring_flush_resp {
	struct msgbuf_common_hdr	msg;
	struct msgbuf_completion_hdr	compl_hdr;
	__le32				rsvd0[3];
};

struct brcmf_msgbuf_work_item {
	struct list_head queue;
	u32 flowid;
	int ifidx;
	u8 sa[ETH_ALEN];
	u8 da[ETH_ALEN];
};

struct brcmf_msgbuf {
	struct brcmf_pub *drvr;

	struct brcmf_commonring **commonrings;
	struct brcmf_commonring **flowrings;
	dma_addr_t *flowring_dma_handle;

	u16 max_flowrings;
	u16 max_submissionrings;
	u16 max_completionrings;

	u16 rx_dataoffset;
	u32 max_rxbufpost;
	u16 rx_metadata_offset;
	u32 rxbufpost;

	u32 max_ioctlrespbuf;
	u32 cur_ioctlrespbuf;
	u32 max_eventbuf;
	u32 cur_eventbuf;

	void *ioctbuf;
	dma_addr_t ioctbuf_handle;
	u32 ioctbuf_phys_hi;
	u32 ioctbuf_phys_lo;
	int ioctl_resp_status;
	u32 ioctl_resp_ret_len;
	u32 ioctl_resp_pktid;

	u16 data_seq_no;
	u16 ioctl_seq_no;
	u32 reqid;
	wait_queue_head_t ioctl_resp_wait;
	bool ctl_completed;

	struct brcmf_msgbuf_pktids *tx_pktids;
	struct brcmf_msgbuf_pktids *rx_pktids;
	struct brcmf_flowring *flow;

	struct workqueue_struct *txflow_wq;
	struct work_struct txflow_work;
	unsigned long *flow_map;
	unsigned long *txstatus_done_map;

	struct work_struct flowring_work;
	spinlock_t flowring_work_lock;
	struct list_head work_queue;
};

struct brcmf_msgbuf_pktid {
	atomic_t  allocated;
	u16 data_offset;
	struct sk_buff *skb;
	dma_addr_t physaddr;
};

struct brcmf_msgbuf_pktids {
	u32 array_size;
	u32 last_allocated_idx;
	enum dma_data_direction direction;
	struct brcmf_msgbuf_pktid *array;
};

static void brcmf_msgbuf_rxbuf_ioctlresp_post(struct brcmf_msgbuf *msgbuf);


static struct brcmf_msgbuf_pktids *
brcmf_msgbuf_init_pktids(u32 nr_array_entries,
			 enum dma_data_direction direction)
{
	struct brcmf_msgbuf_pktid *array;
	struct brcmf_msgbuf_pktids *pktids;

	array = kcalloc(nr_array_entries, sizeof(*array), GFP_KERNEL);
	if (!array)
		return NULL;

	pktids = kzalloc(sizeof(*pktids), GFP_KERNEL);
	if (!pktids) {
		kfree(array);
		return NULL;
	}
	pktids->array = array;
	pktids->array_size = nr_array_entries;

	return pktids;
}


static int
brcmf_msgbuf_alloc_pktid(struct device *dev,
			 struct brcmf_msgbuf_pktids *pktids,
			 struct sk_buff *skb, u16 data_offset,
			 dma_addr_t *physaddr, u32 *idx)
{
	struct brcmf_msgbuf_pktid *array;
	u32 count;

	array = pktids->array;

	*physaddr = dma_map_single(dev, skb->data + data_offset,
				   skb->len - data_offset, pktids->direction);

	if (dma_mapping_error(dev, *physaddr)) {
		brcmf_err("dma_map_single failed !!\n");
		return -ENOMEM;
	}

	*idx = pktids->last_allocated_idx;

	count = 0;
	do {
		(*idx)++;
		if (*idx == pktids->array_size)
			*idx = 0;
		if (array[*idx].allocated.counter == 0)
			if (atomic_cmpxchg(&array[*idx].allocated, 0, 1) == 0)
				break;
		count++;
	} while (count < pktids->array_size);

	if (count == pktids->array_size)
		return -ENOMEM;

	array[*idx].data_offset = data_offset;
	array[*idx].physaddr = *physaddr;
	array[*idx].skb = skb;

	pktids->last_allocated_idx = *idx;

	return 0;
}


static struct sk_buff *
brcmf_msgbuf_get_pktid(struct device *dev, struct brcmf_msgbuf_pktids *pktids,
		       u32 idx)
{
	struct brcmf_msgbuf_pktid *pktid;
	struct sk_buff *skb;

	if (idx >= pktids->array_size) {
		brcmf_err("Invalid packet id %d (max %d)\n", idx,
			  pktids->array_size);
		return NULL;
	}
	if (pktids->array[idx].allocated.counter) {
		pktid = &pktids->array[idx];
		dma_unmap_single(dev, pktid->physaddr,
				 pktid->skb->len - pktid->data_offset,
				 pktids->direction);
		skb = pktid->skb;
		pktid->allocated.counter = 0;
		return skb;
	} else {
		brcmf_err("Invalid packet id %d (not in use)\n", idx);
	}

	return NULL;
}


static void
brcmf_msgbuf_release_array(struct device *dev,
			   struct brcmf_msgbuf_pktids *pktids)
{
	struct brcmf_msgbuf_pktid *array;
	struct brcmf_msgbuf_pktid *pktid;
	u32 count;

	array = pktids->array;
	count = 0;
	do {
		if (array[count].allocated.counter) {
			pktid = &array[count];
			dma_unmap_single(dev, pktid->physaddr,
					 pktid->skb->len - pktid->data_offset,
					 pktids->direction);
			brcmu_pkt_buf_free_skb(pktid->skb);
		}
		count++;
	} while (count < pktids->array_size);

	kfree(array);
	kfree(pktids);
}


static void brcmf_msgbuf_release_pktids(struct brcmf_msgbuf *msgbuf)
{
	if (msgbuf->rx_pktids)
		brcmf_msgbuf_release_array(msgbuf->drvr->bus_if->dev,
					   msgbuf->rx_pktids);
	if (msgbuf->tx_pktids)
		brcmf_msgbuf_release_array(msgbuf->drvr->bus_if->dev,
					   msgbuf->tx_pktids);
}


static int brcmf_msgbuf_tx_ioctl(struct brcmf_pub *drvr, int ifidx,
				 uint cmd, void *buf, uint len)
{
	struct brcmf_msgbuf *msgbuf = (struct brcmf_msgbuf *)drvr->proto->pd;
	struct brcmf_commonring *commonring;
	struct msgbuf_ioctl_req_hdr *request;
	u16 buf_len;
	void *ret_ptr;
	int err;

	commonring = msgbuf->commonrings[BRCMF_H2D_MSGRING_CONTROL_SUBMIT];
	brcmf_commonring_lock(commonring);
	ret_ptr = brcmf_commonring_reserve_for_write(commonring);
	if (!ret_ptr) {
		bphy_err(drvr, "Failed to reserve space in commonring\n");
		brcmf_commonring_unlock(commonring);
		return -ENOMEM;
	}

	msgbuf->reqid++;

	request = (struct msgbuf_ioctl_req_hdr *)ret_ptr;
	request->msg.msgtype = MSGBUF_TYPE_IOCTLPTR_REQ;
	request->msg.ifidx = (u8)ifidx;
	request->msg.flags = 0;
	request->msg.request_id = cpu_to_le32(BRCMF_IOCTL_REQ_PKTID);
	request->cmd = cpu_to_le32(cmd);
	request->output_buf_len = cpu_to_le16(len);
	request->trans_id = cpu_to_le16(msgbuf->reqid);

	buf_len = min_t(u16, len, BRCMF_TX_IOCTL_MAX_MSG_SIZE);
	request->input_buf_len = cpu_to_le16(buf_len);
	request->req_buf_addr.high_addr = cpu_to_le32(msgbuf->ioctbuf_phys_hi);
	request->req_buf_addr.low_addr = cpu_to_le32(msgbuf->ioctbuf_phys_lo);
	if (buf)
		memcpy(msgbuf->ioctbuf, buf, buf_len);
	else
		memset(msgbuf->ioctbuf, 0, buf_len);

	err = brcmf_commonring_write_complete(commonring);
	brcmf_commonring_unlock(commonring);

	return err;
}


static int brcmf_msgbuf_ioctl_resp_wait(struct brcmf_msgbuf *msgbuf)
{
	return wait_event_timeout(msgbuf->ioctl_resp_wait,
				  msgbuf->ctl_completed,
				  MSGBUF_IOCTL_RESP_TIMEOUT);
}


static void brcmf_msgbuf_ioctl_resp_wake(struct brcmf_msgbuf *msgbuf)
{
	msgbuf->ctl_completed = true;
	wake_up(&msgbuf->ioctl_resp_wait);
}


static int brcmf_msgbuf_query_dcmd(struct brcmf_pub *drvr, int ifidx,
				   uint cmd, void *buf, uint len, int *fwerr)
{
	struct brcmf_msgbuf *msgbuf = (struct brcmf_msgbuf *)drvr->proto->pd;
	struct sk_buff *skb = NULL;
	int timeout;
	int err;

	brcmf_dbg(MSGBUF, "ifidx=%d, cmd=%d, len=%d\n", ifidx, cmd, len);
	*fwerr = 0;
	msgbuf->ctl_completed = false;
	err = brcmf_msgbuf_tx_ioctl(drvr, ifidx, cmd, buf, len);
	if (err)
		return err;

	timeout = brcmf_msgbuf_ioctl_resp_wait(msgbuf);
	if (!timeout) {
		bphy_err(drvr, "Timeout on response for query command\n");
		return -EIO;
	}

	skb = brcmf_msgbuf_get_pktid(msgbuf->drvr->bus_if->dev,
				     msgbuf->rx_pktids,
				     msgbuf->ioctl_resp_pktid);
	if (msgbuf->ioctl_resp_ret_len != 0) {
		if (!skb)
			return -EBADF;

		memcpy(buf, skb->data, (len < msgbuf->ioctl_resp_ret_len) ?
				       len : msgbuf->ioctl_resp_ret_len);
	}
	brcmu_pkt_buf_free_skb(skb);

	*fwerr = msgbuf->ioctl_resp_status;
	return 0;
}


static int brcmf_msgbuf_set_dcmd(struct brcmf_pub *drvr, int ifidx,
				 uint cmd, void *buf, uint len, int *fwerr)
{
	return brcmf_msgbuf_query_dcmd(drvr, ifidx, cmd, buf, len, fwerr);
}


static int brcmf_msgbuf_hdrpull(struct brcmf_pub *drvr, bool do_fws,
				struct sk_buff *skb, struct brcmf_if **ifp)
{
	return -ENODEV;
}

static void brcmf_msgbuf_rxreorder(struct brcmf_if *ifp, struct sk_buff *skb)
{
}

static void
brcmf_msgbuf_remove_flowring(struct brcmf_msgbuf *msgbuf, u16 flowid)
{
	u32 dma_sz;
	void *dma_buf;

	brcmf_dbg(MSGBUF, "Removing flowring %d\n", flowid);

	dma_sz = BRCMF_H2D_TXFLOWRING_MAX_ITEM * BRCMF_H2D_TXFLOWRING_ITEMSIZE;
	dma_buf = msgbuf->flowrings[flowid]->buf_addr;
	dma_free_coherent(msgbuf->drvr->bus_if->dev, dma_sz, dma_buf,
			  msgbuf->flowring_dma_handle[flowid]);

	brcmf_flowring_delete(msgbuf->flow, flowid);
}


static struct brcmf_msgbuf_work_item *
brcmf_msgbuf_dequeue_work(struct brcmf_msgbuf *msgbuf)
{
	struct brcmf_msgbuf_work_item *work = NULL;
	ulong flags;

	spin_lock_irqsave(&msgbuf->flowring_work_lock, flags);
	if (!list_empty(&msgbuf->work_queue)) {
		work = list_first_entry(&msgbuf->work_queue,
					struct brcmf_msgbuf_work_item, queue);
		list_del(&work->queue);
	}
	spin_unlock_irqrestore(&msgbuf->flowring_work_lock, flags);

	return work;
}


static u32
brcmf_msgbuf_flowring_create_worker(struct brcmf_msgbuf *msgbuf,
				    struct brcmf_msgbuf_work_item *work)
{
	struct brcmf_pub *drvr = msgbuf->drvr;
	struct msgbuf_tx_flowring_create_req *create;
	struct brcmf_commonring *commonring;
	void *ret_ptr;
	u32 flowid;
	void *dma_buf;
	u32 dma_sz;
	u64 address;
	int err;

	flowid = work->flowid;
	dma_sz = BRCMF_H2D_TXFLOWRING_MAX_ITEM * BRCMF_H2D_TXFLOWRING_ITEMSIZE;
	dma_buf = dma_alloc_coherent(msgbuf->drvr->bus_if->dev, dma_sz,
				     &msgbuf->flowring_dma_handle[flowid],
				     GFP_KERNEL);
	if (!dma_buf) {
		bphy_err(drvr, "dma_alloc_coherent failed\n");
		brcmf_flowring_delete(msgbuf->flow, flowid);
		return BRCMF_FLOWRING_INVALID_ID;
	}

	brcmf_commonring_config(msgbuf->flowrings[flowid],
				BRCMF_H2D_TXFLOWRING_MAX_ITEM,
				BRCMF_H2D_TXFLOWRING_ITEMSIZE, dma_buf);

	commonring = msgbuf->commonrings[BRCMF_H2D_MSGRING_CONTROL_SUBMIT];
	brcmf_commonring_lock(commonring);
	ret_ptr = brcmf_commonring_reserve_for_write(commonring);
	if (!ret_ptr) {
		bphy_err(drvr, "Failed to reserve space in commonring\n");
		brcmf_commonring_unlock(commonring);
		brcmf_msgbuf_remove_flowring(msgbuf, flowid);
		return BRCMF_FLOWRING_INVALID_ID;
	}

	create = (struct msgbuf_tx_flowring_create_req *)ret_ptr;
	create->msg.msgtype = MSGBUF_TYPE_FLOW_RING_CREATE;
	create->msg.ifidx = work->ifidx;
	create->msg.request_id = 0;
	create->tid = brcmf_flowring_tid(msgbuf->flow, flowid);
	create->flow_ring_id = cpu_to_le16(flowid +
					   BRCMF_H2D_MSGRING_FLOWRING_IDSTART);
	memcpy(create->sa, work->sa, ETH_ALEN);
	memcpy(create->da, work->da, ETH_ALEN);
	address = (u64)msgbuf->flowring_dma_handle[flowid];
	create->flow_ring_addr.high_addr = cpu_to_le32(address >> 32);
	create->flow_ring_addr.low_addr = cpu_to_le32(address & 0xffffffff);
	create->max_items = cpu_to_le16(BRCMF_H2D_TXFLOWRING_MAX_ITEM);
	create->len_item = cpu_to_le16(BRCMF_H2D_TXFLOWRING_ITEMSIZE);

	brcmf_dbg(MSGBUF, "Send Flow Create Req flow ID %d for peer %pM prio %d ifindex %d\n",
		  flowid, work->da, create->tid, work->ifidx);

	err = brcmf_commonring_write_complete(commonring);
	brcmf_commonring_unlock(commonring);
	if (err) {
		bphy_err(drvr, "Failed to write commonring\n");
		brcmf_msgbuf_remove_flowring(msgbuf, flowid);
		return BRCMF_FLOWRING_INVALID_ID;
	}

	return flowid;
}


static void brcmf_msgbuf_flowring_worker(struct work_struct *work)
{
	struct brcmf_msgbuf *msgbuf;
	struct brcmf_msgbuf_work_item *create;

	msgbuf = container_of(work, struct brcmf_msgbuf, flowring_work);

	while ((create = brcmf_msgbuf_dequeue_work(msgbuf))) {
		brcmf_msgbuf_flowring_create_worker(msgbuf, create);
		kfree(create);
	}
}


static u32 brcmf_msgbuf_flowring_create(struct brcmf_msgbuf *msgbuf, int ifidx,
					struct sk_buff *skb)
{
	struct brcmf_msgbuf_work_item *create;
	struct ethhdr *eh = (struct ethhdr *)(skb->data);
	u32 flowid;
	ulong flags;

	create = kzalloc(sizeof(*create), GFP_ATOMIC);
	if (create == NULL)
		return BRCMF_FLOWRING_INVALID_ID;

	flowid = brcmf_flowring_create(msgbuf->flow, eh->h_dest,
				       skb->priority, ifidx);
	if (flowid == BRCMF_FLOWRING_INVALID_ID) {
		kfree(create);
		return flowid;
	}

	create->flowid = flowid;
	create->ifidx = ifidx;
	memcpy(create->sa, eh->h_source, ETH_ALEN);
	memcpy(create->da, eh->h_dest, ETH_ALEN);

	spin_lock_irqsave(&msgbuf->flowring_work_lock, flags);
	list_add_tail(&create->queue, &msgbuf->work_queue);
	spin_unlock_irqrestore(&msgbuf->flowring_work_lock, flags);
	schedule_work(&msgbuf->flowring_work);

	return flowid;
}


static void brcmf_msgbuf_txflow(struct brcmf_msgbuf *msgbuf, u16 flowid)
{
	struct brcmf_flowring *flow = msgbuf->flow;
	struct brcmf_pub *drvr = msgbuf->drvr;
	struct brcmf_commonring *commonring;
	void *ret_ptr;
	u32 count;
	struct sk_buff *skb;
	dma_addr_t physaddr;
	u32 pktid;
	struct msgbuf_tx_msghdr *tx_msghdr;
	u64 address;

	commonring = msgbuf->flowrings[flowid];
	if (!brcmf_commonring_write_available(commonring))
		return;

	brcmf_commonring_lock(commonring);

	count = BRCMF_MSGBUF_TX_FLUSH_CNT2 - BRCMF_MSGBUF_TX_FLUSH_CNT1;
	while (brcmf_flowring_qlen(flow, flowid)) {
		skb = brcmf_flowring_dequeue(flow, flowid);
		if (skb == NULL) {
			bphy_err(drvr, "No SKB, but qlen %d\n",
				 brcmf_flowring_qlen(flow, flowid));
			break;
		}
		skb_orphan(skb);
		if (brcmf_msgbuf_alloc_pktid(msgbuf->drvr->bus_if->dev,
					     msgbuf->tx_pktids, skb, ETH_HLEN,
					     &physaddr, &pktid)) {
			brcmf_flowring_reinsert(flow, flowid, skb);
			bphy_err(drvr, "No PKTID available !!\n");
			break;
		}
		ret_ptr = brcmf_commonring_reserve_for_write(commonring);
		if (!ret_ptr) {
			brcmf_msgbuf_get_pktid(msgbuf->drvr->bus_if->dev,
					       msgbuf->tx_pktids, pktid);
			brcmf_flowring_reinsert(flow, flowid, skb);
			break;
		}
		count++;

		tx_msghdr = (struct msgbuf_tx_msghdr *)ret_ptr;

		tx_msghdr->msg.msgtype = MSGBUF_TYPE_TX_POST;
		tx_msghdr->msg.request_id = cpu_to_le32(pktid + 1);
		tx_msghdr->msg.ifidx = brcmf_flowring_ifidx_get(flow, flowid);
		tx_msghdr->flags = BRCMF_MSGBUF_PKT_FLAGS_FRAME_802_3;
		tx_msghdr->flags |= (skb->priority & 0x07) <<
				    BRCMF_MSGBUF_PKT_FLAGS_PRIO_SHIFT;
		tx_msghdr->seg_cnt = 1;
		memcpy(tx_msghdr->txhdr, skb->data, ETH_HLEN);
		tx_msghdr->data_len = cpu_to_le16(skb->len - ETH_HLEN);
		address = (u64)physaddr;
		tx_msghdr->data_buf_addr.high_addr = cpu_to_le32(address >> 32);
		tx_msghdr->data_buf_addr.low_addr =
			cpu_to_le32(address & 0xffffffff);
		tx_msghdr->metadata_buf_len = 0;
		tx_msghdr->metadata_buf_addr.high_addr = 0;
		tx_msghdr->metadata_buf_addr.low_addr = 0;
		atomic_inc(&commonring->outstanding_tx);
		if (count >= BRCMF_MSGBUF_TX_FLUSH_CNT2) {
			brcmf_commonring_write_complete(commonring);
			count = 0;
		}
	}
	if (count)
		brcmf_commonring_write_complete(commonring);
	brcmf_commonring_unlock(commonring);
}


static void brcmf_msgbuf_txflow_worker(struct work_struct *worker)
{
	struct brcmf_msgbuf *msgbuf;
	u32 flowid;

	msgbuf = container_of(worker, struct brcmf_msgbuf, txflow_work);
	for_each_set_bit(flowid, msgbuf->flow_map, msgbuf->max_flowrings) {
		clear_bit(flowid, msgbuf->flow_map);
		brcmf_msgbuf_txflow(msgbuf, flowid);
	}
}


static int brcmf_msgbuf_schedule_txdata(struct brcmf_msgbuf *msgbuf, u32 flowid,
					bool force)
{
	struct brcmf_commonring *commonring;

	set_bit(flowid, msgbuf->flow_map);
	commonring = msgbuf->flowrings[flowid];
	if ((force) || (atomic_read(&commonring->outstanding_tx) <
			BRCMF_MSGBUF_DELAY_TXWORKER_THRS))
		queue_work(msgbuf->txflow_wq, &msgbuf->txflow_work);

	return 0;
}


static int brcmf_msgbuf_tx_queue_data(struct brcmf_pub *drvr, int ifidx,
				      struct sk_buff *skb)
{
	struct brcmf_msgbuf *msgbuf = (struct brcmf_msgbuf *)drvr->proto->pd;
	struct brcmf_flowring *flow = msgbuf->flow;
	struct ethhdr *eh = (struct ethhdr *)(skb->data);
	u32 flowid;
	u32 queue_count;
	bool force;

	flowid = brcmf_flowring_lookup(flow, eh->h_dest, skb->priority, ifidx);
	if (flowid == BRCMF_FLOWRING_INVALID_ID) {
		flowid = brcmf_msgbuf_flowring_create(msgbuf, ifidx, skb);
		if (flowid == BRCMF_FLOWRING_INVALID_ID)
			return -ENOMEM;
	}
	queue_count = brcmf_flowring_enqueue(flow, flowid, skb);
	force = ((queue_count % BRCMF_MSGBUF_TRICKLE_TXWORKER_THRS) == 0);
	brcmf_msgbuf_schedule_txdata(msgbuf, flowid, force);

	return 0;
}


static void
brcmf_msgbuf_configure_addr_mode(struct brcmf_pub *drvr, int ifidx,
				 enum proto_addr_mode addr_mode)
{
	struct brcmf_msgbuf *msgbuf = (struct brcmf_msgbuf *)drvr->proto->pd;

	brcmf_flowring_configure_addr_mode(msgbuf->flow, ifidx, addr_mode);
}


static void
brcmf_msgbuf_delete_peer(struct brcmf_pub *drvr, int ifidx, u8 peer[ETH_ALEN])
{
	struct brcmf_msgbuf *msgbuf = (struct brcmf_msgbuf *)drvr->proto->pd;

	brcmf_flowring_delete_peer(msgbuf->flow, ifidx, peer);
}


static void
brcmf_msgbuf_add_tdls_peer(struct brcmf_pub *drvr, int ifidx, u8 peer[ETH_ALEN])
{
	struct brcmf_msgbuf *msgbuf = (struct brcmf_msgbuf *)drvr->proto->pd;

	brcmf_flowring_add_tdls_peer(msgbuf->flow, ifidx, peer);
}


static void
brcmf_msgbuf_process_ioctl_complete(struct brcmf_msgbuf *msgbuf, void *buf)
{
	struct msgbuf_ioctl_resp_hdr *ioctl_resp;

	ioctl_resp = (struct msgbuf_ioctl_resp_hdr *)buf;

	msgbuf->ioctl_resp_status =
			(s16)le16_to_cpu(ioctl_resp->compl_hdr.status);
	msgbuf->ioctl_resp_ret_len = le16_to_cpu(ioctl_resp->resp_len);
	msgbuf->ioctl_resp_pktid = le32_to_cpu(ioctl_resp->msg.request_id);

	brcmf_msgbuf_ioctl_resp_wake(msgbuf);

	if (msgbuf->cur_ioctlrespbuf)
		msgbuf->cur_ioctlrespbuf--;
	brcmf_msgbuf_rxbuf_ioctlresp_post(msgbuf);
}


static void
brcmf_msgbuf_process_txstatus(struct brcmf_msgbuf *msgbuf, void *buf)
{
	struct brcmf_commonring *commonring;
	struct msgbuf_tx_status *tx_status;
	u32 idx;
	struct sk_buff *skb;
	u16 flowid;

	tx_status = (struct msgbuf_tx_status *)buf;
	idx = le32_to_cpu(tx_status->msg.request_id) - 1;
	flowid = le16_to_cpu(tx_status->compl_hdr.flow_ring_id);
	flowid -= BRCMF_H2D_MSGRING_FLOWRING_IDSTART;
	skb = brcmf_msgbuf_get_pktid(msgbuf->drvr->bus_if->dev,
				     msgbuf->tx_pktids, idx);
	if (!skb)
		return;

	set_bit(flowid, msgbuf->txstatus_done_map);
	commonring = msgbuf->flowrings[flowid];
	atomic_dec(&commonring->outstanding_tx);

	brcmf_txfinalize(brcmf_get_ifp(msgbuf->drvr, tx_status->msg.ifidx),
			 skb, true);
}


static u32 brcmf_msgbuf_rxbuf_data_post(struct brcmf_msgbuf *msgbuf, u32 count)
{
	struct brcmf_pub *drvr = msgbuf->drvr;
	struct brcmf_commonring *commonring;
	void *ret_ptr;
	struct sk_buff *skb;
	u16 alloced;
	u32 pktlen;
	dma_addr_t physaddr;
	struct msgbuf_rx_bufpost *rx_bufpost;
	u64 address;
	u32 pktid;
	u32 i;

	commonring = msgbuf->commonrings[BRCMF_H2D_MSGRING_RXPOST_SUBMIT];
	ret_ptr = brcmf_commonring_reserve_for_write_multiple(commonring,
							      count,
							      &alloced);
	if (!ret_ptr) {
		brcmf_dbg(MSGBUF, "Failed to reserve space in commonring\n");
		return 0;
	}

	for (i = 0; i < alloced; i++) {
		rx_bufpost = (struct msgbuf_rx_bufpost *)ret_ptr;
		memset(rx_bufpost, 0, sizeof(*rx_bufpost));

		skb = brcmu_pkt_buf_get_skb(BRCMF_MSGBUF_MAX_PKT_SIZE);

		if (skb == NULL) {
			bphy_err(drvr, "Failed to alloc SKB\n");
			brcmf_commonring_write_cancel(commonring, alloced - i);
			break;
		}

		pktlen = skb->len;
		if (brcmf_msgbuf_alloc_pktid(msgbuf->drvr->bus_if->dev,
					     msgbuf->rx_pktids, skb, 0,
					     &physaddr, &pktid)) {
			dev_kfree_skb_any(skb);
			bphy_err(drvr, "No PKTID available !!\n");
			brcmf_commonring_write_cancel(commonring, alloced - i);
			break;
		}

		if (msgbuf->rx_metadata_offset) {
			address = (u64)physaddr;
			rx_bufpost->metadata_buf_len =
				cpu_to_le16(msgbuf->rx_metadata_offset);
			rx_bufpost->metadata_buf_addr.high_addr =
				cpu_to_le32(address >> 32);
			rx_bufpost->metadata_buf_addr.low_addr =
				cpu_to_le32(address & 0xffffffff);

			skb_pull(skb, msgbuf->rx_metadata_offset);
			pktlen = skb->len;
			physaddr += msgbuf->rx_metadata_offset;
		}
		rx_bufpost->msg.msgtype = MSGBUF_TYPE_RXBUF_POST;
		rx_bufpost->msg.request_id = cpu_to_le32(pktid);

		address = (u64)physaddr;
		rx_bufpost->data_buf_len = cpu_to_le16((u16)pktlen);
		rx_bufpost->data_buf_addr.high_addr =
			cpu_to_le32(address >> 32);
		rx_bufpost->data_buf_addr.low_addr =
			cpu_to_le32(address & 0xffffffff);

		ret_ptr += brcmf_commonring_len_item(commonring);
	}

	if (i)
		brcmf_commonring_write_complete(commonring);

	return i;
}


static void
brcmf_msgbuf_rxbuf_data_fill(struct brcmf_msgbuf *msgbuf)
{
	u32 fillbufs;
	u32 retcount;

	fillbufs = msgbuf->max_rxbufpost - msgbuf->rxbufpost;

	while (fillbufs) {
		retcount = brcmf_msgbuf_rxbuf_data_post(msgbuf, fillbufs);
		if (!retcount)
			break;
		msgbuf->rxbufpost += retcount;
		fillbufs -= retcount;
	}
}


static void
brcmf_msgbuf_update_rxbufpost_count(struct brcmf_msgbuf *msgbuf, u16 rxcnt)
{
	msgbuf->rxbufpost -= rxcnt;
	if (msgbuf->rxbufpost <= (msgbuf->max_rxbufpost -
				  BRCMF_MSGBUF_RXBUFPOST_THRESHOLD))
		brcmf_msgbuf_rxbuf_data_fill(msgbuf);
}


static u32
brcmf_msgbuf_rxbuf_ctrl_post(struct brcmf_msgbuf *msgbuf, bool event_buf,
			     u32 count)
{
	struct brcmf_pub *drvr = msgbuf->drvr;
	struct brcmf_commonring *commonring;
	void *ret_ptr;
	struct sk_buff *skb;
	u16 alloced;
	u32 pktlen;
	dma_addr_t physaddr;
	struct msgbuf_rx_ioctl_resp_or_event *rx_bufpost;
	u64 address;
	u32 pktid;
	u32 i;

	commonring = msgbuf->commonrings[BRCMF_H2D_MSGRING_CONTROL_SUBMIT];
	brcmf_commonring_lock(commonring);
	ret_ptr = brcmf_commonring_reserve_for_write_multiple(commonring,
							      count,
							      &alloced);
	if (!ret_ptr) {
		bphy_err(drvr, "Failed to reserve space in commonring\n");
		brcmf_commonring_unlock(commonring);
		return 0;
	}

	for (i = 0; i < alloced; i++) {
		rx_bufpost = (struct msgbuf_rx_ioctl_resp_or_event *)ret_ptr;
		memset(rx_bufpost, 0, sizeof(*rx_bufpost));

		skb = brcmu_pkt_buf_get_skb(BRCMF_MSGBUF_MAX_PKT_SIZE);

		if (skb == NULL) {
			bphy_err(drvr, "Failed to alloc SKB\n");
			brcmf_commonring_write_cancel(commonring, alloced - i);
			break;
		}

		pktlen = skb->len;
		if (brcmf_msgbuf_alloc_pktid(msgbuf->drvr->bus_if->dev,
					     msgbuf->rx_pktids, skb, 0,
					     &physaddr, &pktid)) {
			dev_kfree_skb_any(skb);
			bphy_err(drvr, "No PKTID available !!\n");
			brcmf_commonring_write_cancel(commonring, alloced - i);
			break;
		}
		if (event_buf)
			rx_bufpost->msg.msgtype = MSGBUF_TYPE_EVENT_BUF_POST;
		else
			rx_bufpost->msg.msgtype =
				MSGBUF_TYPE_IOCTLRESP_BUF_POST;
		rx_bufpost->msg.request_id = cpu_to_le32(pktid);

		address = (u64)physaddr;
		rx_bufpost->host_buf_len = cpu_to_le16((u16)pktlen);
		rx_bufpost->host_buf_addr.high_addr =
			cpu_to_le32(address >> 32);
		rx_bufpost->host_buf_addr.low_addr =
			cpu_to_le32(address & 0xffffffff);

		ret_ptr += brcmf_commonring_len_item(commonring);
	}

	if (i)
		brcmf_commonring_write_complete(commonring);

	brcmf_commonring_unlock(commonring);

	return i;
}


static void brcmf_msgbuf_rxbuf_ioctlresp_post(struct brcmf_msgbuf *msgbuf)
{
	u32 count;

	count = msgbuf->max_ioctlrespbuf - msgbuf->cur_ioctlrespbuf;
	count = brcmf_msgbuf_rxbuf_ctrl_post(msgbuf, false, count);
	msgbuf->cur_ioctlrespbuf += count;
}


static void brcmf_msgbuf_rxbuf_event_post(struct brcmf_msgbuf *msgbuf)
{
	u32 count;

	count = msgbuf->max_eventbuf - msgbuf->cur_eventbuf;
	count = brcmf_msgbuf_rxbuf_ctrl_post(msgbuf, true, count);
	msgbuf->cur_eventbuf += count;
}


static void brcmf_msgbuf_process_event(struct brcmf_msgbuf *msgbuf, void *buf)
{
	struct brcmf_pub *drvr = msgbuf->drvr;
	struct msgbuf_rx_event *event;
	u32 idx;
	u16 buflen;
	struct sk_buff *skb;
	struct brcmf_if *ifp;

	event = (struct msgbuf_rx_event *)buf;
	idx = le32_to_cpu(event->msg.request_id);
	buflen = le16_to_cpu(event->event_data_len);

	if (msgbuf->cur_eventbuf)
		msgbuf->cur_eventbuf--;
	brcmf_msgbuf_rxbuf_event_post(msgbuf);

	skb = brcmf_msgbuf_get_pktid(msgbuf->drvr->bus_if->dev,
				     msgbuf->rx_pktids, idx);
	if (!skb)
		return;

	if (msgbuf->rx_dataoffset)
		skb_pull(skb, msgbuf->rx_dataoffset);

	skb_trim(skb, buflen);

	ifp = brcmf_get_ifp(msgbuf->drvr, event->msg.ifidx);
	if (!ifp || !ifp->ndev) {
		bphy_err(drvr, "Received pkt for invalid ifidx %d\n",
			 event->msg.ifidx);
		goto exit;
	}

	skb->protocol = eth_type_trans(skb, ifp->ndev);

	brcmf_fweh_process_skb(ifp->drvr, skb, 0);

exit:
	brcmu_pkt_buf_free_skb(skb);
}


static void
brcmf_msgbuf_process_rx_complete(struct brcmf_msgbuf *msgbuf, void *buf)
{
	struct brcmf_pub *drvr = msgbuf->drvr;
	struct msgbuf_rx_complete *rx_complete;
	struct sk_buff *skb;
	u16 data_offset;
	u16 buflen;
	u16 flags;
	u32 idx;
	struct brcmf_if *ifp;

	brcmf_msgbuf_update_rxbufpost_count(msgbuf, 1);

	rx_complete = (struct msgbuf_rx_complete *)buf;
	data_offset = le16_to_cpu(rx_complete->data_offset);
	buflen = le16_to_cpu(rx_complete->data_len);
	idx = le32_to_cpu(rx_complete->msg.request_id);
	flags = le16_to_cpu(rx_complete->flags);

	skb = brcmf_msgbuf_get_pktid(msgbuf->drvr->bus_if->dev,
				     msgbuf->rx_pktids, idx);
	if (!skb)
		return;

	if (data_offset)
		skb_pull(skb, data_offset);
	else if (msgbuf->rx_dataoffset)
		skb_pull(skb, msgbuf->rx_dataoffset);

	skb_trim(skb, buflen);

	if ((flags & BRCMF_MSGBUF_PKT_FLAGS_FRAME_MASK) ==
	    BRCMF_MSGBUF_PKT_FLAGS_FRAME_802_11) {
		ifp = msgbuf->drvr->mon_if;

		if (!ifp) {
			bphy_err(drvr, "Received unexpected monitor pkt\n");
			brcmu_pkt_buf_free_skb(skb);
			return;
		}

		brcmf_netif_mon_rx(ifp, skb);
		return;
	}

	ifp = brcmf_get_ifp(msgbuf->drvr, rx_complete->msg.ifidx);
	if (!ifp || !ifp->ndev) {
		bphy_err(drvr, "Received pkt for invalid ifidx %d\n",
			 rx_complete->msg.ifidx);
		brcmu_pkt_buf_free_skb(skb);
		return;
	}

	skb->protocol = eth_type_trans(skb, ifp->ndev);
	brcmf_netif_rx(ifp, skb);
}

static void brcmf_msgbuf_process_gen_status(struct brcmf_msgbuf *msgbuf,
					    void *buf)
{
	struct msgbuf_gen_status *gen_status = buf;
	struct brcmf_pub *drvr = msgbuf->drvr;
	int err;

	err = le16_to_cpu(gen_status->compl_hdr.status);
	if (err)
		bphy_err(drvr, "Firmware reported general error: %d\n", err);
}

static void brcmf_msgbuf_process_ring_status(struct brcmf_msgbuf *msgbuf,
					     void *buf)
{
	struct msgbuf_ring_status *ring_status = buf;
	struct brcmf_pub *drvr = msgbuf->drvr;
	int err;

	err = le16_to_cpu(ring_status->compl_hdr.status);
	if (err) {
		int ring = le16_to_cpu(ring_status->compl_hdr.flow_ring_id);

		bphy_err(drvr, "Firmware reported ring %d error: %d\n", ring,
			 err);
	}
}

static void
brcmf_msgbuf_process_flow_ring_create_response(struct brcmf_msgbuf *msgbuf,
					       void *buf)
{
	struct brcmf_pub *drvr = msgbuf->drvr;
	struct msgbuf_flowring_create_resp *flowring_create_resp;
	u16 status;
	u16 flowid;

	flowring_create_resp = (struct msgbuf_flowring_create_resp *)buf;

	flowid = le16_to_cpu(flowring_create_resp->compl_hdr.flow_ring_id);
	flowid -= BRCMF_H2D_MSGRING_FLOWRING_IDSTART;
	status =  le16_to_cpu(flowring_create_resp->compl_hdr.status);

	if (status) {
		bphy_err(drvr, "Flowring creation failed, code %d\n", status);
		brcmf_msgbuf_remove_flowring(msgbuf, flowid);
		return;
	}
	brcmf_dbg(MSGBUF, "Flowring %d Create response status %d\n", flowid,
		  status);

	brcmf_flowring_open(msgbuf->flow, flowid);

	brcmf_msgbuf_schedule_txdata(msgbuf, flowid, true);
}


static void
brcmf_msgbuf_process_flow_ring_delete_response(struct brcmf_msgbuf *msgbuf,
					       void *buf)
{
	struct brcmf_pub *drvr = msgbuf->drvr;
	struct msgbuf_flowring_delete_resp *flowring_delete_resp;
	u16 status;
	u16 flowid;

	flowring_delete_resp = (struct msgbuf_flowring_delete_resp *)buf;

	flowid = le16_to_cpu(flowring_delete_resp->compl_hdr.flow_ring_id);
	flowid -= BRCMF_H2D_MSGRING_FLOWRING_IDSTART;
	status =  le16_to_cpu(flowring_delete_resp->compl_hdr.status);

	if (status) {
		bphy_err(drvr, "Flowring deletion failed, code %d\n", status);
		brcmf_flowring_delete(msgbuf->flow, flowid);
		return;
	}
	brcmf_dbg(MSGBUF, "Flowring %d Delete response status %d\n", flowid,
		  status);

	brcmf_msgbuf_remove_flowring(msgbuf, flowid);
}


static void brcmf_msgbuf_process_msgtype(struct brcmf_msgbuf *msgbuf, void *buf)
{
	struct brcmf_pub *drvr = msgbuf->drvr;
	struct msgbuf_common_hdr *msg;

	msg = (struct msgbuf_common_hdr *)buf;
	switch (msg->msgtype) {
	case MSGBUF_TYPE_GEN_STATUS:
		brcmf_dbg(MSGBUF, "MSGBUF_TYPE_GEN_STATUS\n");
		brcmf_msgbuf_process_gen_status(msgbuf, buf);
		break;
	case MSGBUF_TYPE_RING_STATUS:
		brcmf_dbg(MSGBUF, "MSGBUF_TYPE_RING_STATUS\n");
		brcmf_msgbuf_process_ring_status(msgbuf, buf);
		break;
	case MSGBUF_TYPE_FLOW_RING_CREATE_CMPLT:
		brcmf_dbg(MSGBUF, "MSGBUF_TYPE_FLOW_RING_CREATE_CMPLT\n");
		brcmf_msgbuf_process_flow_ring_create_response(msgbuf, buf);
		break;
	case MSGBUF_TYPE_FLOW_RING_DELETE_CMPLT:
		brcmf_dbg(MSGBUF, "MSGBUF_TYPE_FLOW_RING_DELETE_CMPLT\n");
		brcmf_msgbuf_process_flow_ring_delete_response(msgbuf, buf);
		break;
	case MSGBUF_TYPE_IOCTLPTR_REQ_ACK:
		brcmf_dbg(MSGBUF, "MSGBUF_TYPE_IOCTLPTR_REQ_ACK\n");
		break;
	case MSGBUF_TYPE_IOCTL_CMPLT:
		brcmf_dbg(MSGBUF, "MSGBUF_TYPE_IOCTL_CMPLT\n");
		brcmf_msgbuf_process_ioctl_complete(msgbuf, buf);
		break;
	case MSGBUF_TYPE_WL_EVENT:
		brcmf_dbg(MSGBUF, "MSGBUF_TYPE_WL_EVENT\n");
		brcmf_msgbuf_process_event(msgbuf, buf);
		break;
	case MSGBUF_TYPE_TX_STATUS:
		brcmf_dbg(MSGBUF, "MSGBUF_TYPE_TX_STATUS\n");
		brcmf_msgbuf_process_txstatus(msgbuf, buf);
		break;
	case MSGBUF_TYPE_RX_CMPLT:
		brcmf_dbg(MSGBUF, "MSGBUF_TYPE_RX_CMPLT\n");
		brcmf_msgbuf_process_rx_complete(msgbuf, buf);
		break;
	default:
		bphy_err(drvr, "Unsupported msgtype %d\n", msg->msgtype);
		break;
	}
}


static void brcmf_msgbuf_process_rx(struct brcmf_msgbuf *msgbuf,
				    struct brcmf_commonring *commonring)
{
	void *buf;
	u16 count;
	u16 processed;

again:
	buf = brcmf_commonring_get_read_ptr(commonring, &count);
	if (buf == NULL)
		return;

	processed = 0;
	while (count) {
		brcmf_msgbuf_process_msgtype(msgbuf,
					     buf + msgbuf->rx_dataoffset);
		buf += brcmf_commonring_len_item(commonring);
		processed++;
		if (processed == BRCMF_MSGBUF_UPDATE_RX_PTR_THRS) {
			brcmf_commonring_read_complete(commonring, processed);
			processed = 0;
		}
		count--;
	}
	if (processed)
		brcmf_commonring_read_complete(commonring, processed);

	if (commonring->r_ptr == 0)
		goto again;
}


int brcmf_proto_msgbuf_rx_trigger(struct device *dev)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;
	struct brcmf_msgbuf *msgbuf = (struct brcmf_msgbuf *)drvr->proto->pd;
	struct brcmf_commonring *commonring;
	void *buf;
	u32 flowid;
	int qlen;

	buf = msgbuf->commonrings[BRCMF_D2H_MSGRING_RX_COMPLETE];
	brcmf_msgbuf_process_rx(msgbuf, buf);
	buf = msgbuf->commonrings[BRCMF_D2H_MSGRING_TX_COMPLETE];
	brcmf_msgbuf_process_rx(msgbuf, buf);
	buf = msgbuf->commonrings[BRCMF_D2H_MSGRING_CONTROL_COMPLETE];
	brcmf_msgbuf_process_rx(msgbuf, buf);

	for_each_set_bit(flowid, msgbuf->txstatus_done_map,
			 msgbuf->max_flowrings) {
		clear_bit(flowid, msgbuf->txstatus_done_map);
		commonring = msgbuf->flowrings[flowid];
		qlen = brcmf_flowring_qlen(msgbuf->flow, flowid);
		if ((qlen > BRCMF_MSGBUF_TRICKLE_TXWORKER_THRS) ||
		    ((qlen) && (atomic_read(&commonring->outstanding_tx) <
				BRCMF_MSGBUF_TRICKLE_TXWORKER_THRS)))
			brcmf_msgbuf_schedule_txdata(msgbuf, flowid, true);
	}

	return 0;
}


void brcmf_msgbuf_delete_flowring(struct brcmf_pub *drvr, u16 flowid)
{
	struct brcmf_msgbuf *msgbuf = (struct brcmf_msgbuf *)drvr->proto->pd;
	struct msgbuf_tx_flowring_delete_req *delete;
	struct brcmf_commonring *commonring;
	void *ret_ptr;
	u8 ifidx;
	int err;

	/* no need to submit if firmware can not be reached */
	if (drvr->bus_if->state != BRCMF_BUS_UP) {
		brcmf_dbg(MSGBUF, "bus down, flowring will be removed\n");
		brcmf_msgbuf_remove_flowring(msgbuf, flowid);
		return;
	}

	commonring = msgbuf->commonrings[BRCMF_H2D_MSGRING_CONTROL_SUBMIT];
	brcmf_commonring_lock(commonring);
	ret_ptr = brcmf_commonring_reserve_for_write(commonring);
	if (!ret_ptr) {
		bphy_err(drvr, "FW unaware, flowring will be removed !!\n");
		brcmf_commonring_unlock(commonring);
		brcmf_msgbuf_remove_flowring(msgbuf, flowid);
		return;
	}

	delete = (struct msgbuf_tx_flowring_delete_req *)ret_ptr;

	ifidx = brcmf_flowring_ifidx_get(msgbuf->flow, flowid);

	delete->msg.msgtype = MSGBUF_TYPE_FLOW_RING_DELETE;
	delete->msg.ifidx = ifidx;
	delete->msg.request_id = 0;

	delete->flow_ring_id = cpu_to_le16(flowid +
					   BRCMF_H2D_MSGRING_FLOWRING_IDSTART);
	delete->reason = 0;

	brcmf_dbg(MSGBUF, "Send Flow Delete Req flow ID %d, ifindex %d\n",
		  flowid, ifidx);

	err = brcmf_commonring_write_complete(commonring);
	brcmf_commonring_unlock(commonring);
	if (err) {
		bphy_err(drvr, "Failed to submit RING_DELETE, flowring will be removed\n");
		brcmf_msgbuf_remove_flowring(msgbuf, flowid);
	}
}

#ifdef DEBUG
static int brcmf_msgbuf_stats_read(struct seq_file *seq, void *data)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(seq->private);
	struct brcmf_pub *drvr = bus_if->drvr;
	struct brcmf_msgbuf *msgbuf = (struct brcmf_msgbuf *)drvr->proto->pd;
	struct brcmf_commonring *commonring;
	u16 i;
	struct brcmf_flowring_ring *ring;
	struct brcmf_flowring_hash *hash;

	commonring = msgbuf->commonrings[BRCMF_H2D_MSGRING_CONTROL_SUBMIT];
	seq_printf(seq, "h2d_ctl_submit: rp %4u, wp %4u, depth %4u\n",
		   commonring->r_ptr, commonring->w_ptr, commonring->depth);
	commonring = msgbuf->commonrings[BRCMF_H2D_MSGRING_RXPOST_SUBMIT];
	seq_printf(seq, "h2d_rx_submit:  rp %4u, wp %4u, depth %4u\n",
		   commonring->r_ptr, commonring->w_ptr, commonring->depth);
	commonring = msgbuf->commonrings[BRCMF_D2H_MSGRING_CONTROL_COMPLETE];
	seq_printf(seq, "d2h_ctl_cmplt:  rp %4u, wp %4u, depth %4u\n",
		   commonring->r_ptr, commonring->w_ptr, commonring->depth);
	commonring = msgbuf->commonrings[BRCMF_D2H_MSGRING_TX_COMPLETE];
	seq_printf(seq, "d2h_tx_cmplt:   rp %4u, wp %4u, depth %4u\n",
		   commonring->r_ptr, commonring->w_ptr, commonring->depth);
	commonring = msgbuf->commonrings[BRCMF_D2H_MSGRING_RX_COMPLETE];
	seq_printf(seq, "d2h_rx_cmplt:   rp %4u, wp %4u, depth %4u\n",
		   commonring->r_ptr, commonring->w_ptr, commonring->depth);

	seq_printf(seq, "\nh2d_flowrings: depth %u\n",
		   BRCMF_H2D_TXFLOWRING_MAX_ITEM);
	seq_puts(seq, "Active flowrings:\n");
	for (i = 0; i < msgbuf->flow->nrofrings; i++) {
		if (!msgbuf->flow->rings[i])
			continue;
		ring = msgbuf->flow->rings[i];
		if (ring->status != RING_OPEN)
			continue;
		commonring = msgbuf->flowrings[i];
		hash = &msgbuf->flow->hash[ring->hash_id];
		seq_printf(seq, "id %3u: rp %4u, wp %4u, qlen %4u, blocked %u\n"
				"        ifidx %u, fifo %u, da %pM\n",
				i, commonring->r_ptr, commonring->w_ptr,
				skb_queue_len(&ring->skblist), ring->blocked,
				hash->ifidx, hash->fifo, hash->mac);
	}

	return 0;
}
#else
static int brcmf_msgbuf_stats_read(struct seq_file *seq, void *data)
{
	return 0;
}
#endif

static void brcmf_msgbuf_debugfs_create(struct brcmf_pub *drvr)
{
	brcmf_debugfs_add_entry(drvr, "msgbuf_stats", brcmf_msgbuf_stats_read);
}

int brcmf_proto_msgbuf_attach(struct brcmf_pub *drvr)
{
	struct brcmf_bus_msgbuf *if_msgbuf;
	struct brcmf_msgbuf *msgbuf;
	u64 address;
	u32 count;

	if_msgbuf = drvr->bus_if->msgbuf;

	if (if_msgbuf->max_flowrings >= BRCMF_FLOWRING_HASHSIZE) {
		bphy_err(drvr, "driver not configured for this many flowrings %d\n",
			 if_msgbuf->max_flowrings);
		if_msgbuf->max_flowrings = BRCMF_FLOWRING_HASHSIZE - 1;
	}

	msgbuf = kzalloc(sizeof(*msgbuf), GFP_KERNEL);
	if (!msgbuf)
		goto fail;

	msgbuf->txflow_wq = create_singlethread_workqueue("msgbuf_txflow");
	if (msgbuf->txflow_wq == NULL) {
		bphy_err(drvr, "workqueue creation failed\n");
		goto fail;
	}
	INIT_WORK(&msgbuf->txflow_work, brcmf_msgbuf_txflow_worker);
	count = BITS_TO_LONGS(if_msgbuf->max_flowrings);
	count = count * sizeof(unsigned long);
	msgbuf->flow_map = kzalloc(count, GFP_KERNEL);
	if (!msgbuf->flow_map)
		goto fail;

	msgbuf->txstatus_done_map = kzalloc(count, GFP_KERNEL);
	if (!msgbuf->txstatus_done_map)
		goto fail;

	msgbuf->drvr = drvr;
	msgbuf->ioctbuf = dma_alloc_coherent(drvr->bus_if->dev,
					     BRCMF_TX_IOCTL_MAX_MSG_SIZE,
					     &msgbuf->ioctbuf_handle,
					     GFP_KERNEL);
	if (!msgbuf->ioctbuf)
		goto fail;
	address = (u64)msgbuf->ioctbuf_handle;
	msgbuf->ioctbuf_phys_hi = address >> 32;
	msgbuf->ioctbuf_phys_lo = address & 0xffffffff;

	drvr->proto->hdrpull = brcmf_msgbuf_hdrpull;
	drvr->proto->query_dcmd = brcmf_msgbuf_query_dcmd;
	drvr->proto->set_dcmd = brcmf_msgbuf_set_dcmd;
	drvr->proto->tx_queue_data = brcmf_msgbuf_tx_queue_data;
	drvr->proto->configure_addr_mode = brcmf_msgbuf_configure_addr_mode;
	drvr->proto->delete_peer = brcmf_msgbuf_delete_peer;
	drvr->proto->add_tdls_peer = brcmf_msgbuf_add_tdls_peer;
	drvr->proto->rxreorder = brcmf_msgbuf_rxreorder;
	drvr->proto->debugfs_create = brcmf_msgbuf_debugfs_create;
	drvr->proto->pd = msgbuf;

	init_waitqueue_head(&msgbuf->ioctl_resp_wait);

	msgbuf->commonrings =
		(struct brcmf_commonring **)if_msgbuf->commonrings;
	msgbuf->flowrings = (struct brcmf_commonring **)if_msgbuf->flowrings;
	msgbuf->max_flowrings = if_msgbuf->max_flowrings;
	msgbuf->flowring_dma_handle =
		kcalloc(msgbuf->max_flowrings,
			sizeof(*msgbuf->flowring_dma_handle), GFP_KERNEL);
	if (!msgbuf->flowring_dma_handle)
		goto fail;

	msgbuf->rx_dataoffset = if_msgbuf->rx_dataoffset;
	msgbuf->max_rxbufpost = if_msgbuf->max_rxbufpost;

	msgbuf->max_ioctlrespbuf = BRCMF_MSGBUF_MAX_IOCTLRESPBUF_POST;
	msgbuf->max_eventbuf = BRCMF_MSGBUF_MAX_EVENTBUF_POST;

	msgbuf->tx_pktids = brcmf_msgbuf_init_pktids(NR_TX_PKTIDS,
						     DMA_TO_DEVICE);
	if (!msgbuf->tx_pktids)
		goto fail;
	msgbuf->rx_pktids = brcmf_msgbuf_init_pktids(NR_RX_PKTIDS,
						     DMA_FROM_DEVICE);
	if (!msgbuf->rx_pktids)
		goto fail;

	msgbuf->flow = brcmf_flowring_attach(drvr->bus_if->dev,
					     if_msgbuf->max_flowrings);
	if (!msgbuf->flow)
		goto fail;


	brcmf_dbg(MSGBUF, "Feeding buffers, rx data %d, rx event %d, rx ioctl resp %d\n",
		  msgbuf->max_rxbufpost, msgbuf->max_eventbuf,
		  msgbuf->max_ioctlrespbuf);
	count = 0;
	do {
		brcmf_msgbuf_rxbuf_data_fill(msgbuf);
		if (msgbuf->max_rxbufpost != msgbuf->rxbufpost)
			msleep(10);
		else
			break;
		count++;
	} while (count < 10);
	brcmf_msgbuf_rxbuf_event_post(msgbuf);
	brcmf_msgbuf_rxbuf_ioctlresp_post(msgbuf);

	INIT_WORK(&msgbuf->flowring_work, brcmf_msgbuf_flowring_worker);
	spin_lock_init(&msgbuf->flowring_work_lock);
	INIT_LIST_HEAD(&msgbuf->work_queue);

	return 0;

fail:
	if (msgbuf) {
		kfree(msgbuf->flow_map);
		kfree(msgbuf->txstatus_done_map);
		brcmf_msgbuf_release_pktids(msgbuf);
		kfree(msgbuf->flowring_dma_handle);
		if (msgbuf->ioctbuf)
			dma_free_coherent(drvr->bus_if->dev,
					  BRCMF_TX_IOCTL_MAX_MSG_SIZE,
					  msgbuf->ioctbuf,
					  msgbuf->ioctbuf_handle);
		kfree(msgbuf);
	}
	return -ENOMEM;
}


void brcmf_proto_msgbuf_detach(struct brcmf_pub *drvr)
{
	struct brcmf_msgbuf *msgbuf;
	struct brcmf_msgbuf_work_item *work;

	brcmf_dbg(TRACE, "Enter\n");
	if (drvr->proto->pd) {
		msgbuf = (struct brcmf_msgbuf *)drvr->proto->pd;
		cancel_work_sync(&msgbuf->flowring_work);
		while (!list_empty(&msgbuf->work_queue)) {
			work = list_first_entry(&msgbuf->work_queue,
						struct brcmf_msgbuf_work_item,
						queue);
			list_del(&work->queue);
			kfree(work);
		}
		kfree(msgbuf->flow_map);
		kfree(msgbuf->txstatus_done_map);
		if (msgbuf->txflow_wq)
			destroy_workqueue(msgbuf->txflow_wq);

		brcmf_flowring_detach(msgbuf->flow);
		dma_free_coherent(drvr->bus_if->dev,
				  BRCMF_TX_IOCTL_MAX_MSG_SIZE,
				  msgbuf->ioctbuf, msgbuf->ioctbuf_handle);
		brcmf_msgbuf_release_pktids(msgbuf);
		kfree(msgbuf->flowring_dma_handle);
		kfree(msgbuf);
		drvr->proto->pd = NULL;
	}
}
