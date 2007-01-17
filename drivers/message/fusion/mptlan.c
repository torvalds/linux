/*
 *  linux/drivers/message/fusion/mptlan.c
 *      IP Over Fibre Channel device driver.
 *      For use with LSI Logic Fibre Channel PCI chip/adapters
 *      running LSI Logic Fusion MPT (Message Passing Technology) firmware.
 *
 *  Copyright (c) 2000-2005 LSI Logic Corporation
 *
 */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    NO WARRANTY
    THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
    LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
    solely responsible for determining the appropriateness of using and
    distributing the Program and assumes all risks associated with its
    exercise of rights under this Agreement, including but not limited to
    the risks and costs of program errors, damage to or loss of data,
    programs or equipment, and unavailability or interruption of operations.

    DISCLAIMER OF LIABILITY
    NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
    TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
    USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
    HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 * Define statements used for debugging
 */
//#define MPT_LAN_IO_DEBUG

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#include "mptlan.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>

#define MYNAM		"mptlan"

MODULE_LICENSE("GPL");

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 * MPT LAN message sizes without variable part.
 */
#define MPT_LAN_RECEIVE_POST_REQUEST_SIZE \
	(sizeof(LANReceivePostRequest_t) - sizeof(SGE_MPI_UNION))

#define MPT_LAN_TRANSACTION32_SIZE \
	(sizeof(SGETransaction32_t) - sizeof(u32))

/*
 *  Fusion MPT LAN private structures
 */

struct NAA_Hosed {
	u16 NAA;
	u8 ieee[FC_ALEN];
	struct NAA_Hosed *next;
};

struct BufferControl {
	struct sk_buff	*skb;
	dma_addr_t	dma;
	unsigned int	len;
};

struct mpt_lan_priv {
	MPT_ADAPTER *mpt_dev;
	u8 pnum; /* Port number in the IOC. This is not a Unix network port! */

	atomic_t buckets_out;		/* number of unused buckets on IOC */
	int bucketthresh;		/* Send more when this many left */

	int *mpt_txfidx; /* Free Tx Context list */
	int mpt_txfidx_tail;
	spinlock_t txfidx_lock;

	int *mpt_rxfidx; /* Free Rx Context list */
	int mpt_rxfidx_tail;
	spinlock_t rxfidx_lock;

	struct BufferControl *RcvCtl;	/* Receive BufferControl structs */
	struct BufferControl *SendCtl;	/* Send BufferControl structs */

	int max_buckets_out;		/* Max buckets to send to IOC */
	int tx_max_out;			/* IOC's Tx queue len */

	u32 total_posted;
	u32 total_received;
	struct net_device_stats stats;	/* Per device statistics */

	struct delayed_work post_buckets_task;
	struct net_device *dev;
	unsigned long post_buckets_active;
};

struct mpt_lan_ohdr {
	u16	dtype;
	u8	daddr[FC_ALEN];
	u16	stype;
	u8	saddr[FC_ALEN];
};

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

/*
 *  Forward protos...
 */
static int  lan_reply (MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf,
		       MPT_FRAME_HDR *reply);
static int  mpt_lan_open(struct net_device *dev);
static int  mpt_lan_reset(struct net_device *dev);
static int  mpt_lan_close(struct net_device *dev);
static void mpt_lan_post_receive_buckets(struct mpt_lan_priv *priv);
static void mpt_lan_wake_post_buckets_task(struct net_device *dev,
					   int priority);
static int  mpt_lan_receive_post_turbo(struct net_device *dev, u32 tmsg);
static int  mpt_lan_receive_post_reply(struct net_device *dev,
				       LANReceivePostReply_t *pRecvRep);
static int  mpt_lan_send_turbo(struct net_device *dev, u32 tmsg);
static int  mpt_lan_send_reply(struct net_device *dev,
			       LANSendReply_t *pSendRep);
static int  mpt_lan_ioc_reset(MPT_ADAPTER *ioc, int reset_phase);
static int  mpt_lan_event_process(MPT_ADAPTER *ioc, EventNotificationReply_t *pEvReply);
static unsigned short mpt_lan_type_trans(struct sk_buff *skb,
					 struct net_device *dev);

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  Fusion MPT LAN private data
 */
static int LanCtx = -1;

static u32 max_buckets_out = 127;
static u32 tx_max_out_p = 127 - 16;

#ifdef QLOGIC_NAA_WORKAROUND
static struct NAA_Hosed *mpt_bad_naa = NULL;
DEFINE_RWLOCK(bad_naa_lock);
#endif

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 * Fusion MPT LAN external data
 */
extern int mpt_lan_index;

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	lan_reply - Handle all data sent from the hardware.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@mf: Pointer to original MPT request frame (NULL if TurboReply)
 *	@reply: Pointer to MPT reply frame
 *
 *	Returns 1 indicating original alloc'd request frame ptr
 *	should be freed, or 0 if it shouldn't.
 */
