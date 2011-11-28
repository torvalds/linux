/*
 * Copyright (C) ST-Ericsson AB 2010
 * Contact: Sjur Brendeland / sjur.brandeland@stericsson.com
 * Authors:  Amarnath Revanna / amarnath.bangalore.revanna@stericsson.com,
 *           Daniel Martensson / daniel.martensson@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":" fmt

#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>

#include <net/caif/caif_device.h>
#include <net/caif/caif_shm.h>

#define NR_TX_BUF		6
#define NR_RX_BUF		6
#define TX_BUF_SZ		0x2000
#define RX_BUF_SZ		0x2000

#define CAIF_NEEDED_HEADROOM	32

#define CAIF_FLOW_ON		1
#define CAIF_FLOW_OFF		0

#define LOW_WATERMARK		3
#define HIGH_WATERMARK		4

/* Maximum number of CAIF buffers per shared memory buffer. */
#define SHM_MAX_FRMS_PER_BUF	10

/*
 * Size in bytes of the descriptor area
 * (With end of descriptor signalling)
 */
#define SHM_CAIF_DESC_SIZE	((SHM_MAX_FRMS_PER_BUF + 1) * \
					sizeof(struct shm_pck_desc))

/*
 * Offset to the first CAIF frame within a shared memory buffer.
 * Aligned on 32 bytes.
 */
#define SHM_CAIF_FRM_OFS	(SHM_CAIF_DESC_SIZE + (SHM_CAIF_DESC_SIZE % 32))

/* Number of bytes for CAIF shared memory header. */
#define SHM_HDR_LEN		1

/* Number of padding bytes for the complete CAIF frame. */
#define SHM_FRM_PAD_LEN		4

#define CAIF_MAX_MTU		4096

#define SHM_SET_FULL(x)	(((x+1) & 0x0F) << 0)
#define SHM_GET_FULL(x)	(((x >> 0) & 0x0F) - 1)

#define SHM_SET_EMPTY(x)	(((x+1) & 0x0F) << 4)
#define SHM_GET_EMPTY(x)	(((x >> 4) & 0x0F) - 1)

#define SHM_FULL_MASK		(0x0F << 0)
#define SHM_EMPTY_MASK		(0x0F << 4)

struct shm_pck_desc {
	/*
	 * Offset from start of shared memory area to start of
	 * shared memory CAIF frame.
	 */
	u32 frm_ofs;
	u32 frm_len;
};

struct buf_list {
	unsigned char *desc_vptr;
	u32 phy_addr;
	u32 index;
	u32 len;
	u32 frames;
	u32 frm_ofs;
	struct list_head list;
};

struct shm_caif_frm {
	/* Number of bytes of padding before the CAIF frame. */
	u8 hdr_ofs;
};

struct shmdrv_layer {
	/* caif_dev_common must always be first in the structure*/
	struct caif_dev_common cfdev;

	u32 shm_tx_addr;
	u32 shm_rx_addr;
	u32 shm_base_addr;
	u32 tx_empty_available;
	spinlock_t lock;

	struct list_head tx_empty_list;
	struct list_head tx_pend_list;
	struct list_head tx_full_list;
	struct list_head rx_empty_list;
	struct list_head rx_pend_list;
	struct list_head rx_full_list;

	struct workqueue_struct *pshm_tx_workqueue;
	struct workqueue_struct *pshm_rx_workqueue;

	struct work_struct shm_tx_work;
	struct work_struct shm_rx_work;

	struct sk_buff_head sk_qhead;
	struct shmdev_layer *pshm_dev;
};

static int shm_netdev_open(struct net_device *shm_netdev)
{
	netif_wake_queue(shm_netdev);
	return 0;
}

static int shm_netdev_close(struct net_device *shm_netdev)
{
	netif_stop_queue(shm_netdev);
	return 0;
}

