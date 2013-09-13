/*
 *  Shared Transport Line discipline driver Core
 *	This hooks up ST KIM driver and ST LL driver
 *  Copyright (C) 2009-2010 Texas Instruments
 *  Author: Pavan Savoy <pavan_savoy@ti.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#define pr_fmt(fmt)	"(stc): " fmt
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tty.h>

#include <linux/seq_file.h>
#include <linux/skbuff.h>

#include <linux/ti_wilink_st.h>

extern void st_kim_recv(void *, const unsigned char *, long);
void st_int_recv(void *, const unsigned char *, long);
/* function pointer pointing to either,
 * st_kim_recv during registration to receive fw download responses
 * st_int_recv after registration to receive proto stack responses
 */
static void (*st_recv) (void *, const unsigned char *, long);

/********************************************************************/
static void add_channel_to_table(struct st_data_s *st_gdata,
		struct st_proto_s *new_proto)
{
	pr_info("%s: id %d\n", __func__, new_proto->chnl_id);
	/* list now has the channel id as index itself */
	st_gdata->list[new_proto->chnl_id] = new_proto;
	st_gdata->is_registered[new_proto->chnl_id] = true;
}

static void remove_channel_from_table(struct st_data_s *st_gdata,
		struct st_proto_s *proto)
{
	pr_info("%s: id %d\n", __func__, proto->chnl_id);
/*	st_gdata->list[proto->chnl_id] = NULL; */
	st_gdata->is_registered[proto->chnl_id] = false;
}

/*
 * called from KIM during firmware download.
 *
 * This is a wrapper function to tty->ops->write_room.
 * It returns number of free space available in
 * uart tx buffer.
 */
int st_get_uart_wr_room(struct st_data_s *st_gdata)
{
	struct tty_struct *tty;
	if (unlikely(st_gdata == NULL || st_gdata->tty == NULL)) {
		pr_err("tty unavailable to perform write");
		return -1;
	}
	tty = st_gdata->tty;
	return tty->ops->write_room(tty);
}

/* can be called in from
 * -- KIM (during fw download)
 * -- ST Core (during st_write)
 *
 *  This is the internal write function - a wrapper
 *  to tty->ops->write
 */
int st_int_write(struct st_data_s *st_gdata,
	const unsigned char *data, int count)
{
	struct tty_struct *tty;
	if (unlikely(st_gdata == NULL || st_gdata->tty == NULL)) {
		pr_err("tty unavailable to perform write");
		return -EINVAL;
	}
	tty = st_gdata->tty;
#ifdef VERBOSE
	print_hex_dump(KERN_DEBUG, "<out<", DUMP_PREFIX_NONE,
		16, 1, data, count, 0);
#endif
	return tty->ops->write(tty, data, count);

}

/*
 * push the skb received to relevant
 * protocol stacks
 */
static void st_send_frame(unsigned char chnl_id, struct st_data_s *st_gdata)
{
	pr_debug(" %s(prot:%d) ", __func__, chnl_id);

	if (unlikely
	    (st_gdata == NULL || st_gdata->rx_skb == NULL
	     || st_gdata->is_registered[chnl_id] == false)) {
		pr_err("chnl_id %d not registered, no data to send?",
			   chnl_id);
		kfree_skb(st_gdata->rx_skb);
		return;
	}
	/* this cannot fail
	 * this shouldn't take long
	 * - should be just skb_queue_tail for the
	 *   protocol stack driver
	 */
	if (likely(st_gdata->list[chnl_id]->recv != NULL)) {
		if (unlikely
			(st_gdata->list[chnl_id]->recv
			(st_gdata->list[chnl_id]->priv_data, st_gdata->rx_skb)
			     != 0)) {
			pr_err(" proto stack %d's ->recv failed", chnl_id);
			kfree_skb(st_gdata->rx_skb);
			return;
		}
	} else {
		pr_err(" proto stack %d's ->recv null", chnl_id);
		kfree_skb(st_gdata->rx_skb);
	}
	return;
}

/**
 * st_reg_complete -
 * to call registration complete callbacks
 * of all protocol stack drivers
 * This function is being called with spin lock held, protocol drivers are
 * only expected to complete their waits and do nothing more than that.
 */
