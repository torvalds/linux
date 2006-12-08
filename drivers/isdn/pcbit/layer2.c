/*
 * PCBIT-D low-layer interface
 *
 * Copyright (C) 1996 Universidade de Lisboa
 *
 * Written by Pedro Roque Marques (roque@di.fc.ul.pt)
 *
 * This software may be used and distributed according to the terms of
 * the GNU General Public License, incorporated herein by reference.
 */

/*
 * 19991203 - Fernando Carvalho - takion@superbofh.org
 * Hacked to compile with egcs and run with current version of isdn modules
*/

/*
 *        Based on documentation provided by Inesc:
 *        - "Interface com bus do PC para o PCBIT e PCBIT-D", Inesc, Jan 93
 */

/*
 *        TODO: better handling of errors
 *              re-write/remove debug printks
 */

#include <linux/sched.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/mm.h>
#include <linux/skbuff.h>

#include <linux/isdnif.h>

#include <asm/system.h>
#include <asm/io.h>


#include "pcbit.h"
#include "layer2.h"
#include "edss1.h"

#undef DEBUG_FRAG



/*
 *  task queue struct
 */



/*
 *  Layer 3 packet demultiplexer
 *  drv.c
 */

extern void pcbit_l3_receive(struct pcbit_dev *dev, ulong msg,
			     struct sk_buff *skb,
			     ushort hdr_len, ushort refnum);

/*
 *  Prototypes
 */

static void pcbit_transmit(struct pcbit_dev *dev);

static void pcbit_recv_ack(struct pcbit_dev *dev, unsigned char ack);

static void pcbit_l2_error(struct pcbit_dev *dev);
static void pcbit_l2_active_conf(struct pcbit_dev *dev, u_char info);
static void pcbit_l2_err_recover(unsigned long data);

static void pcbit_firmware_bug(struct pcbit_dev *dev);

static __inline__ void
pcbit_sched_delivery(struct pcbit_dev *dev)
{
	schedule_work(&dev->qdelivery);
}


/*
 *  Called from layer3
 */

int
pcbit_l2_write(struct pcbit_dev *dev, ulong msg, ushort refnum,
	       struct sk_buff *skb, unsigned short hdr_len)
{
	struct frame_buf *frame,
	*ptr;
	unsigned long flags;

	if (dev->l2_state != L2_RUNNING && dev->l2_state != L2_LOADING) {
		dev_kfree_skb(skb);
		return -1;
	}
	if ((frame = (struct frame_buf *) kmalloc(sizeof(struct frame_buf),
						  GFP_ATOMIC)) == NULL) {
		printk(KERN_WARNING "pcbit_2_write: kmalloc failed\n");
		dev_kfree_skb(skb);
		return -1;
	}
	frame->msg = msg;
	frame->refnum = refnum;
	frame->copied = 0;
	frame->hdr_len = hdr_len;

	if (skb)
		frame->dt_len = skb->len - hdr_len;
	else
		frame->dt_len = 0;

	frame->skb = skb;

	frame->next = NULL;

	spin_lock_irqsave(&dev->lock, flags);

	if (dev->write_queue == NULL) {
		dev->write_queue = frame;
		spin_unlock_irqrestore(&dev->lock, flags);
		pcbit_transmit(dev);
	} else {
		for (ptr = dev->write_queue; ptr->next; ptr = ptr->next);
		ptr->next = frame;

		spin_unlock_irqrestore(&dev->lock, flags);
	}
	return 0;
}

static __inline__ void
pcbit_tx_update(struct pcbit_dev *dev, ushort len)
{
	u_char info;

	dev->send_seq = (dev->send_seq + 1) % 8;

	dev->fsize[dev->send_seq] = len;
	info = 0;
	info |= dev->rcv_seq << 3;
	info |= dev->send_seq;

	writeb(info, dev->sh_mem + BANK4);

}

/*
 * called by interrupt service routine or by write_2
 */