static int
lan_reply (MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf, MPT_FRAME_HDR *reply)
{
	struct net_device *dev = ioc->netdev;
	int FreeReqFrame = 0;

	dioprintk((KERN_INFO MYNAM ": %s/%s: Got reply.\n",
		  IOC_AND_NETDEV_NAMES_s_s(dev)));

//	dioprintk((KERN_INFO MYNAM "@lan_reply: mf = %p, reply = %p\n",
//			mf, reply));

	if (mf == NULL) {
		u32 tmsg = CAST_PTR_TO_U32(reply);

		dioprintk((KERN_INFO MYNAM ": %s/%s: @lan_reply, tmsg %08x\n",
				IOC_AND_NETDEV_NAMES_s_s(dev),
				tmsg));

		switch (GET_LAN_FORM(tmsg)) {

		// NOTE!  (Optimization) First case here is now caught in
		//  mptbase.c::mpt_interrupt() routine and callcack here
		//  is now skipped for this case!
#if 0
		case LAN_REPLY_FORM_MESSAGE_CONTEXT:
//			dioprintk((KERN_INFO MYNAM "/lan_reply: "
//				  "MessageContext turbo reply received\n"));
			FreeReqFrame = 1;
			break;
#endif

		case LAN_REPLY_FORM_SEND_SINGLE:
//			dioprintk((MYNAM "/lan_reply: "
//				  "calling mpt_lan_send_reply (turbo)\n"));

			// Potential BUG here?
			//	FreeReqFrame = mpt_lan_send_turbo(dev, tmsg);
			//  If/when mpt_lan_send_turbo would return 1 here,
			//  calling routine (mptbase.c|mpt_interrupt)
			//  would Oops because mf has already been set
			//  to NULL.  So after return from this func,
			//  mpt_interrupt() will attempt to put (NULL) mf ptr
			//  item back onto its adapter FreeQ - Oops!:-(
			//  It's Ok, since mpt_lan_send_turbo() *currently*
			//  always returns 0, but..., just in case:

			(void) mpt_lan_send_turbo(dev, tmsg);
			FreeReqFrame = 0;

			break;

		case LAN_REPLY_FORM_RECEIVE_SINGLE:
//			dioprintk((KERN_INFO MYNAM "@lan_reply: "
//				  "rcv-Turbo = %08x\n", tmsg));
			mpt_lan_receive_post_turbo(dev, tmsg);
			break;

		default:
			printk (KERN_ERR MYNAM "/lan_reply: Got a turbo reply "
				"that I don't know what to do with\n");

			/* CHECKME!  Hmmm...  FreeReqFrame is 0 here; is that right? */

			break;
		}

		return FreeReqFrame;
	}

//	msg = (u32 *) reply;
//	dioprintk((KERN_INFO MYNAM "@lan_reply: msg = %08x %08x %08x %08x\n",
//		  le32_to_cpu(msg[0]), le32_to_cpu(msg[1]),
//		  le32_to_cpu(msg[2]), le32_to_cpu(msg[3])));
//	dioprintk((KERN_INFO MYNAM "@lan_reply: Function = %02xh\n",
//		  reply->u.hdr.Function));

	switch (reply->u.hdr.Function) {

	case MPI_FUNCTION_LAN_SEND:
	{
		LANSendReply_t *pSendRep;

		pSendRep = (LANSendReply_t *) reply;
		FreeReqFrame = mpt_lan_send_reply(dev, pSendRep);
		break;
	}

	case MPI_FUNCTION_LAN_RECEIVE:
	{
		LANReceivePostReply_t *pRecvRep;

		pRecvRep = (LANReceivePostReply_t *) reply;
		if (pRecvRep->NumberOfContexts) {
			mpt_lan_receive_post_reply(dev, pRecvRep);
			if (!(pRecvRep->MsgFlags & MPI_MSGFLAGS_CONTINUATION_REPLY))
				FreeReqFrame = 1;
		} else
			dioprintk((KERN_INFO MYNAM "@lan_reply: zero context "
				  "ReceivePostReply received.\n"));
		break;
	}

	case MPI_FUNCTION_LAN_RESET:
		/* Just a default reply. Might want to check it to
		 * make sure that everything went ok.
		 */
		FreeReqFrame = 1;
		break;

	case MPI_FUNCTION_EVENT_NOTIFICATION:
	case MPI_FUNCTION_EVENT_ACK:
		/*  _EVENT_NOTIFICATION should NOT come down this path any more.
		 *  Should be routed to mpt_lan_event_process(), but just in case...
		 */
		FreeReqFrame = 1;
		break;

	default:
		printk (KERN_ERR MYNAM "/lan_reply: Got a non-turbo "
			"reply that I don't know what to do with\n");

		/* CHECKME!  Hmmm...  FreeReqFrame is 0 here; is that right? */
		FreeReqFrame = 1;

		break;
	}

	return FreeReqFrame;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int
mpt_lan_ioc_reset(MPT_ADAPTER *ioc, int reset_phase)
{
	struct net_device *dev = ioc->netdev;
	struct mpt_lan_priv *priv;

	if (dev == NULL)
		return(1);
	else
		priv = netdev_priv(dev);

	dlprintk((KERN_INFO MYNAM ": IOC %s_reset routed to LAN driver!\n",
			reset_phase==MPT_IOC_SETUP_RESET ? "setup" : (
			reset_phase==MPT_IOC_PRE_RESET ? "pre" : "post")));

	if (priv->mpt_rxfidx == NULL)
		return (1);

	if (reset_phase == MPT_IOC_SETUP_RESET) {
		;
	} else if (reset_phase == MPT_IOC_PRE_RESET) {
		int i;
		unsigned long flags;

		netif_stop_queue(dev);

		dlprintk ((KERN_INFO "mptlan/ioc_reset: called netif_stop_queue for %s.\n", dev->name));

		atomic_set(&priv->buckets_out, 0);

		/* Reset Rx Free Tail index and re-populate the queue. */
		spin_lock_irqsave(&priv->rxfidx_lock, flags);
		priv->mpt_rxfidx_tail = -1;
		for (i = 0; i < priv->max_buckets_out; i++)
			priv->mpt_rxfidx[++priv->mpt_rxfidx_tail] = i;
		spin_unlock_irqrestore(&priv->rxfidx_lock, flags);
	} else {
		mpt_lan_post_receive_buckets(priv);
		netif_wake_queue(dev);
	}

	return 1;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int
mpt_lan_event_process(MPT_ADAPTER *ioc, EventNotificationReply_t *pEvReply)
{
	dlprintk((KERN_INFO MYNAM ": MPT event routed to LAN driver!\n"));

	switch (le32_to_cpu(pEvReply->Event)) {
	case MPI_EVENT_NONE:				/* 00 */
	case MPI_EVENT_LOG_DATA:			/* 01 */
	case MPI_EVENT_STATE_CHANGE:			/* 02 */
	case MPI_EVENT_UNIT_ATTENTION:			/* 03 */
	case MPI_EVENT_IOC_BUS_RESET:			/* 04 */
	case MPI_EVENT_EXT_BUS_RESET:			/* 05 */
	case MPI_EVENT_RESCAN:				/* 06 */
		/* Ok, do we need to do anything here? As far as
		   I can tell, this is when a new device gets added
		   to the loop. */
	case MPI_EVENT_LINK_STATUS_CHANGE:		/* 07 */
	case MPI_EVENT_LOOP_STATE_CHANGE:		/* 08 */
	case MPI_EVENT_LOGOUT:				/* 09 */
	case MPI_EVENT_EVENT_CHANGE:			/* 0A */
	default:
		break;
	}

	/*
	 *  NOTE: pEvent->AckRequired handling now done in mptbase.c;
	 *  Do NOT do it here now!
	 */

	return 1;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int
mpt_lan_open(struct net_device *dev)
{
	struct mpt_lan_priv *priv = netdev_priv(dev);
	int i;

	if (mpt_lan_reset(dev) != 0) {
		MPT_ADAPTER *mpt_dev = priv->mpt_dev;

		printk (KERN_WARNING MYNAM "/lan_open: lan_reset failed.");

		if (mpt_dev->active)
			printk ("The ioc is active. Perhaps it needs to be"
				" reset?\n");
		else
			printk ("The ioc in inactive, most likely in the "
				"process of being reset. Please try again in "
				"a moment.\n");
	}

	priv->mpt_txfidx = kmalloc(priv->tx_max_out * sizeof(int), GFP_KERNEL);
	if (priv->mpt_txfidx == NULL)
		goto out;
	priv->mpt_txfidx_tail = -1;

	priv->SendCtl = kcalloc(priv->tx_max_out, sizeof(struct BufferControl),
				GFP_KERNEL);
	if (priv->SendCtl == NULL)
		goto out_mpt_txfidx;
	for (i = 0; i < priv->tx_max_out; i++)
		priv->mpt_txfidx[++priv->mpt_txfidx_tail] = i;

	dlprintk((KERN_INFO MYNAM "@lo: Finished initializing SendCtl\n"));

	priv->mpt_rxfidx = kmalloc(priv->max_buckets_out * sizeof(int),
				   GFP_KERNEL);
	if (priv->mpt_rxfidx == NULL)
		goto out_SendCtl;
	priv->mpt_rxfidx_tail = -1;

	priv->RcvCtl = kcalloc(priv->max_buckets_out,
			       sizeof(struct BufferControl),
			       GFP_KERNEL);
	if (priv->RcvCtl == NULL)
		goto out_mpt_rxfidx;
	for (i = 0; i < priv->max_buckets_out; i++)
		priv->mpt_rxfidx[++priv->mpt_rxfidx_tail] = i;

/**/	dlprintk((KERN_INFO MYNAM "/lo: txfidx contains - "));
/**/	for (i = 0; i < priv->tx_max_out; i++)
/**/		dlprintk((" %xh", priv->mpt_txfidx[i]));
/**/	dlprintk(("\n"));

	dlprintk((KERN_INFO MYNAM "/lo: Finished initializing RcvCtl\n"));

	mpt_lan_post_receive_buckets(priv);
	printk(KERN_INFO MYNAM ": %s/%s: interface up & active\n",
			IOC_AND_NETDEV_NAMES_s_s(dev));

	if (mpt_event_register(LanCtx, mpt_lan_event_process) != 0) {
		printk (KERN_WARNING MYNAM "/lo: Unable to register for Event"
			" Notifications. This is a bad thing! We're not going "
			"to go ahead, but I'd be leery of system stability at "
			"this point.\n");
	}

	netif_start_queue(dev);
	dlprintk((KERN_INFO MYNAM "/lo: Done.\n"));

	return 0;
out_mpt_rxfidx:
	kfree(priv->mpt_rxfidx);
	priv->mpt_rxfidx = NULL;
out_SendCtl:
	kfree(priv->SendCtl);
	priv->SendCtl = NULL;
out_mpt_txfidx:
	kfree(priv->mpt_txfidx);
	priv->mpt_txfidx = NULL;
out:	return -ENOMEM;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Send a LanReset message to the FW. This should result in the FW returning
   any buckets it still has. */
static int
mpt_lan_reset(struct net_device *dev)
{
	MPT_FRAME_HDR *mf;
	LANResetRequest_t *pResetReq;
	struct mpt_lan_priv *priv = netdev_priv(dev);

	mf = mpt_get_msg_frame(LanCtx, priv->mpt_dev);

	if (mf == NULL) {
/*		dlprintk((KERN_ERR MYNAM "/reset: Evil funkiness abounds! "
		"Unable to allocate a request frame.\n"));
*/
		return -1;
	}

	pResetReq = (LANResetRequest_t *) mf;

	pResetReq->Function	= MPI_FUNCTION_LAN_RESET;
	pResetReq->ChainOffset	= 0;
	pResetReq->Reserved	= 0;
	pResetReq->PortNumber	= priv->pnum;
	pResetReq->MsgFlags	= 0;
	pResetReq->Reserved2	= 0;

	mpt_put_msg_frame(LanCtx, priv->mpt_dev, mf);

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int
mpt_lan_close(struct net_device *dev)
{
	struct mpt_lan_priv *priv = netdev_priv(dev);
	MPT_ADAPTER *mpt_dev = priv->mpt_dev;
	unsigned long timeout;
	int i;

	dlprintk((KERN_INFO MYNAM ": mpt_lan_close called\n"));

	mpt_event_deregister(LanCtx);

	dlprintk((KERN_INFO MYNAM ":lan_close: Posted %d buckets "
		  "since driver was loaded, %d still out\n",
		  priv->total_posted,atomic_read(&priv->buckets_out)));

	netif_stop_queue(dev);

	mpt_lan_reset(dev);

	timeout = jiffies + 2 * HZ;
	while (atomic_read(&priv->buckets_out) && time_before(jiffies, timeout))
		schedule_timeout_interruptible(1);

	for (i = 0; i < priv->max_buckets_out; i++) {
		if (priv->RcvCtl[i].skb != NULL) {
/**/			dlprintk((KERN_INFO MYNAM "/lan_close: bucket %05x "
/**/				  "is still out\n", i));
			pci_unmap_single(mpt_dev->pcidev, priv->RcvCtl[i].dma,
					 priv->RcvCtl[i].len,
					 PCI_DMA_FROMDEVICE);
			dev_kfree_skb(priv->RcvCtl[i].skb);
		}
	}

	kfree(priv->RcvCtl);
	kfree(priv->mpt_rxfidx);

	for (i = 0; i < priv->tx_max_out; i++) {
		if (priv->SendCtl[i].skb != NULL) {
			pci_unmap_single(mpt_dev->pcidev, priv->SendCtl[i].dma,
					 priv->SendCtl[i].len,
					 PCI_DMA_TODEVICE);
			dev_kfree_skb(priv->SendCtl[i].skb);
		}
	}

	kfree(priv->SendCtl);
	kfree(priv->mpt_txfidx);

	atomic_set(&priv->buckets_out, 0);

	printk(KERN_INFO MYNAM ": %s/%s: interface down & inactive\n",
			IOC_AND_NETDEV_NAMES_s_s(dev));

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static struct net_device_stats *
mpt_lan_get_stats(struct net_device *dev)
{
	struct mpt_lan_priv *priv = netdev_priv(dev);

	return (struct net_device_stats *) &priv->stats;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int
mpt_lan_change_mtu(struct net_device *dev, int new_mtu)
{
	if ((new_mtu < MPT_LAN_MIN_MTU) || (new_mtu > MPT_LAN_MAX_MTU))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Tx timeout handler. */
static void
mpt_lan_tx_timeout(struct net_device *dev)
{
	struct mpt_lan_priv *priv = netdev_priv(dev);
	MPT_ADAPTER *mpt_dev = priv->mpt_dev;

	if (mpt_dev->active) {
		dlprintk (("mptlan/tx_timeout: calling netif_wake_queue for %s.\n", dev->name));
		netif_wake_queue(dev);
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
//static inline int
static int
mpt_lan_send_turbo(struct net_device *dev, u32 tmsg)
{
	struct mpt_lan_priv *priv = netdev_priv(dev);
	MPT_ADAPTER *mpt_dev = priv->mpt_dev;
	struct sk_buff *sent;
	unsigned long flags;
	u32 ctx;

	ctx = GET_LAN_BUFFER_CONTEXT(tmsg);
	sent = priv->SendCtl[ctx].skb;

	priv->stats.tx_packets++;
	priv->stats.tx_bytes += sent->len;

	dioprintk((KERN_INFO MYNAM ": %s/%s: @%s, skb %p sent.\n",
			IOC_AND_NETDEV_NAMES_s_s(dev),
			__FUNCTION__, sent));

	priv->SendCtl[ctx].skb = NULL;
	pci_unmap_single(mpt_dev->pcidev, priv->SendCtl[ctx].dma,
			 priv->SendCtl[ctx].len, PCI_DMA_TODEVICE);
	dev_kfree_skb_irq(sent);

	spin_lock_irqsave(&priv->txfidx_lock, flags);
	priv->mpt_txfidx[++priv->mpt_txfidx_tail] = ctx;
	spin_unlock_irqrestore(&priv->txfidx_lock, flags);

	netif_wake_queue(dev);
	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int
mpt_lan_send_reply(struct net_device *dev, LANSendReply_t *pSendRep)
{
	struct mpt_lan_priv *priv = netdev_priv(dev);
	MPT_ADAPTER *mpt_dev = priv->mpt_dev;
	struct sk_buff *sent;
	unsigned long flags;
	int FreeReqFrame = 0;
	u32 *pContext;
	u32 ctx;
	u8 count;

	count = pSendRep->NumberOfContexts;

	dioprintk((KERN_INFO MYNAM ": send_reply: IOCStatus: %04x\n",
		 le16_to_cpu(pSendRep->IOCStatus)));

	/* Add check for Loginfo Flag in IOCStatus */

	switch (le16_to_cpu(pSendRep->IOCStatus) & MPI_IOCSTATUS_MASK) {
	case MPI_IOCSTATUS_SUCCESS:
		priv->stats.tx_packets += count;
		break;

	case MPI_IOCSTATUS_LAN_CANCELED:
	case MPI_IOCSTATUS_LAN_TRANSMIT_ABORTED:
		break;

	case MPI_IOCSTATUS_INVALID_SGL:
		priv->stats.tx_errors += count;
		printk (KERN_ERR MYNAM ": %s/%s: ERROR - Invalid SGL sent to IOC!\n",
				IOC_AND_NETDEV_NAMES_s_s(dev));
		goto out;

	default:
		priv->stats.tx_errors += count;
		break;
	}

	pContext = &pSendRep->BufferContext;

	spin_lock_irqsave(&priv->txfidx_lock, flags);
	while (count > 0) {
		ctx = GET_LAN_BUFFER_CONTEXT(le32_to_cpu(*pContext));

		sent = priv->SendCtl[ctx].skb;
		priv->stats.tx_bytes += sent->len;

		dioprintk((KERN_INFO MYNAM ": %s/%s: @%s, skb %p sent.\n",
				IOC_AND_NETDEV_NAMES_s_s(dev),
				__FUNCTION__, sent));

		priv->SendCtl[ctx].skb = NULL;
		pci_unmap_single(mpt_dev->pcidev, priv->SendCtl[ctx].dma,
				 priv->SendCtl[ctx].len, PCI_DMA_TODEVICE);
		dev_kfree_skb_irq(sent);

		priv->mpt_txfidx[++priv->mpt_txfidx_tail] = ctx;

		pContext++;
		count--;
	}
	spin_unlock_irqrestore(&priv->txfidx_lock, flags);

out:
	if (!(pSendRep->MsgFlags & MPI_MSGFLAGS_CONTINUATION_REPLY))
		FreeReqFrame = 1;

	netif_wake_queue(dev);
	return FreeReqFrame;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int
mpt_lan_sdu_send (struct sk_buff *skb, struct net_device *dev)
{
	struct mpt_lan_priv *priv = netdev_priv(dev);
	MPT_ADAPTER *mpt_dev = priv->mpt_dev;
	MPT_FRAME_HDR *mf;
	LANSendRequest_t *pSendReq;
	SGETransaction32_t *pTrans;
	SGESimple64_t *pSimple;
	dma_addr_t dma;
	unsigned long flags;
	int ctx;
	u16 cur_naa = 0x1000;

	dioprintk((KERN_INFO MYNAM ": %s called, skb_addr = %p\n",
			__FUNCTION__, skb));

	spin_lock_irqsave(&priv->txfidx_lock, flags);
	if (priv->mpt_txfidx_tail < 0) {
		netif_stop_queue(dev);
		spin_unlock_irqrestore(&priv->txfidx_lock, flags);

		printk (KERN_ERR "%s: no tx context available: %u\n",
			__FUNCTION__, priv->mpt_txfidx_tail);
		return 1;
	}

	mf = mpt_get_msg_frame(LanCtx, mpt_dev);
	if (mf == NULL) {
		netif_stop_queue(dev);
		spin_unlock_irqrestore(&priv->txfidx_lock, flags);

		printk (KERN_ERR "%s: Unable to alloc request frame\n",
			__FUNCTION__);
		return 1;
	}

	ctx = priv->mpt_txfidx[priv->mpt_txfidx_tail--];
	spin_unlock_irqrestore(&priv->txfidx_lock, flags);

//	dioprintk((KERN_INFO MYNAM ": %s/%s: Creating new msg frame (send).\n",
//			IOC_AND_NETDEV_NAMES_s_s(dev)));

	pSendReq = (LANSendRequest_t *) mf;

	/* Set the mac.raw pointer, since this apparently isn't getting
	 * done before we get the skb. Pull the data pointer past the mac data.
	 */
	skb->mac.raw = skb->data;
	skb_pull(skb, 12);

        dma = pci_map_single(mpt_dev->pcidev, skb->data, skb->len,
			     PCI_DMA_TODEVICE);

	priv->SendCtl[ctx].skb = skb;
	priv->SendCtl[ctx].dma = dma;
	priv->SendCtl[ctx].len = skb->len;

	/* Message Header */
	pSendReq->Reserved    = 0;
	pSendReq->Function    = MPI_FUNCTION_LAN_SEND;
	pSendReq->ChainOffset = 0;
	pSendReq->Reserved2   = 0;
	pSendReq->MsgFlags    = 0;
	pSendReq->PortNumber  = priv->pnum;

	/* Transaction Context Element */
	pTrans = (SGETransaction32_t *) pSendReq->SG_List;

	/* No Flags, 8 bytes of Details, 32bit Context (bloody turbo replies) */
	pTrans->ContextSize   = sizeof(u32);
	pTrans->DetailsLength = 2 * sizeof(u32);
	pTrans->Flags         = 0;
	pTrans->TransactionContext[0] = cpu_to_le32(ctx);

//	dioprintk((KERN_INFO MYNAM ": %s/%s: BC = %08x, skb = %p, buff = %p\n",
//			IOC_AND_NETDEV_NAMES_s_s(dev),
//			ctx, skb, skb->data));

#ifdef QLOGIC_NAA_WORKAROUND
{
	struct NAA_Hosed *nh;

	/* Munge the NAA for Tx packets to QLogic boards, which don't follow
	   RFC 2625. The longer I look at this, the more my opinion of Qlogic
	   drops. */
	read_lock_irq(&bad_naa_lock);
	for (nh = mpt_bad_naa; nh != NULL; nh=nh->next) {
		if ((nh->ieee[0] == skb->mac.raw[0]) &&
		    (nh->ieee[1] == skb->mac.raw[1]) &&
		    (nh->ieee[2] == skb->mac.raw[2]) &&
		    (nh->ieee[3] == skb->mac.raw[3]) &&
		    (nh->ieee[4] == skb->mac.raw[4]) &&
		    (nh->ieee[5] == skb->mac.raw[5])) {
			cur_naa = nh->NAA;
			dlprintk ((KERN_INFO "mptlan/sdu_send: using NAA value "
				  "= %04x.\n", cur_naa));
			break;
		}
	}
	read_unlock_irq(&bad_naa_lock);
}
#endif

	pTrans->TransactionDetails[0] = cpu_to_le32((cur_naa         << 16) |
						    (skb->mac.raw[0] <<  8) |
						    (skb->mac.raw[1] <<  0));
	pTrans->TransactionDetails[1] = cpu_to_le32((skb->mac.raw[2] << 24) |
						    (skb->mac.raw[3] << 16) |
						    (skb->mac.raw[4] <<  8) |
						    (skb->mac.raw[5] <<  0));

	pSimple = (SGESimple64_t *) &pTrans->TransactionDetails[2];

	/* If we ever decide to send more than one Simple SGE per LANSend, then
	   we will need to make sure that LAST_ELEMENT only gets set on the
	   last one. Otherwise, bad voodoo and evil funkiness will commence. */
	pSimple->FlagsLength = cpu_to_le32(
			((MPI_SGE_FLAGS_LAST_ELEMENT |
			  MPI_SGE_FLAGS_END_OF_BUFFER |
			  MPI_SGE_FLAGS_SIMPLE_ELEMENT |
			  MPI_SGE_FLAGS_SYSTEM_ADDRESS |
			  MPI_SGE_FLAGS_HOST_TO_IOC |
			  MPI_SGE_FLAGS_64_BIT_ADDRESSING |
			  MPI_SGE_FLAGS_END_OF_LIST) << MPI_SGE_FLAGS_SHIFT) |
			skb->len);
	pSimple->Address.Low = cpu_to_le32((u32) dma);
	if (sizeof(dma_addr_t) > sizeof(u32))
		pSimple->Address.High = cpu_to_le32((u32) ((u64) dma >> 32));
	else
		pSimple->Address.High = 0;

	mpt_put_msg_frame (LanCtx, mpt_dev, mf);
	dev->trans_start = jiffies;

	dioprintk((KERN_INFO MYNAM ": %s/%s: Sending packet. FlagsLength = %08x.\n",
			IOC_AND_NETDEV_NAMES_s_s(dev),
			le32_to_cpu(pSimple->FlagsLength)));

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static void
mpt_lan_wake_post_buckets_task(struct net_device *dev, int priority)
/*
 * @priority: 0 = put it on the timer queue, 1 = put it on the immediate queue
 */
{
	struct mpt_lan_priv *priv = dev->priv;
	
	if (test_and_set_bit(0, &priv->post_buckets_active) == 0) {
		if (priority) {
			schedule_delayed_work(&priv->post_buckets_task, 0);
		} else {
			schedule_delayed_work(&priv->post_buckets_task, 1);
			dioprintk((KERN_INFO MYNAM ": post_buckets queued on "
				   "timer.\n"));
		}
	        dioprintk((KERN_INFO MYNAM ": %s/%s: Queued post_buckets task.\n",
			   IOC_AND_NETDEV_NAMES_s_s(dev) ));
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int
mpt_lan_receive_skb(struct net_device *dev, struct sk_buff *skb)
{
	struct mpt_lan_priv *priv = dev->priv;

	skb->protocol = mpt_lan_type_trans(skb, dev);

	dioprintk((KERN_INFO MYNAM ": %s/%s: Incoming packet (%d bytes) "
		 "delivered to upper level.\n",
			IOC_AND_NETDEV_NAMES_s_s(dev), skb->len));

	priv->stats.rx_bytes += skb->len;
	priv->stats.rx_packets++;

	skb->dev = dev;
	netif_rx(skb);

	dioprintk((MYNAM "/receive_skb: %d buckets remaining\n",
		 atomic_read(&priv->buckets_out)));

	if (atomic_read(&priv->buckets_out) < priv->bucketthresh)
		mpt_lan_wake_post_buckets_task(dev, 1);

	dioprintk((KERN_INFO MYNAM "/receive_post_reply: %d buckets "
		  "remaining, %d received back since sod\n",
		  atomic_read(&priv->buckets_out), priv->total_received));

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
//static inline int
static int
mpt_lan_receive_post_turbo(struct net_device *dev, u32 tmsg)
{
	struct mpt_lan_priv *priv = dev->priv;
	MPT_ADAPTER *mpt_dev = priv->mpt_dev;
	struct sk_buff *skb, *old_skb;
	unsigned long flags;
	u32 ctx, len;

	ctx = GET_LAN_BUCKET_CONTEXT(tmsg);
	skb = priv->RcvCtl[ctx].skb;

	len = GET_LAN_PACKET_LENGTH(tmsg);

	if (len < MPT_LAN_RX_COPYBREAK) {
		old_skb = skb;

		skb = (struct sk_buff *)dev_alloc_skb(len);
		if (!skb) {
			printk (KERN_ERR MYNAM ": %s/%s: ERROR - Can't allocate skb! (%s@%d)\n",
					IOC_AND_NETDEV_NAMES_s_s(dev),
					__FILE__, __LINE__);
			return -ENOMEM;
		}

		pci_dma_sync_single_for_cpu(mpt_dev->pcidev, priv->RcvCtl[ctx].dma,
					    priv->RcvCtl[ctx].len, PCI_DMA_FROMDEVICE);

		memcpy(skb_put(skb, len), old_skb->data, len);

		pci_dma_sync_single_for_device(mpt_dev->pcidev, priv->RcvCtl[ctx].dma,
					       priv->RcvCtl[ctx].len, PCI_DMA_FROMDEVICE);
		goto out;
	}

	skb_put(skb, len);

	priv->RcvCtl[ctx].skb = NULL;

	pci_unmap_single(mpt_dev->pcidev, priv->RcvCtl[ctx].dma,
			 priv->RcvCtl[ctx].len, PCI_DMA_FROMDEVICE);

out:
	spin_lock_irqsave(&priv->rxfidx_lock, flags);
	priv->mpt_rxfidx[++priv->mpt_rxfidx_tail] = ctx;
	spin_unlock_irqrestore(&priv->rxfidx_lock, flags);

	atomic_dec(&priv->buckets_out);
	priv->total_received++;

	return mpt_lan_receive_skb(dev, skb);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int
mpt_lan_receive_post_free(struct net_device *dev,
			  LANReceivePostReply_t *pRecvRep)
{
	struct mpt_lan_priv *priv = dev->priv;
	MPT_ADAPTER *mpt_dev = priv->mpt_dev;
	unsigned long flags;
	struct sk_buff *skb;
	u32 ctx;
	int count;
	int i;

	count = pRecvRep->NumberOfContexts;

/**/	dlprintk((KERN_INFO MYNAM "/receive_post_reply: "
		  "IOC returned %d buckets, freeing them...\n", count));

	spin_lock_irqsave(&priv->rxfidx_lock, flags);
	for (i = 0; i < count; i++) {
		ctx = le32_to_cpu(pRecvRep->BucketContext[i]);

		skb = priv->RcvCtl[ctx].skb;

//		dlprintk((KERN_INFO MYNAM ": %s: dev_name = %s\n",
//				IOC_AND_NETDEV_NAMES_s_s(dev)));
//		dlprintk((KERN_INFO MYNAM "@rpr[2], priv = %p, buckets_out addr = %p",
//				priv, &(priv->buckets_out)));
//		dlprintk((KERN_INFO MYNAM "@rpr[2] TC + 3\n"));

		priv->RcvCtl[ctx].skb = NULL;
		pci_unmap_single(mpt_dev->pcidev, priv->RcvCtl[ctx].dma,
				 priv->RcvCtl[ctx].len, PCI_DMA_FROMDEVICE);
		dev_kfree_skb_any(skb);

		priv->mpt_rxfidx[++priv->mpt_rxfidx_tail] = ctx;
	}
	spin_unlock_irqrestore(&priv->rxfidx_lock, flags);

	atomic_sub(count, &priv->buckets_out);

//	for (i = 0; i < priv->max_buckets_out; i++)
//		if (priv->RcvCtl[i].skb != NULL)
//			dlprintk((KERN_INFO MYNAM "@rpr: bucket %03x "
//				  "is still out\n", i));

/*	dlprintk((KERN_INFO MYNAM "/receive_post_reply: freed %d buckets\n",
		  count));
*/
/**/	dlprintk((KERN_INFO MYNAM "@receive_post_reply: %d buckets "
/**/		  "remaining, %d received back since sod.\n",
/**/		  atomic_read(&priv->buckets_out), priv->total_received));
	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int
mpt_lan_receive_post_reply(struct net_device *dev,
			   LANReceivePostReply_t *pRecvRep)
{
	struct mpt_lan_priv *priv = dev->priv;
	MPT_ADAPTER *mpt_dev = priv->mpt_dev;
	struct sk_buff *skb, *old_skb;
	unsigned long flags;
	u32 len, ctx, offset;
	u32 remaining = le32_to_cpu(pRecvRep->BucketsRemaining);
	int count;
	int i, l;

	dioprintk((KERN_INFO MYNAM ": mpt_lan_receive_post_reply called\n"));
	dioprintk((KERN_INFO MYNAM ": receive_post_reply: IOCStatus: %04x\n",
		 le16_to_cpu(pRecvRep->IOCStatus)));

	if ((le16_to_cpu(pRecvRep->IOCStatus) & MPI_IOCSTATUS_MASK) ==
						MPI_IOCSTATUS_LAN_CANCELED)
		return mpt_lan_receive_post_free(dev, pRecvRep);

	len = le32_to_cpu(pRecvRep->PacketLength);
	if (len == 0) {
		printk (KERN_ERR MYNAM ": %s/%s: ERROR - Got a non-TURBO "
			"ReceivePostReply w/ PacketLength zero!\n",
				IOC_AND_NETDEV_NAMES_s_s(dev));
		printk (KERN_ERR MYNAM ": MsgFlags = %02x, IOCStatus = %04x\n",
				pRecvRep->MsgFlags, le16_to_cpu(pRecvRep->IOCStatus));
		return -1;
	}

	ctx    = le32_to_cpu(pRecvRep->BucketContext[0]);
	count  = pRecvRep->NumberOfContexts;
	skb    = priv->RcvCtl[ctx].skb;

	offset = le32_to_cpu(pRecvRep->PacketOffset);
//	if (offset != 0) {
//		printk (KERN_INFO MYNAM ": %s/%s: Got a ReceivePostReply "
//			"w/ PacketOffset %u\n",
//				IOC_AND_NETDEV_NAMES_s_s(dev),
//				offset);
//	}

	dioprintk((KERN_INFO MYNAM ": %s/%s: @rpr, offset = %d, len = %d\n",
			IOC_AND_NETDEV_NAMES_s_s(dev),
			offset, len));

	if (count > 1) {
		int szrem = len;

//		dioprintk((KERN_INFO MYNAM ": %s/%s: Multiple buckets returned "
//			"for single packet, concatenating...\n",
//				IOC_AND_NETDEV_NAMES_s_s(dev)));

		skb = (struct sk_buff *)dev_alloc_skb(len);
		if (!skb) {
			printk (KERN_ERR MYNAM ": %s/%s: ERROR - Can't allocate skb! (%s@%d)\n",
					IOC_AND_NETDEV_NAMES_s_s(dev),
					__FILE__, __LINE__);
			return -ENOMEM;
		}

		spin_lock_irqsave(&priv->rxfidx_lock, flags);
		for (i = 0; i < count; i++) {

			ctx = le32_to_cpu(pRecvRep->BucketContext[i]);
			old_skb = priv->RcvCtl[ctx].skb;

			l = priv->RcvCtl[ctx].len;
			if (szrem < l)
				l = szrem;

//			dioprintk((KERN_INFO MYNAM ": %s/%s: Buckets = %d, len = %u\n",
//					IOC_AND_NETDEV_NAMES_s_s(dev),
//					i, l));

			pci_dma_sync_single_for_cpu(mpt_dev->pcidev,
						    priv->RcvCtl[ctx].dma,
						    priv->RcvCtl[ctx].len,
						    PCI_DMA_FROMDEVICE);
			memcpy(skb_put(skb, l), old_skb->data, l);

			pci_dma_sync_single_for_device(mpt_dev->pcidev,
						       priv->RcvCtl[ctx].dma,
						       priv->RcvCtl[ctx].len,
						       PCI_DMA_FROMDEVICE);

			priv->mpt_rxfidx[++priv->mpt_rxfidx_tail] = ctx;
			szrem -= l;
		}
		spin_unlock_irqrestore(&priv->rxfidx_lock, flags);

	} else if (len < MPT_LAN_RX_COPYBREAK) {

		old_skb = skb;

		skb = (struct sk_buff *)dev_alloc_skb(len);
		if (!skb) {
			printk (KERN_ERR MYNAM ": %s/%s: ERROR - Can't allocate skb! (%s@%d)\n",
					IOC_AND_NETDEV_NAMES_s_s(dev),
					__FILE__, __LINE__);
			return -ENOMEM;
		}

		pci_dma_sync_single_for_cpu(mpt_dev->pcidev,
					    priv->RcvCtl[ctx].dma,
					    priv->RcvCtl[ctx].len,
					    PCI_DMA_FROMDEVICE);

		memcpy(skb_put(skb, len), old_skb->data, len);

		pci_dma_sync_single_for_device(mpt_dev->pcidev,
					       priv->RcvCtl[ctx].dma,
					       priv->RcvCtl[ctx].len,
					       PCI_DMA_FROMDEVICE);

		spin_lock_irqsave(&priv->rxfidx_lock, flags);
		priv->mpt_rxfidx[++priv->mpt_rxfidx_tail] = ctx;
		spin_unlock_irqrestore(&priv->rxfidx_lock, flags);

	} else {
		spin_lock_irqsave(&priv->rxfidx_lock, flags);

		priv->RcvCtl[ctx].skb = NULL;

		pci_unmap_single(mpt_dev->pcidev, priv->RcvCtl[ctx].dma,
				 priv->RcvCtl[ctx].len, PCI_DMA_FROMDEVICE);
		priv->RcvCtl[ctx].dma = 0;

		priv->mpt_rxfidx[++priv->mpt_rxfidx_tail] = ctx;
		spin_unlock_irqrestore(&priv->rxfidx_lock, flags);

		skb_put(skb,len);
	}

	atomic_sub(count, &priv->buckets_out);
	priv->total_received += count;

	if (priv->mpt_rxfidx_tail >= MPT_LAN_MAX_BUCKETS_OUT) {
		printk (KERN_ERR MYNAM ": %s/%s: Yoohoo! mpt_rxfidx_tail = %d, "
			"MPT_LAN_MAX_BUCKETS_OUT = %d\n",
				IOC_AND_NETDEV_NAMES_s_s(dev),
				priv->mpt_rxfidx_tail,
				MPT_LAN_MAX_BUCKETS_OUT);

		return -1;
	}

	if (remaining == 0)
		printk (KERN_WARNING MYNAM ": %s/%s: WARNING - IOC out of buckets! "
			"(priv->buckets_out = %d)\n",
			IOC_AND_NETDEV_NAMES_s_s(dev),
			atomic_read(&priv->buckets_out));
	else if (remaining < 10)
		printk (KERN_INFO MYNAM ": %s/%s: IOC says %d buckets left. "
			"(priv->buckets_out = %d)\n",
			IOC_AND_NETDEV_NAMES_s_s(dev),
			remaining, atomic_read(&priv->buckets_out));
	
	if ((remaining < priv->bucketthresh) &&
	    ((atomic_read(&priv->buckets_out) - remaining) >
	     MPT_LAN_BUCKETS_REMAIN_MISMATCH_THRESH)) {
		
		printk (KERN_WARNING MYNAM " Mismatch between driver's "
			"buckets_out count and fw's BucketsRemaining "
			"count has crossed the threshold, issuing a "
			"LanReset to clear the fw's hashtable. You may "
			"want to check your /var/log/messages for \"CRC "
			"error\" event notifications.\n");
		
		mpt_lan_reset(dev);
		mpt_lan_wake_post_buckets_task(dev, 0);
	}
	
	return mpt_lan_receive_skb(dev, skb);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* Simple SGE's only at the moment */

static void
mpt_lan_post_receive_buckets(struct mpt_lan_priv *priv)
{
	struct net_device *dev = priv->dev;
	MPT_ADAPTER *mpt_dev = priv->mpt_dev;
	MPT_FRAME_HDR *mf;
	LANReceivePostRequest_t *pRecvReq;
	SGETransaction32_t *pTrans;
	SGESimple64_t *pSimple;
	struct sk_buff *skb;
	dma_addr_t dma;
	u32 curr, buckets, count, max;
	u32 len = (dev->mtu + dev->hard_header_len + 4);
	unsigned long flags;
	int i;

	curr = atomic_read(&priv->buckets_out);
	buckets = (priv->max_buckets_out - curr);

	dioprintk((KERN_INFO MYNAM ": %s/%s: @%s, Start_buckets = %u, buckets_out = %u\n",
			IOC_AND_NETDEV_NAMES_s_s(dev),
			__FUNCTION__, buckets, curr));

	max = (mpt_dev->req_sz - MPT_LAN_RECEIVE_POST_REQUEST_SIZE) /
			(MPT_LAN_TRANSACTION32_SIZE + sizeof(SGESimple64_t));

	while (buckets) {
		mf = mpt_get_msg_frame(LanCtx, mpt_dev);
		if (mf == NULL) {
			printk (KERN_ERR "%s: Unable to alloc request frame\n",
				__FUNCTION__);
			dioprintk((KERN_ERR "%s: %u buckets remaining\n",
				 __FUNCTION__, buckets));
			goto out;
		}
		pRecvReq = (LANReceivePostRequest_t *) mf;

		count = buckets;
		if (count > max)
			count = max;

		pRecvReq->Function    = MPI_FUNCTION_LAN_RECEIVE;
		pRecvReq->ChainOffset = 0;
		pRecvReq->MsgFlags    = 0;
		pRecvReq->PortNumber  = priv->pnum;

		pTrans = (SGETransaction32_t *) pRecvReq->SG_List;
		pSimple = NULL;

		for (i = 0; i < count; i++) {
			int ctx;

			spin_lock_irqsave(&priv->rxfidx_lock, flags);
			if (priv->mpt_rxfidx_tail < 0) {
				printk (KERN_ERR "%s: Can't alloc context\n",
					__FUNCTION__);
				spin_unlock_irqrestore(&priv->rxfidx_lock,
						       flags);
				break;
			}

			ctx = priv->mpt_rxfidx[priv->mpt_rxfidx_tail--];

			skb = priv->RcvCtl[ctx].skb;
			if (skb && (priv->RcvCtl[ctx].len != len)) {
				pci_unmap_single(mpt_dev->pcidev,
						 priv->RcvCtl[ctx].dma,
						 priv->RcvCtl[ctx].len,
						 PCI_DMA_FROMDEVICE);
				dev_kfree_skb(priv->RcvCtl[ctx].skb);
				skb = priv->RcvCtl[ctx].skb = NULL;
			}

			if (skb == NULL) {
				skb = dev_alloc_skb(len);
				if (skb == NULL) {
					printk (KERN_WARNING
						MYNAM "/%s: Can't alloc skb\n",
						__FUNCTION__);
					priv->mpt_rxfidx[++priv->mpt_rxfidx_tail] = ctx;
					spin_unlock_irqrestore(&priv->rxfidx_lock, flags);
					break;
				}

				dma = pci_map_single(mpt_dev->pcidev, skb->data,
						     len, PCI_DMA_FROMDEVICE);

				priv->RcvCtl[ctx].skb = skb;
				priv->RcvCtl[ctx].dma = dma;
				priv->RcvCtl[ctx].len = len;
			}

			spin_unlock_irqrestore(&priv->rxfidx_lock, flags);

			pTrans->ContextSize   = sizeof(u32);
			pTrans->DetailsLength = 0;
			pTrans->Flags         = 0;
			pTrans->TransactionContext[0] = cpu_to_le32(ctx);

			pSimple = (SGESimple64_t *) pTrans->TransactionDetails;

			pSimple->FlagsLength = cpu_to_le32(
				((MPI_SGE_FLAGS_END_OF_BUFFER |
				  MPI_SGE_FLAGS_SIMPLE_ELEMENT |
				  MPI_SGE_FLAGS_64_BIT_ADDRESSING) << MPI_SGE_FLAGS_SHIFT) | len);
			pSimple->Address.Low = cpu_to_le32((u32) priv->RcvCtl[ctx].dma);
			if (sizeof(dma_addr_t) > sizeof(u32))
				pSimple->Address.High = cpu_to_le32((u32) ((u64) priv->RcvCtl[ctx].dma >> 32));
			else
				pSimple->Address.High = 0;

			pTrans = (SGETransaction32_t *) (pSimple + 1);
		}

		if (pSimple == NULL) {
/**/			printk (KERN_WARNING MYNAM "/%s: No buckets posted\n",
/**/				__FUNCTION__);
			mpt_free_msg_frame(mpt_dev, mf);
			goto out;
		}

		pSimple->FlagsLength |= cpu_to_le32(MPI_SGE_FLAGS_END_OF_LIST << MPI_SGE_FLAGS_SHIFT);

		pRecvReq->BucketCount = cpu_to_le32(i);

/*	printk(KERN_INFO MYNAM ": posting buckets\n   ");
 *	for (i = 0; i < j + 2; i ++)
 *	    printk (" %08x", le32_to_cpu(msg[i]));
 *	printk ("\n");
 */

		mpt_put_msg_frame(LanCtx, mpt_dev, mf);

		priv->total_posted += i;
		buckets -= i;
		atomic_add(i, &priv->buckets_out);
	}

out:
	dioprintk((KERN_INFO MYNAM "/%s: End_buckets = %u, priv->buckets_out = %u\n",
		  __FUNCTION__, buckets, atomic_read(&priv->buckets_out)));
	dioprintk((KERN_INFO MYNAM "/%s: Posted %u buckets and received %u back\n",
	__FUNCTION__, priv->total_posted, priv->total_received));

	clear_bit(0, &priv->post_buckets_active);
}

static void
mpt_lan_post_receive_buckets_work(struct work_struct *work)
{
	mpt_lan_post_receive_buckets(container_of(work, struct mpt_lan_priv,
						  post_buckets_task.work));
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static struct net_device *
mpt_register_lan_device (MPT_ADAPTER *mpt_dev, int pnum)
{
	struct net_device *dev = alloc_fcdev(sizeof(struct mpt_lan_priv));
	struct mpt_lan_priv *priv = NULL;
	u8 HWaddr[FC_ALEN], *a;

	if (!dev)
		return NULL;

	dev->mtu = MPT_LAN_MTU;

	priv = netdev_priv(dev);

	priv->dev = dev;
	priv->mpt_dev = mpt_dev;
	priv->pnum = pnum;

	memset(&priv->post_buckets_task, 0, sizeof(priv->post_buckets_task));
	INIT_DELAYED_WORK(&priv->post_buckets_task,
			  mpt_lan_post_receive_buckets_work);
	priv->post_buckets_active = 0;

	dlprintk((KERN_INFO MYNAM "@%d: bucketlen = %d\n",
			__LINE__, dev->mtu + dev->hard_header_len + 4));

	atomic_set(&priv->buckets_out, 0);
	priv->total_posted = 0;
	priv->total_received = 0;
	priv->max_buckets_out = max_buckets_out;
	if (mpt_dev->pfacts[0].MaxLanBuckets < max_buckets_out)
		priv->max_buckets_out = mpt_dev->pfacts[0].MaxLanBuckets;

	dlprintk((KERN_INFO MYNAM "@%d: MaxLanBuckets=%d, max_buckets_out/priv=%d/%d\n",
			__LINE__,
			mpt_dev->pfacts[0].MaxLanBuckets,
			max_buckets_out,
			priv->max_buckets_out));

	priv->bucketthresh = priv->max_buckets_out * 2 / 3;
	spin_lock_init(&priv->txfidx_lock);
	spin_lock_init(&priv->rxfidx_lock);

	memset(&priv->stats, 0, sizeof(priv->stats));

	/*  Grab pre-fetched LANPage1 stuff. :-) */
	a = (u8 *) &mpt_dev->lan_cnfg_page1.HardwareAddressLow;

	HWaddr[0] = a[5];
	HWaddr[1] = a[4];
	HWaddr[2] = a[3];
	HWaddr[3] = a[2];
	HWaddr[4] = a[1];
	HWaddr[5] = a[0];

	dev->addr_len = FC_ALEN;
	memcpy(dev->dev_addr, HWaddr, FC_ALEN);
	memset(dev->broadcast, 0xff, FC_ALEN);

	/* The Tx queue is 127 deep on the 909.
	 * Give ourselves some breathing room.
	 */
	priv->tx_max_out = (tx_max_out_p <= MPT_TX_MAX_OUT_LIM) ?
			    tx_max_out_p : MPT_TX_MAX_OUT_LIM;

	dev->open = mpt_lan_open;
	dev->stop = mpt_lan_close;
	dev->get_stats = mpt_lan_get_stats;
	dev->set_multicast_list = NULL;
	dev->change_mtu = mpt_lan_change_mtu;
	dev->hard_start_xmit = mpt_lan_sdu_send;

/* Not in 2.3.42. Need 2.3.45+ */
	dev->tx_timeout = mpt_lan_tx_timeout;
	dev->watchdog_timeo = MPT_LAN_TX_TIMEOUT;

	dlprintk((KERN_INFO MYNAM ": Finished registering dev "
		"and setting initial values\n"));

	SET_MODULE_OWNER(dev);

	if (register_netdev(dev) != 0) {
		free_netdev(dev);
		dev = NULL;
	}
	return dev;
}

static int
mptlan_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	MPT_ADAPTER 		*ioc = pci_get_drvdata(pdev);
	struct net_device	*dev;
	int			i;

	for (i = 0; i < ioc->facts.NumberOfPorts; i++) {
		printk(KERN_INFO MYNAM ": %s: PortNum=%x, "
		       "ProtocolFlags=%02Xh (%c%c%c%c)\n",
		       ioc->name, ioc->pfacts[i].PortNumber,
		       ioc->pfacts[i].ProtocolFlags,
		       MPT_PROTOCOL_FLAGS_c_c_c_c(
			       ioc->pfacts[i].ProtocolFlags));

		if (!(ioc->pfacts[i].ProtocolFlags &
					MPI_PORTFACTS_PROTOCOL_LAN)) {
			printk(KERN_INFO MYNAM ": %s: Hmmm... LAN protocol "
			       "seems to be disabled on this adapter port!\n",
			       ioc->name);
			continue;
		}

		dev = mpt_register_lan_device(ioc, i);
		if (!dev) {
			printk(KERN_ERR MYNAM ": %s: Unable to register "
			       "port%d as a LAN device\n", ioc->name,
			       ioc->pfacts[i].PortNumber);
			continue;
		}
		
		printk(KERN_INFO MYNAM ": %s: Fusion MPT LAN device "
		       "registered as '%s'\n", ioc->name, dev->name);
		printk(KERN_INFO MYNAM ": %s/%s: "
		       "LanAddr = %02X:%02X:%02X:%02X:%02X:%02X\n",
		       IOC_AND_NETDEV_NAMES_s_s(dev),
		       dev->dev_addr[0], dev->dev_addr[1],
		       dev->dev_addr[2], dev->dev_addr[3],
		       dev->dev_addr[4], dev->dev_addr[5]);
	
		ioc->netdev = dev;

		return 0;
	}

	return -ENODEV;
}

static void
mptlan_remove(struct pci_dev *pdev)
{
	MPT_ADAPTER 		*ioc = pci_get_drvdata(pdev);
	struct net_device	*dev = ioc->netdev;

	if(dev != NULL) {
		unregister_netdev(dev);
		free_netdev(dev);
	}
}

static struct mpt_pci_driver mptlan_driver = {
	.probe		= mptlan_probe,
	.remove		= mptlan_remove,
};

static int __init mpt_lan_init (void)
{
	show_mptmod_ver(LANAME, LANVER);

	if ((LanCtx = mpt_register(lan_reply, MPTLAN_DRIVER)) <= 0) {
		printk (KERN_ERR MYNAM ": Failed to register with MPT base driver\n");
		return -EBUSY;
	}

	/* Set the callback index to be used by driver core for turbo replies */
	mpt_lan_index = LanCtx;

	dlprintk((KERN_INFO MYNAM ": assigned context of %d\n", LanCtx));

	if (mpt_reset_register(LanCtx, mpt_lan_ioc_reset)) {
		printk(KERN_ERR MYNAM ": Eieee! unable to register a reset "
		       "handler with mptbase! The world is at an end! "
		       "Everything is fading to black! Goodbye.\n");
		return -EBUSY;
	}

	dlprintk((KERN_INFO MYNAM ": Registered for IOC reset notifications\n"));
	
	if (mpt_device_driver_register(&mptlan_driver, MPTLAN_DRIVER))
		dprintk((KERN_INFO MYNAM ": failed to register dd callbacks\n"));
	return 0;
}

static void __exit mpt_lan_exit(void)
{
	mpt_device_driver_deregister(MPTLAN_DRIVER);
	mpt_reset_deregister(LanCtx);

	if (LanCtx >= 0) {
		mpt_deregister(LanCtx);
		LanCtx = -1;
		mpt_lan_index = 0;
	}
}

module_init(mpt_lan_init);
module_exit(mpt_lan_exit);

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static unsigned short
mpt_lan_type_trans(struct sk_buff *skb, struct net_device *dev)
{
	struct mpt_lan_ohdr *fch = (struct mpt_lan_ohdr *)skb->data;
	struct fcllc *fcllc;

	skb->mac.raw = skb->data;
	skb_pull(skb, sizeof(struct mpt_lan_ohdr));

	if (fch->dtype == htons(0xffff)) {
		u32 *p = (u32 *) fch;

		swab32s(p + 0);
		swab32s(p + 1);
		swab32s(p + 2);
		swab32s(p + 3);

		printk (KERN_WARNING MYNAM ": %s: WARNING - Broadcast swap F/W bug detected!\n",
				NETDEV_PTR_TO_IOC_NAME_s(dev));
		printk (KERN_WARNING MYNAM ": Please update sender @ MAC_addr = %02x:%02x:%02x:%02x:%02x:%02x\n",
				fch->saddr[0], fch->saddr[1], fch->saddr[2],
				fch->saddr[3], fch->saddr[4], fch->saddr[5]);
	}

	if (*fch->daddr & 1) {
		if (!memcmp(fch->daddr, dev->broadcast, FC_ALEN)) {
			skb->pkt_type = PACKET_BROADCAST;
		} else {
			skb->pkt_type = PACKET_MULTICAST;
		}
	} else {
		if (memcmp(fch->daddr, dev->dev_addr, FC_ALEN)) {
			skb->pkt_type = PACKET_OTHERHOST;
		} else {
			skb->pkt_type = PACKET_HOST;
		}
	}

	fcllc = (struct fcllc *)skb->data;

#ifdef QLOGIC_NAA_WORKAROUND
{
	u16 source_naa = fch->stype, found = 0;

	/* Workaround for QLogic not following RFC 2625 in regards to the NAA
	   value. */

	if ((source_naa & 0xF000) == 0)
		source_naa = swab16(source_naa);

	if (fcllc->ethertype == htons(ETH_P_ARP))
	    dlprintk ((KERN_INFO "mptlan/type_trans: got arp req/rep w/ naa of "
		      "%04x.\n", source_naa));

	if ((fcllc->ethertype == htons(ETH_P_ARP)) &&
	   ((source_naa >> 12) !=  MPT_LAN_NAA_RFC2625)){
		struct NAA_Hosed *nh, *prevnh;
		int i;

		dlprintk ((KERN_INFO "mptlan/type_trans: ARP Req/Rep from "
			  "system with non-RFC 2625 NAA value (%04x).\n",
			  source_naa));

		write_lock_irq(&bad_naa_lock);
		for (prevnh = nh = mpt_bad_naa; nh != NULL;
		     prevnh=nh, nh=nh->next) {
			if ((nh->ieee[0] == fch->saddr[0]) &&
			    (nh->ieee[1] == fch->saddr[1]) &&
			    (nh->ieee[2] == fch->saddr[2]) &&
			    (nh->ieee[3] == fch->saddr[3]) &&
			    (nh->ieee[4] == fch->saddr[4]) &&
			    (nh->ieee[5] == fch->saddr[5])) {
				found = 1;
				dlprintk ((KERN_INFO "mptlan/type_trans: ARP Re"
					 "q/Rep w/ bad NAA from system already"
					 " in DB.\n"));
				break;
			}
		}

		if ((!found) && (nh == NULL)) {

			nh = kmalloc(sizeof(struct NAA_Hosed), GFP_KERNEL);
			dlprintk ((KERN_INFO "mptlan/type_trans: ARP Req/Rep w/"
				 " bad NAA from system not yet in DB.\n"));

			if (nh != NULL) {
				nh->next = NULL;
				if (!mpt_bad_naa)
					mpt_bad_naa = nh;
				if (prevnh)
					prevnh->next = nh;

				nh->NAA = source_naa; /* Set the S_NAA value. */
				for (i = 0; i < FC_ALEN; i++)
					nh->ieee[i] = fch->saddr[i];
				dlprintk ((KERN_INFO "Got ARP from %02x:%02x:%02x:%02x:"
					  "%02x:%02x with non-compliant S_NAA value.\n",
					  fch->saddr[0], fch->saddr[1], fch->saddr[2],
					  fch->saddr[3], fch->saddr[4],fch->saddr[5]));
			} else {
				printk (KERN_ERR "mptlan/type_trans: Unable to"
					" kmalloc a NAA_Hosed struct.\n");
			}
		} else if (!found) {
			printk (KERN_ERR "mptlan/type_trans: found not"
				" set, but nh isn't null. Evil "
				"funkiness abounds.\n");
		}
		write_unlock_irq(&bad_naa_lock);
	}
}
#endif

	/* Strip the SNAP header from ARP packets since we don't
	 * pass them through to the 802.2/SNAP layers.
	 */
	if (fcllc->dsap == EXTENDED_SAP &&
		(fcllc->ethertype == htons(ETH_P_IP) ||
		 fcllc->ethertype == htons(ETH_P_ARP))) {
		skb_pull(skb, sizeof(struct fcllc));
		return fcllc->ethertype;
	}

	return htons(ETH_P_802_2);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