static void st_reg_complete(struct st_data_s *st_gdata, char err)
{
	unsigned char i = 0;
	pr_info(" %s ", __func__);
	for (i = 0; i < ST_MAX_CHANNELS; i++) {
		if (likely(st_gdata != NULL &&
			st_gdata->is_registered[i] == true &&
				st_gdata->list[i]->reg_complete_cb != NULL)) {
			st_gdata->list[i]->reg_complete_cb
				(st_gdata->list[i]->priv_data, err);
			pr_info("protocol %d's cb sent %d\n", i, err);
			if (err) { /* cleanup registered protocol */
				st_gdata->protos_registered--;
				st_gdata->is_registered[i] = false;
			}
		}
	}
}

static inline int st_check_data_len(struct st_data_s *st_gdata,
	unsigned char chnl_id, int len)
{
	int room = skb_tailroom(st_gdata->rx_skb);

	pr_debug("len %d room %d", len, room);

	if (!len) {
		/* Received packet has only packet header and
		 * has zero length payload. So, ask ST CORE to
		 * forward the packet to protocol driver (BT/FM/GPS)
		 */
		st_send_frame(chnl_id, st_gdata);

	} else if (len > room) {
		/* Received packet's payload length is larger.
		 * We can't accommodate it in created skb.
		 */
		pr_err("Data length is too large len %d room %d", len,
			   room);
		kfree_skb(st_gdata->rx_skb);
	} else {
		/* Packet header has non-zero payload length and
		 * we have enough space in created skb. Lets read
		 * payload data */
		st_gdata->rx_state = ST_W4_DATA;
		st_gdata->rx_count = len;
		return len;
	}

	/* Change ST state to continue to process next
	 * packet */
	st_gdata->rx_state = ST_W4_PACKET_TYPE;
	st_gdata->rx_skb = NULL;
	st_gdata->rx_count = 0;
	st_gdata->rx_chnl = 0;

	return 0;
}

/**
 * st_wakeup_ack - internal function for action when wake-up ack
 *	received
 */
static inline void st_wakeup_ack(struct st_data_s *st_gdata,
	unsigned char cmd)
{
	struct sk_buff *waiting_skb;
	unsigned long flags = 0;

	spin_lock_irqsave(&st_gdata->lock, flags);
	/* de-Q from waitQ and Q in txQ now that the
	 * chip is awake
	 */
	while ((waiting_skb = skb_dequeue(&st_gdata->tx_waitq)))
		skb_queue_tail(&st_gdata->txq, waiting_skb);

	/* state forwarded to ST LL */
	st_ll_sleep_state(st_gdata, (unsigned long)cmd);
	spin_unlock_irqrestore(&st_gdata->lock, flags);

	/* wake up to send the recently copied skbs from waitQ */
	st_tx_wakeup(st_gdata);
}

/**
 * st_int_recv - ST's internal receive function.
 *	Decodes received RAW data and forwards to corresponding
 *	client drivers (Bluetooth,FM,GPS..etc).
 *	This can receive various types of packets,
 *	HCI-Events, ACL, SCO, 4 types of HCI-LL PM packets
 *	CH-8 packets from FM, CH-9 packets from GPS cores.
 */