int caif_shmdrv_rx_cb(u32 mbx_msg, void *priv)
{
	struct buf_list *pbuf;
	struct shmdrv_layer *pshm_drv;
	struct list_head *pos;
	u32 avail_emptybuff = 0;
	unsigned long flags = 0;

	pshm_drv = priv;

	/* Check for received buffers. */
	if (mbx_msg & SHM_FULL_MASK) {
		int idx;

		spin_lock_irqsave(&pshm_drv->lock, flags);

		/* Check whether we have any outstanding buffers. */
		if (list_empty(&pshm_drv->rx_empty_list)) {

			/* Release spin lock. */
			spin_unlock_irqrestore(&pshm_drv->lock, flags);

			/* We print even in IRQ context... */
			pr_warn("No empty Rx buffers to fill: "
					"mbx_msg:%x\n", mbx_msg);

			/* Bail out. */
			goto err_sync;
		}

		pbuf =
			list_entry(pshm_drv->rx_empty_list.next,
					struct buf_list, list);
		idx = pbuf->index;

		/* Check buffer synchronization. */
		if (idx != SHM_GET_FULL(mbx_msg)) {

			/* We print even in IRQ context... */
			pr_warn(
			"phyif_shm_mbx_msg_cb: RX full out of sync:"
			" idx:%d, msg:%x SHM_GET_FULL(mbx_msg):%x\n",
				idx, mbx_msg, SHM_GET_FULL(mbx_msg));

			spin_unlock_irqrestore(&pshm_drv->lock, flags);

			/* Bail out. */
			goto err_sync;
		}

		list_del_init(&pbuf->list);
		list_add_tail(&pbuf->list, &pshm_drv->rx_full_list);

		spin_unlock_irqrestore(&pshm_drv->lock, flags);

		/* Schedule RX work queue. */
		if (!work_pending(&pshm_drv->shm_rx_work))
			queue_work(pshm_drv->pshm_rx_workqueue,
						&pshm_drv->shm_rx_work);
	}

	/* Check for emptied buffers. */
	if (mbx_msg & SHM_EMPTY_MASK) {
		int idx;

		spin_lock_irqsave(&pshm_drv->lock, flags);

		/* Check whether we have any outstanding buffers. */
		if (list_empty(&pshm_drv->tx_full_list)) {

			/* We print even in IRQ context... */
			pr_warn("No TX to empty: msg:%x\n", mbx_msg);

			spin_unlock_irqrestore(&pshm_drv->lock, flags);

			/* Bail out. */
			goto err_sync;
		}

		pbuf =
			list_entry(pshm_drv->tx_full_list.next,
					struct buf_list, list);
		idx = pbuf->index;

		/* Check buffer synchronization. */
		if (idx != SHM_GET_EMPTY(mbx_msg)) {

			spin_unlock_irqrestore(&pshm_drv->lock, flags);

			/* We print even in IRQ context... */
			pr_warn("TX empty "
				"out of sync:idx:%d, msg:%x\n", idx, mbx_msg);

			/* Bail out. */
			goto err_sync;
		}
		list_del_init(&pbuf->list);

		/* Reset buffer parameters. */
		pbuf->frames = 0;
		pbuf->frm_ofs = SHM_CAIF_FRM_OFS;

		list_add_tail(&pbuf->list, &pshm_drv->tx_empty_list);

		/* Check the available no. of buffers in the empty list */
		list_for_each(pos, &pshm_drv->tx_empty_list)
			avail_emptybuff++;

		/* Check whether we have to wake up the transmitter. */
		if ((avail_emptybuff > HIGH_WATERMARK) &&
					(!pshm_drv->tx_empty_available)) {
			pshm_drv->tx_empty_available = 1;
			pshm_drv->cfdev.flowctrl
					(pshm_drv->pshm_dev->pshm_netdev,
								CAIF_FLOW_ON);

			spin_unlock_irqrestore(&pshm_drv->lock, flags);

			/* Schedule the work queue. if required */
			if (!work_pending(&pshm_drv->shm_tx_work))
				queue_work(pshm_drv->pshm_tx_workqueue,
							&pshm_drv->shm_tx_work);
		} else
			spin_unlock_irqrestore(&pshm_drv->lock, flags);
	}

	return 0;

err_sync:
	return -EIO;
}