static void
pcbit_transmit(struct pcbit_dev *dev)
{
	struct frame_buf *frame = NULL;
	unsigned char unacked;
	int flen;               /* fragment frame length including all headers */
	int free;
	int count,
	 cp_len;
	unsigned long flags;
	unsigned short tt;

	if (dev->l2_state != L2_RUNNING && dev->l2_state != L2_LOADING)
		return;

	unacked = (dev->send_seq + (8 - dev->unack_seq)) & 0x07;

	spin_lock_irqsave(&dev->lock, flags);

	if (dev->free > 16 && dev->write_queue && unacked < 7) {

		if (!dev->w_busy)
			dev->w_busy = 1;
		else {
			spin_unlock_irqrestore(&dev->lock, flags);
			return;
		}


		frame = dev->write_queue;
		free = dev->free;

		spin_unlock_irqrestore(&dev->lock, flags);

		if (frame->copied == 0) {

			/* Type 0 frame */

			ulong 	msg;

			if (frame->skb)
				flen = FRAME_HDR_LEN + PREHDR_LEN + frame->skb->len;
			else
				flen = FRAME_HDR_LEN + PREHDR_LEN;

			if (flen > free)
				flen = free;

			msg = frame->msg;

			/*
			 *  Board level 2 header
			 */

			pcbit_writew(dev, flen - FRAME_HDR_LEN);

			pcbit_writeb(dev, GET_MSG_CPU(msg));

			pcbit_writeb(dev, GET_MSG_PROC(msg));

			/* TH */
			pcbit_writew(dev, frame->hdr_len + PREHDR_LEN);

			/* TD */
			pcbit_writew(dev, frame->dt_len);


			/*
			 *  Board level 3 fixed-header
			 */

			/* LEN = TH */
			pcbit_writew(dev, frame->hdr_len + PREHDR_LEN);

			/* XX */
			pcbit_writew(dev, 0);

			/* C + S */
			pcbit_writeb(dev, GET_MSG_CMD(msg));
			pcbit_writeb(dev, GET_MSG_SCMD(msg));

			/* NUM */
			pcbit_writew(dev, frame->refnum);

			count = FRAME_HDR_LEN + PREHDR_LEN;
		} else {
			/* Type 1 frame */

			flen = 2 + (frame->skb->len - frame->copied);

			if (flen > free)
				flen = free;

			/* TT */
			tt = ((ushort) (flen - 2)) | 0x8000U;	/* Type 1 */
			pcbit_writew(dev, tt);

			count = 2;
		}

		if (frame->skb) {
			cp_len = frame->skb->len - frame->copied;
			if (cp_len > flen - count)
				cp_len = flen - count;

			memcpy_topcbit(dev, frame->skb->data + frame->copied,
				       cp_len);
			frame->copied += cp_len;
		}
		/* bookkeeping */
		dev->free -= flen;
		pcbit_tx_update(dev, flen);

		spin_lock_irqsave(&dev->lock, flags);

		if (frame->skb == NULL || frame->copied == frame->skb->len) {

			dev->write_queue = frame->next;

			if (frame->skb != NULL) {
				/* free frame */
				dev_kfree_skb(frame->skb);
			}
			kfree(frame);
		}
		dev->w_busy = 0;
		spin_unlock_irqrestore(&dev->lock, flags);
	} else {
		spin_unlock_irqrestore(&dev->lock, flags);
#ifdef DEBUG
		printk(KERN_DEBUG "unacked %d free %d write_queue %s\n",
		     unacked, dev->free, dev->write_queue ? "not empty" :
		       "empty");
#endif
	}
}


/*
 *  deliver a queued frame to the upper layer
 */