void st_int_recv(void *disc_data,
	const unsigned char *data, long count)
{
	char *ptr;
	struct st_proto_s *proto;
	unsigned short payload_len = 0;
	int len = 0;
	unsigned char type = 0;
	unsigned char *plen;
	struct st_data_s *st_gdata = (struct st_data_s *)disc_data;
	unsigned long flags;

	ptr = (char *)data;
	/* tty_receive sent null ? */
	if (unlikely(ptr == NULL) || (st_gdata == NULL)) {
		pr_err(" received null from TTY ");
		return;
	}

	pr_debug("count %ld rx_state %ld"
		   "rx_count %ld", count, st_gdata->rx_state,
		   st_gdata->rx_count);

	spin_lock_irqsave(&st_gdata->lock, flags);
	/* Decode received bytes here */
	while (count) {
		if (st_gdata->rx_count) {
			len = min_t(unsigned int, st_gdata->rx_count, count);
			memcpy(skb_put(st_gdata->rx_skb, len), ptr, len);
			st_gdata->rx_count -= len;
			count -= len;
			ptr += len;

			if (st_gdata->rx_count)
				continue;

			/* Check ST RX state machine , where are we? */
			switch (st_gdata->rx_state) {
			/* Waiting for complete packet ? */
			case ST_W4_DATA:
				pr_debug("Complete pkt received");
				/* Ask ST CORE to forward
				 * the packet to protocol driver */
				st_send_frame(st_gdata->rx_chnl, st_gdata);

				st_gdata->rx_state = ST_W4_PACKET_TYPE;
				st_gdata->rx_skb = NULL;
				continue;
			/* parse the header to know details */
			case ST_W4_HEADER:
				proto = st_gdata->list[st_gdata->rx_chnl];
				plen =
				&st_gdata->rx_skb->data
				[proto->offset_len_in_hdr];
				pr_debug("plen pointing to %x\n", *plen);
				if (proto->len_size == 1)/* 1 byte len field */
					payload_len = *(unsigned char *)plen;
				else if (proto->len_size == 2)
					payload_len =
					__le16_to_cpu(*(unsigned short *)plen);
				else
					pr_info("%s: invalid length "
					"for id %d\n",
					__func__, proto->chnl_id);
				st_check_data_len(st_gdata, proto->chnl_id,
						payload_len);
				pr_debug("off %d, pay len %d\n",
					proto->offset_len_in_hdr, payload_len);
				continue;
			}	/* end of switch rx_state */
		}

		/* end of if rx_count */
		/* Check first byte of packet and identify module
		 * owner (BT/FM/GPS) */
		switch (*ptr) {
		case LL_SLEEP_IND:
		case LL_SLEEP_ACK:
		case LL_WAKE_UP_IND:
			pr_debug("PM packet");
			/* this takes appropriate action based on
			 * sleep state received --
			 */
			st_ll_sleep_state(st_gdata, *ptr);
			/* if WAKEUP_IND collides copy from waitq to txq
			 * and assume chip awake
			 */
			spin_unlock_irqrestore(&st_gdata->lock, flags);
			if (st_ll_getstate(st_gdata) == ST_LL_AWAKE)
				st_wakeup_ack(st_gdata, LL_WAKE_UP_ACK);
			spin_lock_irqsave(&st_gdata->lock, flags);

			ptr++;
			count--;
			continue;
		case LL_WAKE_UP_ACK:
			pr_debug("PM packet");

			spin_unlock_irqrestore(&st_gdata->lock, flags);
			/* wake up ack received */
			st_wakeup_ack(st_gdata, *ptr);
			spin_lock_irqsave(&st_gdata->lock, flags);

			ptr++;
			count--;
			continue;
			/* Unknow packet? */
		default:
			type = *ptr;
			if (st_gdata->list[type] == NULL) {
				pr_err("chip/interface misbehavior dropping"
					" frame starting with 0x%02x", type);
				goto done;

			}
			st_gdata->rx_skb = alloc_skb(
					st_gdata->list[type]->max_frame_size,
					GFP_ATOMIC);
			if (st_gdata->rx_skb == NULL) {
				pr_err("out of memory: dropping\n");
				goto done;
			}

			skb_reserve(st_gdata->rx_skb,
					st_gdata->list[type]->reserve);
			/* next 2 required for BT only */
			st_gdata->rx_skb->cb[0] = type; /*pkt_type*/
			st_gdata->rx_skb->cb[1] = 0; /*incoming*/
			st_gdata->rx_chnl = *ptr;
			st_gdata->rx_state = ST_W4_HEADER;
			st_gdata->rx_count = st_gdata->list[type]->hdr_len;
			pr_debug("rx_count %ld\n", st_gdata->rx_count);
		};
		ptr++;
		count--;
	}
done:
	spin_unlock_irqrestore(&st_gdata->lock, flags);
	pr_debug("done %s", __func__);
	return;
}

/**
 * st_int_dequeue - internal de-Q function.
 *	If the previous data set was not written
 *	completely, return that skb which has the pending data.
 *	In normal cases, return top of txq.
 */
static struct sk_buff *st_int_dequeue(struct st_data_s *st_gdata)
{
	struct sk_buff *returning_skb;

	pr_debug("%s", __func__);
	if (st_gdata->tx_skb != NULL) {
		returning_skb = st_gdata->tx_skb;
		st_gdata->tx_skb = NULL;
		return returning_skb;
	}
	return skb_dequeue(&st_gdata->txq);
}