static void shm_rx_work_func(struct work_struct *rx_work)
{
	struct shmdrv_layer *pshm_drv;
	struct buf_list *pbuf;
	unsigned long flags = 0;
	struct sk_buff *skb;
	char *p;
	int ret;

	pshm_drv = container_of(rx_work, struct shmdrv_layer, shm_rx_work);

	while (1) {

		struct shm_pck_desc *pck_desc;

		spin_lock_irqsave(&pshm_drv->lock, flags);

		/* Check for received buffers. */
		if (list_empty(&pshm_drv->rx_full_list)) {
			spin_unlock_irqrestore(&pshm_drv->lock, flags);
			break;
		}

		pbuf =
			list_entry(pshm_drv->rx_full_list.next, struct buf_list,
					list);
		list_del_init(&pbuf->list);

		/* Retrieve pointer to start of the packet descriptor area. */
		pck_desc = (struct shm_pck_desc *) pbuf->desc_vptr;

		/*
		 * Check whether descriptor contains a CAIF shared memory
		 * frame.
		 */
		while (pck_desc->frm_ofs) {
			unsigned int frm_buf_ofs;
			unsigned int frm_pck_ofs;
			unsigned int frm_pck_len;
			/*
			 * Check whether offset is within buffer limits
			 * (lower).
			 */
			if (pck_desc->frm_ofs <
				(pbuf->phy_addr - pshm_drv->shm_base_addr))
				break;
			/*
			 * Check whether offset is within buffer limits
			 * (higher).
			 */
			if (pck_desc->frm_ofs >
				((pbuf->phy_addr - pshm_drv->shm_base_addr) +
					pbuf->len))
				break;

			/* Calculate offset from start of buffer. */
			frm_buf_ofs =
				pck_desc->frm_ofs - (pbuf->phy_addr -
						pshm_drv->shm_base_addr);

			/*
			 * Calculate offset and length of CAIF packet while
			 * taking care of the shared memory header.
			 */
			frm_pck_ofs =
				frm_buf_ofs + SHM_HDR_LEN +
				(*(pbuf->desc_vptr + frm_buf_ofs));
			frm_pck_len =
				(pck_desc->frm_len - SHM_HDR_LEN -
				(*(pbuf->desc_vptr + frm_buf_ofs)));

			/* Check whether CAIF packet is within buffer limits */
			if ((frm_pck_ofs + pck_desc->frm_len) > pbuf->len)
				break;

			/* Get a suitable CAIF packet and copy in data. */
			skb = netdev_alloc_skb(pshm_drv->pshm_dev->pshm_netdev,
							frm_pck_len + 1);
			BUG_ON(skb == NULL);

			p = skb_put(skb, frm_pck_len);
			memcpy(p, pbuf->desc_vptr + frm_pck_ofs, frm_pck_len);

			skb->protocol = htons(ETH_P_CAIF);
			skb_reset_mac_header(skb);
			skb->dev = pshm_drv->pshm_dev->pshm_netdev;

			/* Push received packet up the stack. */
			ret = netif_rx_ni(skb);

			if (!ret) {
				pshm_drv->pshm_dev->pshm_netdev->stats.
								rx_packets++;
				pshm_drv->pshm_dev->pshm_netdev->stats.
						rx_bytes += pck_desc->frm_len;
			} else
				++pshm_drv->pshm_dev->pshm_netdev->stats.
								rx_dropped;
			/* Move to next packet descriptor. */
			pck_desc++;
		}

		list_add_tail(&pbuf->list, &pshm_drv->rx_pend_list);

		spin_unlock_irqrestore(&pshm_drv->lock, flags);

	}

	/* Schedule the work queue. if required */
	if (!work_pending(&pshm_drv->shm_tx_work))
		queue_work(pshm_drv->pshm_tx_workqueue, &pshm_drv->shm_tx_work);

}