void
pcbit_deliver(struct work_struct *work)
{
	struct frame_buf *frame;
	unsigned long flags, msg;
	struct pcbit_dev *dev =
		container_of(work, struct pcbit_dev, qdelivery);

	spin_lock_irqsave(&dev->lock, flags);

	while ((frame = dev->read_queue)) {
		dev->read_queue = frame->next;
		spin_unlock_irqrestore(&dev->lock, flags);

		msg = 0;
		SET_MSG_CPU(msg, 0);
		SET_MSG_PROC(msg, 0);
		SET_MSG_CMD(msg, frame->skb->data[2]);
		SET_MSG_SCMD(msg, frame->skb->data[3]);

		frame->refnum = *((ushort *) frame->skb->data + 4);
		frame->msg = *((ulong *) & msg);

		skb_pull(frame->skb, 6);

		pcbit_l3_receive(dev, frame->msg, frame->skb, frame->hdr_len,
				 frame->refnum);

		kfree(frame);

		spin_lock_irqsave(&dev->lock, flags);
	}

	spin_unlock_irqrestore(&dev->lock, flags);
}

/*
 * Reads BANK 2 & Reassembles
 */

static void
pcbit_receive(struct pcbit_dev *dev)
{
	unsigned short tt;
	u_char cpu,
	 proc;
	struct frame_buf *frame = NULL;
	unsigned long flags;
	u_char type1;

	if (dev->l2_state != L2_RUNNING && dev->l2_state != L2_LOADING)
		return;

	tt = pcbit_readw(dev);

	if ((tt & 0x7fffU) > 511) {
		printk(KERN_INFO "pcbit: invalid frame length -> TT=%04x\n",
		       tt);
		pcbit_l2_error(dev);
		return;
	}
	if (!(tt & 0x8000U)) {  /* Type 0 */
		type1 = 0;

		if (dev->read_frame) {
			printk(KERN_DEBUG "pcbit_receive: Type 0 frame and read_frame != NULL\n");
			/* discard previous queued frame */
			if (dev->read_frame->skb)
				kfree_skb(dev->read_frame->skb);
			kfree(dev->read_frame);
			dev->read_frame = NULL;
		}
		frame = kmalloc(sizeof(struct frame_buf), GFP_ATOMIC);

		if (frame == NULL) {
			printk(KERN_WARNING "kmalloc failed\n");
			return;
		}
		memset(frame, 0, sizeof(struct frame_buf));

		cpu = pcbit_readb(dev);
		proc = pcbit_readb(dev);


		if (cpu != 0x06 && cpu != 0x02) {
			printk(KERN_DEBUG "pcbit: invalid cpu value\n");
			kfree(frame);
			pcbit_l2_error(dev);
			return;
		}
		/*
		 * we discard cpu & proc on receiving
		 * but we read it to update the pointer
		 */

		frame->hdr_len = pcbit_readw(dev);
		frame->dt_len = pcbit_readw(dev);

		/*
		   * 0 sized packet
		   * I don't know if they are an error or not...
		   * But they are very frequent
		   * Not documented
		 */

		if (frame->hdr_len == 0) {
			kfree(frame);
#ifdef DEBUG
			printk(KERN_DEBUG "0 sized frame\n");
#endif
			pcbit_firmware_bug(dev);
			return;
		}
		/* sanity check the length values */
		if (frame->hdr_len > 1024 || frame->dt_len > 2048) {
#ifdef DEBUG
			printk(KERN_DEBUG "length problem: ");
			printk(KERN_DEBUG "TH=%04x TD=%04x\n",
			       frame->hdr_len,
			       frame->dt_len);
#endif
			pcbit_l2_error(dev);
			kfree(frame);
			return;
		}
		/* minimum frame read */

		frame->skb = dev_alloc_skb(frame->hdr_len + frame->dt_len +
					   ((frame->hdr_len + 15) & ~15));

		if (!frame->skb) {
			printk(KERN_DEBUG "pcbit_receive: out of memory\n");
			kfree(frame);
			return;
		}
		/* 16 byte alignment for IP */
		if (frame->dt_len)
			skb_reserve(frame->skb, (frame->hdr_len + 15) & ~15);

	} else {
		/* Type 1 */
		type1 = 1;
		tt &= 0x7fffU;

		if (!(frame = dev->read_frame)) {
			printk("Type 1 frame and no frame queued\n");
			/* usually after an error: toss frame */
			dev->readptr += tt;
			if (dev->readptr > dev->sh_mem + BANK2 + BANKLEN)
				dev->readptr -= BANKLEN;
			return;

		}
	}

	memcpy_frompcbit(dev, skb_put(frame->skb, tt), tt);

	frame->copied += tt;
	spin_lock_irqsave(&dev->lock, flags);
	if (frame->copied == frame->hdr_len + frame->dt_len) {

		if (type1) {
			dev->read_frame = NULL;
		}
		if (dev->read_queue) {
			struct frame_buf *ptr;
			for (ptr = dev->read_queue; ptr->next; ptr = ptr->next);
			ptr->next = frame;
		} else
			dev->read_queue = frame;

	} else {
		dev->read_frame = frame;
	}
	spin_unlock_irqrestore(&dev->lock, flags);
}