/**
 * st_int_enqueue - internal Q-ing function.
 *	Will either Q the skb to txq or the tx_waitq
 *	depending on the ST LL state.
 *	If the chip is asleep, then Q it onto waitq and
 *	wakeup the chip.
 *	txq and waitq needs protection since the other contexts
 *	may be sending data, waking up chip.
 */
static void st_int_enqueue(struct st_data_s *st_gdata, struct sk_buff *skb)
{
	unsigned long flags = 0;

	pr_debug("%s", __func__);
	spin_lock_irqsave(&st_gdata->lock, flags);

	switch (st_ll_getstate(st_gdata)) {
	case ST_LL_AWAKE:
		pr_debug("ST LL is AWAKE, sending normally");
		skb_queue_tail(&st_gdata->txq, skb);
		break;
	case ST_LL_ASLEEP_TO_AWAKE:
		skb_queue_tail(&st_gdata->tx_waitq, skb);
		break;
	case ST_LL_AWAKE_TO_ASLEEP:
		pr_err("ST LL is illegal state(%ld),"
			   "purging received skb.", st_ll_getstate(st_gdata));
		kfree_skb(skb);
		break;
	case ST_LL_ASLEEP:
		skb_queue_tail(&st_gdata->tx_waitq, skb);
		st_ll_wakeup(st_gdata);
		break;
	default:
		pr_err("ST LL is illegal state(%ld),"
			   "purging received skb.", st_ll_getstate(st_gdata));
		kfree_skb(skb);
		break;
	}

	spin_unlock_irqrestore(&st_gdata->lock, flags);
	pr_debug("done %s", __func__);
	return;
}

/*
 * internal wakeup function
 * called from either
 * - TTY layer when write's finished
 * - st_write (in context of the protocol stack)
 */
void st_tx_wakeup(struct st_data_s *st_data)
{
	struct sk_buff *skb;
	unsigned long flags;	/* for irq save flags */
	pr_debug("%s", __func__);
	/* check for sending & set flag sending here */
	if (test_and_set_bit(ST_TX_SENDING, &st_data->tx_state)) {
		pr_debug("ST already sending");
		/* keep sending */
		set_bit(ST_TX_WAKEUP, &st_data->tx_state);
		return;
		/* TX_WAKEUP will be checked in another
		 * context
		 */
	}
	do {			/* come back if st_tx_wakeup is set */
		/* woke-up to write */
		clear_bit(ST_TX_WAKEUP, &st_data->tx_state);
		while ((skb = st_int_dequeue(st_data))) {
			int len;
			spin_lock_irqsave(&st_data->lock, flags);
			/* enable wake-up from TTY */
			set_bit(TTY_DO_WRITE_WAKEUP, &st_data->tty->flags);
			len = st_int_write(st_data, skb->data, skb->len);
			skb_pull(skb, len);
			/* if skb->len = len as expected, skb->len=0 */
			if (skb->len) {
				/* would be the next skb to be sent */
				st_data->tx_skb = skb;
				spin_unlock_irqrestore(&st_data->lock, flags);
				break;
			}
			kfree_skb(skb);
			spin_unlock_irqrestore(&st_data->lock, flags);
		}
		/* if wake-up is set in another context- restart sending */
	} while (test_bit(ST_TX_WAKEUP, &st_data->tx_state));

	/* clear flag sending */
	clear_bit(ST_TX_SENDING, &st_data->tx_state);
}

/********************************************************************/
/* functions called from ST KIM
*/
void kim_st_list_protocols(struct st_data_s *st_gdata, void *buf)
{
	seq_printf(buf, "[%d]\nBT=%c\nFM=%c\nGPS=%c\n",
			st_gdata->protos_registered,
			st_gdata->is_registered[0x04] == true ? 'R' : 'U',
			st_gdata->is_registered[0x08] == true ? 'R' : 'U',
			st_gdata->is_registered[0x09] == true ? 'R' : 'U');
}

/********************************************************************/
/*
 * functions called from protocol stack drivers
 * to be EXPORT-ed
 */