static void shm_tx_work_func(struct work_struct *tx_work)
{
	u32 mbox_msg;
	unsigned int frmlen, avail_emptybuff, append = 0;
	unsigned long flags = 0;
	struct buf_list *pbuf = NULL;
	struct shmdrv_layer *pshm_drv;
	struct shm_caif_frm *frm;
	struct sk_buff *skb;
	struct shm_pck_desc *pck_desc;
	struct list_head *pos;

	pshm_drv = container_of(tx_work, struct shmdrv_layer, shm_tx_work);

	do {
		/* Initialize mailbox message. */
		mbox_msg = 0x00;
		avail_emptybuff = 0;

		spin_lock_irqsave(&pshm_drv->lock, flags);

		/* Check for pending receive buffers. */
		if (!list_empty(&pshm_drv->rx_pend_list)) {

			pbuf = list_entry(pshm_drv->rx_pend_list.next,
						struct buf_list, list);

			list_del_init(&pbuf->list);
			list_add_tail(&pbuf->list, &pshm_drv->rx_empty_list);
			/*
			 * Value index is never changed,
			 * so read access should be safe.
			 */
			mbox_msg |= SHM_SET_EMPTY(pbuf->index);
		}

		skb = skb_peek(&pshm_drv->sk_qhead);

		if (skb == NULL)
			goto send_msg;

		/* Check the available no. of buffers in the empty list */
		list_for_each(pos, &pshm_drv->tx_empty_list)
			avail_emptybuff++;

		if ((avail_emptybuff < LOW_WATERMARK) &&
					pshm_drv->tx_empty_available) {
			/* Update blocking condition. */
			pshm_drv->tx_empty_available = 0;
			pshm_drv->cfdev.flowctrl
					(pshm_drv->pshm_dev->pshm_netdev,
					CAIF_FLOW_OFF);
		}
		/*
		 * We simply return back to the caller if we do not have space
		 * either in Tx pending list or Tx empty list. In this case,
		 * we hold the received skb in the skb list, waiting to
		 * be transmitted once Tx buffers become available
		 */
		if (list_empty(&pshm_drv->tx_empty_list))
			goto send_msg;

		/* Get the first free Tx buffer. */
		pbuf = list_entry(pshm_drv->tx_empty_list.next,
						struct buf_list, list);
		do {
			if (append) {
				skb = skb_peek(&pshm_drv->sk_qhead);
				if (skb == NULL)
					break;
			}

			frm = (struct shm_caif_frm *)
					(pbuf->desc_vptr + pbuf->frm_ofs);

			frm->hdr_ofs = 0;
			frmlen = 0;
			frmlen += SHM_HDR_LEN + frm->hdr_ofs + skb->len;

			/* Add tail padding if needed. */
			if (frmlen % SHM_FRM_PAD_LEN)
				frmlen += SHM_FRM_PAD_LEN -
						(frmlen % SHM_FRM_PAD_LEN);

			/*
			 * Verify that packet, header and additional padding
			 * can fit within the buffer frame area.
			 */
			if (frmlen >= (pbuf->len - pbuf->frm_ofs))
				break;

			if (!append) {
				list_del_init(&pbuf->list);
				append = 1;
			}

			skb = skb_dequeue(&pshm_drv->sk_qhead);
			/* Copy in CAIF frame. */
			skb_copy_bits(skb, 0, pbuf->desc_vptr +
					pbuf->frm_ofs + SHM_HDR_LEN +
						frm->hdr_ofs, skb->len);

			pshm_drv->pshm_dev->pshm_netdev->stats.tx_packets++;
			pshm_drv->pshm_dev->pshm_netdev->stats.tx_bytes +=
									frmlen;
			dev_kfree_skb(skb);

			/* Fill in the shared memory packet descriptor area. */
			pck_desc = (struct shm_pck_desc *) (pbuf->desc_vptr);
			/* Forward to current frame. */
			pck_desc += pbuf->frames;
			pck_desc->frm_ofs = (pbuf->phy_addr -
						pshm_drv->shm_base_addr) +
								pbuf->frm_ofs;
			pck_desc->frm_len = frmlen;
			/* Terminate packet descriptor area. */
			pck_desc++;
			pck_desc->frm_ofs = 0;
			/* Update buffer parameters. */
			pbuf->frames++;
			pbuf->frm_ofs += frmlen + (frmlen % 32);

		} while (pbuf->frames < SHM_MAX_FRMS_PER_BUF);

		/* Assign buffer as full. */
		list_add_tail(&pbuf->list, &pshm_drv->tx_full_list);
		append = 0;
		mbox_msg |= SHM_SET_FULL(pbuf->index);
send_msg:
		spin_unlock_irqrestore(&pshm_drv->lock, flags);

		if (mbox_msg)
			pshm_drv->pshm_dev->pshmdev_mbxsend
					(pshm_drv->pshm_dev->shm_id, mbox_msg);
	} while (mbox_msg);
}

static int shm_netdev_tx(struct sk_buff *skb, struct net_device *shm_netdev)
{
	struct shmdrv_layer *pshm_drv;
	unsigned long flags = 0;

	pshm_drv = netdev_priv(shm_netdev);

	spin_lock_irqsave(&pshm_drv->lock, flags);

	skb_queue_tail(&pshm_drv->sk_qhead, skb);

	spin_unlock_irqrestore(&pshm_drv->lock, flags);

	/* Schedule Tx work queue. for deferred processing of skbs*/
	if (!work_pending(&pshm_drv->shm_tx_work))
		queue_work(pshm_drv->pshm_tx_workqueue, &pshm_drv->shm_tx_work);

	return 0;
}