/*
 *  The board sends 0 sized frames
 *  They are TDATA_CONFs that get messed up somehow
 *  gotta send a fake acknowledgment to the upper layer somehow
 */

static __inline__ void
pcbit_fake_conf(struct pcbit_dev *dev, struct pcbit_chan *chan)
{
	isdn_ctrl ictl;

	if (chan->queued) {
		chan->queued--;

		ictl.driver = dev->id;
		ictl.command = ISDN_STAT_BSENT;
		ictl.arg = chan->id;
		dev->dev_if->statcallb(&ictl);
	}
}

static void
pcbit_firmware_bug(struct pcbit_dev *dev)
{
	struct pcbit_chan *chan;

	chan = dev->b1;

	if (chan->fsm_state == ST_ACTIVE) {
		pcbit_fake_conf(dev, chan);
	}
	chan = dev->b2;

	if (chan->fsm_state == ST_ACTIVE) {
		pcbit_fake_conf(dev, chan);
	}
}

irqreturn_t
pcbit_irq_handler(int interrupt, void *devptr)
{
	struct pcbit_dev *dev;
	u_char info,
	 ack_seq,
	 read_seq;

	dev = (struct pcbit_dev *) devptr;

	if (!dev) {
		printk(KERN_WARNING "pcbit_irq_handler: wrong device\n");
		return IRQ_NONE;
	}
	if (dev->interrupt) {
		printk(KERN_DEBUG "pcbit: reentering interrupt hander\n");
		return IRQ_HANDLED;
	}
	dev->interrupt = 1;

	info = readb(dev->sh_mem + BANK3);

	if (dev->l2_state == L2_STARTING || dev->l2_state == L2_ERROR) {
		pcbit_l2_active_conf(dev, info);
		dev->interrupt = 0;
		return IRQ_HANDLED;
	}
	if (info & 0x40U) {     /* E bit set */
#ifdef DEBUG
		printk(KERN_DEBUG "pcbit_irq_handler: E bit on\n");
#endif
		pcbit_l2_error(dev);
		dev->interrupt = 0;
		return IRQ_HANDLED;
	}
	if (dev->l2_state != L2_RUNNING && dev->l2_state != L2_LOADING) {
		dev->interrupt = 0;
		return IRQ_HANDLED;
	}
	ack_seq = (info >> 3) & 0x07U;
	read_seq = (info & 0x07U);

	dev->interrupt = 0;

	if (read_seq != dev->rcv_seq) {
		while (read_seq != dev->rcv_seq) {
			pcbit_receive(dev);
			dev->rcv_seq = (dev->rcv_seq + 1) % 8;
		}
		pcbit_sched_delivery(dev);
	}
	if (ack_seq != dev->unack_seq) {
		pcbit_recv_ack(dev, ack_seq);
	}
	info = dev->rcv_seq << 3;
	info |= dev->send_seq;

	writeb(info, dev->sh_mem + BANK4);
	return IRQ_HANDLED;
}