long st_register(struct st_proto_s *new_proto)
{
	struct st_data_s	*st_gdata;
	long err = 0;
	unsigned long flags = 0;

	st_kim_ref(&st_gdata, 0);
	if (st_gdata == NULL || new_proto == NULL || new_proto->recv == NULL
	    || new_proto->reg_complete_cb == NULL) {
		pr_err("gdata/new_proto/recv or reg_complete_cb not ready");
		return -EINVAL;
	}

	if (new_proto->chnl_id >= ST_MAX_CHANNELS) {
		pr_err("chnl_id %d not supported", new_proto->chnl_id);
		return -EPROTONOSUPPORT;
	}

	if (st_gdata->is_registered[new_proto->chnl_id] == true) {
		pr_err("chnl_id %d already registered", new_proto->chnl_id);
		return -EALREADY;
	}

	/* can be from process context only */
	spin_lock_irqsave(&st_gdata->lock, flags);

	if (test_bit(ST_REG_IN_PROGRESS, &st_gdata->st_state)) {
		pr_info(" ST_REG_IN_PROGRESS:%d ", new_proto->chnl_id);
		/* fw download in progress */

		add_channel_to_table(st_gdata, new_proto);
		st_gdata->protos_registered++;
		new_proto->write = st_write;

		set_bit(ST_REG_PENDING, &st_gdata->st_state);
		spin_unlock_irqrestore(&st_gdata->lock, flags);
		return -EINPROGRESS;
	} else if (st_gdata->protos_registered == ST_EMPTY) {
		pr_info(" chnl_id list empty :%d ", new_proto->chnl_id);
		set_bit(ST_REG_IN_PROGRESS, &st_gdata->st_state);
		st_recv = st_kim_recv;

		/* enable the ST LL - to set default chip state */
		st_ll_enable(st_gdata);

		/* release lock previously held - re-locked below */
		spin_unlock_irqrestore(&st_gdata->lock, flags);

		/* this may take a while to complete
		 * since it involves BT fw download
		 */
		err = st_kim_start(st_gdata->kim_data);
		if (err != 0) {
			clear_bit(ST_REG_IN_PROGRESS, &st_gdata->st_state);
			if ((st_gdata->protos_registered != ST_EMPTY) &&
			    (test_bit(ST_REG_PENDING, &st_gdata->st_state))) {
				pr_err(" KIM failure complete callback ");
				spin_lock_irqsave(&st_gdata->lock, flags);
				st_reg_complete(st_gdata, err);
				spin_unlock_irqrestore(&st_gdata->lock, flags);
				clear_bit(ST_REG_PENDING, &st_gdata->st_state);
			}
			return -EINVAL;
		}

		spin_lock_irqsave(&st_gdata->lock, flags);

		clear_bit(ST_REG_IN_PROGRESS, &st_gdata->st_state);
		st_recv = st_int_recv;

		/* this is where all pending registration
		 * are signalled to be complete by calling callback functions
		 */
		if ((st_gdata->protos_registered != ST_EMPTY) &&
		    (test_bit(ST_REG_PENDING, &st_gdata->st_state))) {
			pr_debug(" call reg complete callback ");
			st_reg_complete(st_gdata, 0);
		}
		clear_bit(ST_REG_PENDING, &st_gdata->st_state);

		/* check for already registered once more,
		 * since the above check is old
		 */
		if (st_gdata->is_registered[new_proto->chnl_id] == true) {
			pr_err(" proto %d already registered ",
				   new_proto->chnl_id);
			spin_unlock_irqrestore(&st_gdata->lock, flags);
			return -EALREADY;
		}

		add_channel_to_table(st_gdata, new_proto);
		st_gdata->protos_registered++;
		new_proto->write = st_write;
		spin_unlock_irqrestore(&st_gdata->lock, flags);
		return err;
	}
	/* if fw is already downloaded & new stack registers protocol */
	else {
		add_channel_to_table(st_gdata, new_proto);
		st_gdata->protos_registered++;
		new_proto->write = st_write;

		/* lock already held before entering else */
		spin_unlock_irqrestore(&st_gdata->lock, flags);
		return err;
	}
	pr_debug("done %s(%d) ", __func__, new_proto->chnl_id);
}
EXPORT_SYMBOL_GPL(st_register);

/* to unregister a protocol -
 * to be called from protocol stack driver
 */