static const struct net_device_ops netdev_ops = {
	.ndo_open = shm_netdev_open,
	.ndo_stop = shm_netdev_close,
	.ndo_start_xmit = shm_netdev_tx,
};

static void shm_netdev_setup(struct net_device *pshm_netdev)
{
	struct shmdrv_layer *pshm_drv;
	pshm_netdev->netdev_ops = &netdev_ops;

	pshm_netdev->mtu = CAIF_MAX_MTU;
	pshm_netdev->type = ARPHRD_CAIF;
	pshm_netdev->hard_header_len = CAIF_NEEDED_HEADROOM;
	pshm_netdev->tx_queue_len = 0;
	pshm_netdev->destructor = free_netdev;

	pshm_drv = netdev_priv(pshm_netdev);

	/* Initialize structures in a clean state. */
	memset(pshm_drv, 0, sizeof(struct shmdrv_layer));

	pshm_drv->cfdev.link_select = CAIF_LINK_LOW_LATENCY;
}

int caif_shmcore_probe(struct shmdev_layer *pshm_dev)
{
	int result, j;
	struct shmdrv_layer *pshm_drv = NULL;

	pshm_dev->pshm_netdev = alloc_netdev(sizeof(struct shmdrv_layer),
						"cfshm%d", shm_netdev_setup);
	if (!pshm_dev->pshm_netdev)
		return -ENOMEM;

	pshm_drv = netdev_priv(pshm_dev->pshm_netdev);
	pshm_drv->pshm_dev = pshm_dev;

	/*
	 * Initialization starts with the verification of the
	 * availability of MBX driver by calling its setup function.
	 * MBX driver must be available by this time for proper
	 * functioning of SHM driver.
	 */
	if ((pshm_dev->pshmdev_mbxsetup
				(caif_shmdrv_rx_cb, pshm_dev, pshm_drv)) != 0) {
		pr_warn("Could not config. SHM Mailbox,"
				" Bailing out.....\n");
		free_netdev(pshm_dev->pshm_netdev);
		return -ENODEV;
	}

	skb_queue_head_init(&pshm_drv->sk_qhead);

	pr_info("SHM DEVICE[%d] PROBED BY DRIVER, NEW SHM DRIVER"
			" INSTANCE AT pshm_drv =0x%p\n",
			pshm_drv->pshm_dev->shm_id, pshm_drv);

	if (pshm_dev->shm_total_sz <
			(NR_TX_BUF * TX_BUF_SZ + NR_RX_BUF * RX_BUF_SZ)) {

		pr_warn("ERROR, Amount of available"
				" Phys. SHM cannot accommodate current SHM "
				"driver configuration, Bailing out ...\n");
		free_netdev(pshm_dev->pshm_netdev);
		return -ENOMEM;
	}

	pshm_drv->shm_base_addr = pshm_dev->shm_base_addr;
	pshm_drv->shm_tx_addr = pshm_drv->shm_base_addr;

	if (pshm_dev->shm_loopback)
		pshm_drv->shm_rx_addr = pshm_drv->shm_tx_addr;
	else
		pshm_drv->shm_rx_addr = pshm_dev->shm_base_addr +
						(NR_TX_BUF * TX_BUF_SZ);

	INIT_LIST_HEAD(&pshm_drv->tx_empty_list);
	INIT_LIST_HEAD(&pshm_drv->tx_pend_list);
	INIT_LIST_HEAD(&pshm_drv->tx_full_list);

	INIT_LIST_HEAD(&pshm_drv->rx_empty_list);
	INIT_LIST_HEAD(&pshm_drv->rx_pend_list);
	INIT_LIST_HEAD(&pshm_drv->rx_full_list);

	INIT_WORK(&pshm_drv->shm_tx_work, shm_tx_work_func);
	INIT_WORK(&pshm_drv->shm_rx_work, shm_rx_work_func);

	pshm_drv->pshm_tx_workqueue =
				create_singlethread_workqueue("shm_tx_work");
	pshm_drv->pshm_rx_workqueue =
				create_singlethread_workqueue("shm_rx_work");

	for (j = 0; j < NR_TX_BUF; j++) {
		struct buf_list *tx_buf =
				kmalloc(sizeof(struct buf_list), GFP_KERNEL);

		if (tx_buf == NULL) {
			pr_warn("ERROR, Could not"
					" allocate dynamic mem. for tx_buf,"
					" Bailing out ...\n");
			free_netdev(pshm_dev->pshm_netdev);
			return -ENOMEM;
		}
		tx_buf->index = j;
		tx_buf->phy_addr = pshm_drv->shm_tx_addr + (TX_BUF_SZ * j);
		tx_buf->len = TX_BUF_SZ;
		tx_buf->frames = 0;
		tx_buf->frm_ofs = SHM_CAIF_FRM_OFS;

		if (pshm_dev->shm_loopback)
			tx_buf->desc_vptr = (char *)tx_buf->phy_addr;
		else
			tx_buf->desc_vptr =
					ioremap(tx_buf->phy_addr, TX_BUF_SZ);

		list_add_tail(&tx_buf->list, &pshm_drv->tx_empty_list);
	}

	for (j = 0; j < NR_RX_BUF; j++) {
		struct buf_list *rx_buf =
				kmalloc(sizeof(struct buf_list), GFP_KERNEL);

		if (rx_buf == NULL) {
			pr_warn("ERROR, Could not"
					" allocate dynamic mem.for rx_buf,"
					" Bailing out ...\n");
			free_netdev(pshm_dev->pshm_netdev);
			return -ENOMEM;
		}
		rx_buf->index = j;
		rx_buf->phy_addr = pshm_drv->shm_rx_addr + (RX_BUF_SZ * j);
		rx_buf->len = RX_BUF_SZ;

		if (pshm_dev->shm_loopback)
			rx_buf->desc_vptr = (char *)rx_buf->phy_addr;
		else
			rx_buf->desc_vptr =
					ioremap(rx_buf->phy_addr, RX_BUF_SZ);
		list_add_tail(&rx_buf->list, &pshm_drv->rx_empty_list);
	}

	pshm_drv->tx_empty_available = 1;
	result = register_netdev(pshm_dev->pshm_netdev);
	if (result)
		pr_warn("ERROR[%d], SHM could not, "
			"register with NW FRMWK Bailing out ...\n", result);

	return result;
}