static void
pcbit_l2_active_conf(struct pcbit_dev *dev, u_char info)
{
	u_char state;

	state = dev->l2_state;

#ifdef DEBUG
	printk(KERN_DEBUG "layer2_active_confirm\n");
#endif


	if (info & 0x80U) {
		dev->rcv_seq = info & 0x07U;
		dev->l2_state = L2_RUNNING;
	} else
		dev->l2_state = L2_DOWN;

	if (state == L2_STARTING)
		wake_up_interruptible(&dev->set_running_wq);

	if (state == L2_ERROR && dev->l2_state == L2_RUNNING) {
		pcbit_transmit(dev);
	}
}

static void
pcbit_l2_err_recover(unsigned long data)
{

	struct pcbit_dev *dev;
	struct frame_buf *frame;

	dev = (struct pcbit_dev *) data;

	del_timer(&dev->error_recover_timer);
	if (dev->w_busy || dev->r_busy) {
		init_timer(&dev->error_recover_timer);
		dev->error_recover_timer.expires = jiffies + ERRTIME;
		add_timer(&dev->error_recover_timer);
		return;
	}
	dev->w_busy = dev->r_busy = 1;

	if (dev->read_frame) {
		if (dev->read_frame->skb)
			kfree_skb(dev->read_frame->skb);
		kfree(dev->read_frame);
		dev->read_frame = NULL;
	}
	if (dev->write_queue) {
		frame = dev->write_queue;
#ifdef FREE_ON_ERROR
		dev->write_queue = dev->write_queue->next;

		if (frame->skb) {
			dev_kfree_skb(frame->skb);
		}
		kfree(frame);
#else
		frame->copied = 0;
#endif
	}
	dev->rcv_seq = dev->send_seq = dev->unack_seq = 0;
	dev->free = 511;
	dev->l2_state = L2_ERROR;

	/* this is an hack... */
	pcbit_firmware_bug(dev);

	dev->writeptr = dev->sh_mem;
	dev->readptr = dev->sh_mem + BANK2;

	writeb((0x80U | ((dev->rcv_seq & 0x07) << 3) | (dev->send_seq & 0x07)),
	       dev->sh_mem + BANK4);
	dev->w_busy = dev->r_busy = 0;

}

static void
pcbit_l2_error(struct pcbit_dev *dev)
{
	if (dev->l2_state == L2_RUNNING) {

		printk(KERN_INFO "pcbit: layer 2 error\n");

#ifdef DEBUG
		log_state(dev);
#endif

		dev->l2_state = L2_DOWN;

		init_timer(&dev->error_recover_timer);
		dev->error_recover_timer.function = &pcbit_l2_err_recover;
		dev->error_recover_timer.data = (ulong) dev;
		dev->error_recover_timer.expires = jiffies + ERRTIME;
		add_timer(&dev->error_recover_timer);
	}
}

/*
 * Description:
 * if board acks frames
 *   update dev->free
 *   call pcbit_transmit to write possible queued frames
 */

static void
pcbit_recv_ack(struct pcbit_dev *dev, unsigned char ack)
{
	int i,
	 count;
	int unacked;

	unacked = (dev->send_seq + (8 - dev->unack_seq)) & 0x07;

	/* dev->unack_seq < ack <= dev->send_seq; */

	if (unacked) {

		if (dev->send_seq > dev->unack_seq) {
			if (ack <= dev->unack_seq || ack > dev->send_seq) {
				printk(KERN_DEBUG
				     "layer 2 ack unacceptable - dev %d",
				       dev->id);

				pcbit_l2_error(dev);
			} else if (ack > dev->send_seq && ack <= dev->unack_seq) {
				printk(KERN_DEBUG
				     "layer 2 ack unacceptable - dev %d",
				       dev->id);
				pcbit_l2_error(dev);
			}
		}
		/* ack is acceptable */


		i = dev->unack_seq;

		do {
			dev->unack_seq = i = (i + 1) % 8;
			dev->free += dev->fsize[i];
		} while (i != ack);

		count = 0;
		while (count < 7 && dev->write_queue) {
			u8 lsend_seq = dev->send_seq;

			pcbit_transmit(dev);

			if (dev->send_seq == lsend_seq)
				break;
			count++;
		}
	} else
		printk(KERN_DEBUG "recv_ack: unacked = 0\n");
}