long st_unregister(struct st_proto_s *proto)
{
	long err = 0;
	unsigned long flags = 0;
	struct st_data_s	*st_gdata;

	pr_debug("%s: %d ", __func__, proto->chnl_id);

	st_kim_ref(&st_gdata, 0);
	if (!st_gdata || proto->chnl_id >= ST_MAX_CHANNELS) {
		pr_err(" chnl_id %d not supported", proto->chnl_id);
		return -EPROTONOSUPPORT;
	}

	spin_lock_irqsave(&st_gdata->lock, flags);

	if (st_gdata->is_registered[proto->chnl_id] == false) {
		pr_err(" chnl_id %d not registered", proto->chnl_id);
		spin_unlock_irqrestore(&st_gdata->lock, flags);
		return -EPROTONOSUPPORT;
	}

	st_gdata->protos_registered--;
	remove_channel_from_table(st_gdata, proto);
	spin_unlock_irqrestore(&st_gdata->lock, flags);

	/* paranoid check */
	if (st_gdata->protos_registered < ST_EMPTY)
		st_gdata->protos_registered = ST_EMPTY;

	if ((st_gdata->protos_registered == ST_EMPTY) &&
	    (!test_bit(ST_REG_PENDING, &st_gdata->st_state))) {
		pr_info(" all chnl_ids unregistered ");

		/* stop traffic on tty */
		if (st_gdata->tty) {
			tty_ldisc_flush(st_gdata->tty);
			stop_tty(st_gdata->tty);
		}

		/* all chnl_ids now unregistered */
		st_kim_stop(st_gdata->kim_data);
		/* disable ST LL */
		st_ll_disable(st_gdata);
	}
	return err;
}

/*
 * called in protocol stack drivers
 * via the write function pointer
 */
long st_write(struct sk_buff *skb)
{
	struct st_data_s *st_gdata;
	long len;

	st_kim_ref(&st_gdata, 0);
	if (unlikely(skb == NULL || st_gdata == NULL
		|| st_gdata->tty == NULL)) {
		pr_err("data/tty unavailable to perform write");
		return -EINVAL;
	}

	pr_debug("%d to be written", skb->len);
	len = skb->len;

	/* st_ll to decide where to enqueue the skb */
	st_int_enqueue(st_gdata, skb);
	/* wake up */
	st_tx_wakeup(st_gdata);

	/* return number of bytes written */
	return len;
}

/* for protocols making use of shared transport */
EXPORT_SYMBOL_GPL(st_unregister);

/********************************************************************/
/*
 * functions called from TTY layer
 */
static int st_tty_open(struct tty_struct *tty)
{
	int err = 0;
	struct st_data_s *st_gdata;
	pr_info("%s ", __func__);

	st_kim_ref(&st_gdata, 0);
	st_gdata->tty = tty;
	tty->disc_data = st_gdata;

	/* don't do an wakeup for now */
	clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);

	/* mem already allocated
	 */
	tty->receive_room = 65536;
	/* Flush any pending characters in the driver and discipline. */
	tty_ldisc_flush(tty);
	tty_driver_flush_buffer(tty);
	/*
	 * signal to UIM via KIM that -
	 * installation of N_TI_WL ldisc is complete
	 */
	st_kim_complete(st_gdata->kim_data);
	pr_debug("done %s", __func__);
	return err;
}

static void st_tty_close(struct tty_struct *tty)
{
	unsigned char i = ST_MAX_CHANNELS;
	unsigned long flags = 0;
	struct	st_data_s *st_gdata = tty->disc_data;

	pr_info("%s ", __func__);

	/* TODO:
	 * if a protocol has been registered & line discipline
	 * un-installed for some reason - what should be done ?
	 */
	spin_lock_irqsave(&st_gdata->lock, flags);
	for (i = ST_BT; i < ST_MAX_CHANNELS; i++) {
		if (st_gdata->is_registered[i] == true)
			pr_err("%d not un-registered", i);
		st_gdata->list[i] = NULL;
		st_gdata->is_registered[i] = false;
	}
	st_gdata->protos_registered = 0;
	spin_unlock_irqrestore(&st_gdata->lock, flags);
	/*
	 * signal to UIM via KIM that -
	 * N_TI_WL ldisc is un-installed
	 */
	st_kim_complete(st_gdata->kim_data);
	st_gdata->tty = NULL;
	/* Flush any pending characters in the driver and discipline. */
	tty_ldisc_flush(tty);
	tty_driver_flush_buffer(tty);

	spin_lock_irqsave(&st_gdata->lock, flags);
	/* empty out txq and tx_waitq */
	skb_queue_purge(&st_gdata->txq);
	skb_queue_purge(&st_gdata->tx_waitq);
	/* reset the TTY Rx states of ST */
	st_gdata->rx_count = 0;
	st_gdata->rx_state = ST_W4_PACKET_TYPE;
	kfree_skb(st_gdata->rx_skb);
	st_gdata->rx_skb = NULL;
	spin_unlock_irqrestore(&st_gdata->lock, flags);

	pr_debug("%s: done ", __func__);
}