void caif_shmcore_remove(struct net_device *pshm_netdev)
{
	struct buf_list *pbuf;
	struct shmdrv_layer *pshm_drv = NULL;

	pshm_drv = netdev_priv(pshm_netdev);

	while (!(list_empty(&pshm_drv->tx_pend_list))) {
		pbuf =
			list_entry(pshm_drv->tx_pend_list.next,
					struct buf_list, list);

		list_del(&pbuf->list);
		kfree(pbuf);
	}

	while (!(list_empty(&pshm_drv->tx_full_list))) {
		pbuf =
			list_entry(pshm_drv->tx_full_list.next,
					struct buf_list, list);
		list_del(&pbuf->list);
		kfree(pbuf);
	}

	while (!(list_empty(&pshm_drv->tx_empty_list))) {
		pbuf =
			list_entry(pshm_drv->tx_empty_list.next,
					struct buf_list, list);
		list_del(&pbuf->list);
		kfree(pbuf);
	}

	while (!(list_empty(&pshm_drv->rx_full_list))) {
		pbuf =
			list_entry(pshm_drv->tx_full_list.next,
				struct buf_list, list);
		list_del(&pbuf->list);
		kfree(pbuf);
	}

	while (!(list_empty(&pshm_drv->rx_pend_list))) {
		pbuf =
			list_entry(pshm_drv->tx_pend_list.next,
				struct buf_list, list);
		list_del(&pbuf->list);
		kfree(pbuf);
	}

	while (!(list_empty(&pshm_drv->rx_empty_list))) {
		pbuf =
			list_entry(pshm_drv->rx_empty_list.next,
				struct buf_list, list);
		list_del(&pbuf->list);
		kfree(pbuf);
	}

	/* Destroy work queues. */
	destroy_workqueue(pshm_drv->pshm_tx_workqueue);
	destroy_workqueue(pshm_drv->pshm_rx_workqueue);

	unregister_netdev(pshm_netdev);
}