static void st_tty_receive(struct tty_struct *tty, const unsigned char *data,
			   char *tty_flags, int count)
{
#ifdef VERBOSE
	print_hex_dump(KERN_DEBUG, ">in>", DUMP_PREFIX_NONE,
		16, 1, data, count, 0);
#endif

	/*
	 * if fw download is in progress then route incoming data
	 * to KIM for validation
	 */
	st_recv(tty->disc_data, data, count);
	pr_debug("done %s", __func__);
}

/* wake-up function called in from the TTY layer
 * inside the internal wakeup function will be called
 */
static void st_tty_wakeup(struct tty_struct *tty)
{
	struct	st_data_s *st_gdata = tty->disc_data;
	pr_debug("%s ", __func__);
	/* don't do an wakeup for now */
	clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);

	/* call our internal wakeup */
	st_tx_wakeup((void *)st_gdata);
}

static void st_tty_flush_buffer(struct tty_struct *tty)
{
	struct	st_data_s *st_gdata = tty->disc_data;
	pr_debug("%s ", __func__);

	kfree_skb(st_gdata->tx_skb);
	st_gdata->tx_skb = NULL;

	tty->ops->flush_buffer(tty);
	return;
}

static struct tty_ldisc_ops st_ldisc_ops = {
	.magic = TTY_LDISC_MAGIC,
	.name = "n_st",
	.open = st_tty_open,
	.close = st_tty_close,
	.receive_buf = st_tty_receive,
	.write_wakeup = st_tty_wakeup,
	.flush_buffer = st_tty_flush_buffer,
	.owner = THIS_MODULE
};

/********************************************************************/
int st_core_init(struct st_data_s **core_data)
{
	struct st_data_s *st_gdata;
	long err;

	err = tty_register_ldisc(N_TI_WL, &st_ldisc_ops);
	if (err) {
		pr_err("error registering %d line discipline %ld",
			   N_TI_WL, err);
		return err;
	}
	pr_debug("registered n_shared line discipline");

	st_gdata = kzalloc(sizeof(struct st_data_s), GFP_KERNEL);
	if (!st_gdata) {
		pr_err("memory allocation failed");
		err = tty_unregister_ldisc(N_TI_WL);
		if (err)
			pr_err("unable to un-register ldisc %ld", err);
		err = -ENOMEM;
		return err;
	}

	/* Initialize ST TxQ and Tx waitQ queue head. All BT/FM/GPS module skb's
	 * will be pushed in this queue for actual transmission.
	 */
	skb_queue_head_init(&st_gdata->txq);
	skb_queue_head_init(&st_gdata->tx_waitq);

	/* Locking used in st_int_enqueue() to avoid multiple execution */
	spin_lock_init(&st_gdata->lock);

	err = st_ll_init(st_gdata);
	if (err) {
		pr_err("error during st_ll initialization(%ld)", err);
		kfree(st_gdata);
		err = tty_unregister_ldisc(N_TI_WL);
		if (err)
			pr_err("unable to un-register ldisc");
		return err;
	}
	*core_data = st_gdata;
	return 0;
}

void st_core_exit(struct st_data_s *st_gdata)
{
	long err;
	/* internal module cleanup */
	err = st_ll_deinit(st_gdata);
	if (err)
		pr_err("error during deinit of ST LL %ld", err);

	if (st_gdata != NULL) {
		/* Free ST Tx Qs and skbs */
		skb_queue_purge(&st_gdata->txq);
		skb_queue_purge(&st_gdata->tx_waitq);
		kfree_skb(st_gdata->rx_skb);
		kfree_skb(st_gdata->tx_skb);
		/* TTY ldisc cleanup */
		err = tty_unregister_ldisc(N_TI_WL);
		if (err)
			pr_err("unable to un-register ldisc %ld", err);
		/* free the global data pointer */
		kfree(st_gdata);
	}
}


