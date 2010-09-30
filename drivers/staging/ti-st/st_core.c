/*
 *  Shared Transport Line discipline driver Core
 *	This hooks up ST KIM driver and ST LL driver
 *  Copyright (C) 2009 Texas Instruments
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

/* understand BT, FM and GPS for now */
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/hci.h>
#include <linux/ti_wilink_st.h>

/* strings to be used for rfkill entries and by
 * ST Core to be used for sysfs debug entry
 */
#define PROTO_ENTRY(type, name)	name
const unsigned char *protocol_strngs[] = {
	PROTO_ENTRY(ST_BT, "Bluetooth"),
	PROTO_ENTRY(ST_FM, "FM"),
	PROTO_ENTRY(ST_GPS, "GPS"),
};
/* function pointer pointing to either,
 * st_kim_recv during registration to receive fw download responses
 * st_int_recv after registration to receive proto stack responses
 */
void (*st_recv) (void*, const unsigned char*, long);

/********************************************************************/
#if 0
/* internal misc functions */
bool is_protocol_list_empty(void)
{
	unsigned char i = 0;
	pr_debug(" %s ", __func__);
	for (i = 0; i < ST_MAX; i++) {
		if (st_gdata->list[i] != NULL)
			return ST_NOTEMPTY;
		/* not empty */
	}
	/* list empty */
	return ST_EMPTY;
}
#endif

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
		return -1;
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
void st_send_frame(enum proto_type protoid, struct st_data_s *st_gdata)
{
	pr_info(" %s(prot:%d) ", __func__, protoid);

	if (unlikely
	    (st_gdata == NULL || st_gdata->rx_skb == NULL
	     || st_gdata->list[protoid] == NULL)) {
		pr_err("protocol %d not registered, no data to send?",
			   protoid);
		kfree_skb(st_gdata->rx_skb);
		return;
	}
	/* this cannot fail
	 * this shouldn't take long
	 * - should be just skb_queue_tail for the
	 *   protocol stack driver
	 */
	if (likely(st_gdata->list[protoid]->recv != NULL)) {
		if (unlikely
			(st_gdata->list[protoid]->recv
			(st_gdata->list[protoid]->priv_data, st_gdata->rx_skb)
			     != 0)) {
			pr_err(" proto stack %d's ->recv failed", protoid);
			kfree_skb(st_gdata->rx_skb);
			return;
		}
	} else {
		pr_err(" proto stack %d's ->recv null", protoid);
		kfree_skb(st_gdata->rx_skb);
	}
	return;
}

/**
 * st_reg_complete -
 * to call registration complete callbacks
 * of all protocol stack drivers
 */
void st_reg_complete(struct st_data_s *st_gdata, char err)
{
	unsigned char i = 0;
	pr_info(" %s ", __func__);
	for (i = 0; i < ST_MAX; i++) {
		if (likely(st_gdata != NULL && st_gdata->list[i] != NULL &&
			   st_gdata->list[i]->reg_complete_cb != NULL))
			st_gdata->list[i]->reg_complete_cb
				(st_gdata->list[i]->priv_data, err);
	}
}

static inline int st_check_data_len(struct st_data_s *st_gdata,
	int protoid, int len)
{
	register int room = skb_tailroom(st_gdata->rx_skb);

	pr_debug("len %d room %d", len, room);

	if (!len) {
		/* Received packet has only packet header and
		 * has zero length payload. So, ask ST CORE to
		 * forward the packet to protocol driver (BT/FM/GPS)
		 */
		st_send_frame(protoid, st_gdata);

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
		st_gdata->rx_state = ST_BT_W4_DATA;
		st_gdata->rx_count = len;
		return len;
	}

	/* Change ST state to continue to process next
	 * packet */
	st_gdata->rx_state = ST_W4_PACKET_TYPE;
	st_gdata->rx_skb = NULL;
	st_gdata->rx_count = 0;

	return 0;
}

/**
 * st_wakeup_ack - internal function for action when wake-up ack
 *	received
 */
static inline void st_wakeup_ack(struct st_data_s *st_gdata,
	unsigned char cmd)
{
	register struct sk_buff *waiting_skb;
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
	register char *ptr;
	struct hci_event_hdr *eh;
	struct hci_acl_hdr *ah;
	struct hci_sco_hdr *sh;
	struct fm_event_hdr *fm;
	struct gps_event_hdr *gps;
	register int len = 0, type = 0, dlen = 0;
	static enum proto_type protoid = ST_MAX;
	struct st_data_s *st_gdata = (struct st_data_s *)disc_data;

	ptr = (char *)data;
	/* tty_receive sent null ? */
	if (unlikely(ptr == NULL) || (st_gdata == NULL)) {
		pr_err(" received null from TTY ");
		return;
	}

	pr_info("count %ld rx_state %ld"
		   "rx_count %ld", count, st_gdata->rx_state,
		   st_gdata->rx_count);

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
			case ST_BT_W4_DATA:
				pr_debug("Complete pkt received");

				/* Ask ST CORE to forward
				 * the packet to protocol driver */
				st_send_frame(protoid, st_gdata);

				st_gdata->rx_state = ST_W4_PACKET_TYPE;
				st_gdata->rx_skb = NULL;
				protoid = ST_MAX;	/* is this required ? */
				continue;

				/* Waiting for Bluetooth event header ? */
			case ST_BT_W4_EVENT_HDR:
				eh = (struct hci_event_hdr *)st_gdata->rx_skb->
				    data;

				pr_debug("Event header: evt 0x%2.2x"
					   "plen %d", eh->evt, eh->plen);

				st_check_data_len(st_gdata, protoid, eh->plen);
				continue;

				/* Waiting for Bluetooth acl header ? */
			case ST_BT_W4_ACL_HDR:
				ah = (struct hci_acl_hdr *)st_gdata->rx_skb->
				    data;
				dlen = __le16_to_cpu(ah->dlen);

				pr_info("ACL header: dlen %d", dlen);

				st_check_data_len(st_gdata, protoid, dlen);
				continue;

				/* Waiting for Bluetooth sco header ? */
			case ST_BT_W4_SCO_HDR:
				sh = (struct hci_sco_hdr *)st_gdata->rx_skb->
				    data;

				pr_info("SCO header: dlen %d", sh->dlen);

				st_check_data_len(st_gdata, protoid, sh->dlen);
				continue;
			case ST_FM_W4_EVENT_HDR:
				fm = (struct fm_event_hdr *)st_gdata->rx_skb->
				    data;
				pr_info("FM Header: ");
				st_check_data_len(st_gdata, ST_FM, fm->plen);
				continue;
				/* TODO : Add GPS packet machine logic here */
			case ST_GPS_W4_EVENT_HDR:
				/* [0x09 pkt hdr][R/W byte][2 byte len] */
				gps = (struct gps_event_hdr *)st_gdata->rx_skb->
				     data;
				pr_info("GPS Header: ");
				st_check_data_len(st_gdata, ST_GPS, gps->plen);
				continue;
			}	/* end of switch rx_state */
		}

		/* end of if rx_count */
		/* Check first byte of packet and identify module
		 * owner (BT/FM/GPS) */
		switch (*ptr) {

			/* Bluetooth event packet? */
		case HCI_EVENT_PKT:
			pr_info("Event packet");
			st_gdata->rx_state = ST_BT_W4_EVENT_HDR;
			st_gdata->rx_count = HCI_EVENT_HDR_SIZE;
			type = HCI_EVENT_PKT;
			protoid = ST_BT;
			break;

			/* Bluetooth acl packet? */
		case HCI_ACLDATA_PKT:
			pr_info("ACL packet");
			st_gdata->rx_state = ST_BT_W4_ACL_HDR;
			st_gdata->rx_count = HCI_ACL_HDR_SIZE;
			type = HCI_ACLDATA_PKT;
			protoid = ST_BT;
			break;

			/* Bluetooth sco packet? */
		case HCI_SCODATA_PKT:
			pr_info("SCO packet");
			st_gdata->rx_state = ST_BT_W4_SCO_HDR;
			st_gdata->rx_count = HCI_SCO_HDR_SIZE;
			type = HCI_SCODATA_PKT;
			protoid = ST_BT;
			break;

			/* Channel 8(FM) packet? */
		case ST_FM_CH8_PKT:
			pr_info("FM CH8 packet");
			type = ST_FM_CH8_PKT;
			st_gdata->rx_state = ST_FM_W4_EVENT_HDR;
			st_gdata->rx_count = FM_EVENT_HDR_SIZE;
			protoid = ST_FM;
			break;

			/* Channel 9(GPS) packet? */
		case 0x9:	/*ST_LL_GPS_CH9_PKT */
			pr_info("GPS CH9 packet");
			type = 0x9;	/* ST_LL_GPS_CH9_PKT; */
			protoid = ST_GPS;
			st_gdata->rx_state = ST_GPS_W4_EVENT_HDR;
			st_gdata->rx_count = 3;	/* GPS_EVENT_HDR_SIZE -1*/
			break;
		case LL_SLEEP_IND:
		case LL_SLEEP_ACK:
		case LL_WAKE_UP_IND:
			pr_info("PM packet");
			/* this takes appropriate action based on
			 * sleep state received --
			 */
			st_ll_sleep_state(st_gdata, *ptr);
			ptr++;
			count--;
			continue;
		case LL_WAKE_UP_ACK:
			pr_info("PM packet");
			/* wake up ack received */
			st_wakeup_ack(st_gdata, *ptr);
			ptr++;
			count--;
			continue;
			/* Unknow packet? */
		default:
			pr_err("Unknown packet type %2.2x", (__u8) *ptr);
			ptr++;
			count--;
			continue;
		};
		ptr++;
		count--;

		switch (protoid) {
		case ST_BT:
			/* Allocate new packet to hold received data */
			st_gdata->rx_skb =
			    bt_skb_alloc(HCI_MAX_FRAME_SIZE, GFP_ATOMIC);
			if (!st_gdata->rx_skb) {
				pr_err("Can't allocate mem for new packet");
				st_gdata->rx_state = ST_W4_PACKET_TYPE;
				st_gdata->rx_count = 0;
				return;
			}
			bt_cb(st_gdata->rx_skb)->pkt_type = type;
			break;
		case ST_FM:	/* for FM */
			st_gdata->rx_skb =
			    alloc_skb(FM_MAX_FRAME_SIZE, GFP_ATOMIC);
			if (!st_gdata->rx_skb) {
				pr_err("Can't allocate mem for new packet");
				st_gdata->rx_state = ST_W4_PACKET_TYPE;
				st_gdata->rx_count = 0;
				return;
			}
			/* place holder 0x08 */
			skb_reserve(st_gdata->rx_skb, 1);
			st_gdata->rx_skb->cb[0] = ST_FM_CH8_PKT;
			break;
		case ST_GPS:
			/* for GPS */
			st_gdata->rx_skb =
			    alloc_skb(100 /*GPS_MAX_FRAME_SIZE */ , GFP_ATOMIC);
			if (!st_gdata->rx_skb) {
				pr_err("Can't allocate mem for new packet");
				st_gdata->rx_state = ST_W4_PACKET_TYPE;
				st_gdata->rx_count = 0;
				return;
			}
			/* place holder 0x09 */
			skb_reserve(st_gdata->rx_skb, 1);
			st_gdata->rx_skb->cb[0] = 0x09;	/*ST_GPS_CH9_PKT; */
			break;
		case ST_MAX:
			break;
		}
	}
	pr_debug("done %s", __func__);
	return;
}

/**
 * st_int_dequeue - internal de-Q function.
 *	If the previous data set was not written
 *	completely, return that skb which has the pending data.
 *	In normal cases, return top of txq.
 */
struct sk_buff *st_int_dequeue(struct st_data_s *st_gdata)
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
void st_int_enqueue(struct st_data_s *st_gdata, struct sk_buff *skb)
{
	unsigned long flags = 0;

	pr_debug("%s", __func__);
	spin_lock_irqsave(&st_gdata->lock, flags);

	switch (st_ll_getstate(st_gdata)) {
	case ST_LL_AWAKE:
		pr_info("ST LL is AWAKE, sending normally");
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
		pr_info("ST already sending");
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
			st_gdata->list[ST_BT] != NULL ? 'R' : 'U',
			st_gdata->list[ST_FM] != NULL ? 'R' : 'U',
			st_gdata->list[ST_GPS] != NULL ? 'R' : 'U');
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
	pr_info("%s(%d) ", __func__, new_proto->type);
	if (st_gdata == NULL || new_proto == NULL || new_proto->recv == NULL
	    || new_proto->reg_complete_cb == NULL) {
		pr_err("gdata/new_proto/recv or reg_complete_cb not ready");
		return -1;
	}

	if (new_proto->type < ST_BT || new_proto->type >= ST_MAX) {
		pr_err("protocol %d not supported", new_proto->type);
		return -EPROTONOSUPPORT;
	}

	if (st_gdata->list[new_proto->type] != NULL) {
		pr_err("protocol %d already registered", new_proto->type);
		return -EALREADY;
	}

	/* can be from process context only */
	spin_lock_irqsave(&st_gdata->lock, flags);

	if (test_bit(ST_REG_IN_PROGRESS, &st_gdata->st_state)) {
		pr_info(" ST_REG_IN_PROGRESS:%d ", new_proto->type);
		/* fw download in progress */
		st_kim_chip_toggle(new_proto->type, KIM_GPIO_ACTIVE);

		st_gdata->list[new_proto->type] = new_proto;
		st_gdata->protos_registered++;
		new_proto->write = st_write;

		set_bit(ST_REG_PENDING, &st_gdata->st_state);
		spin_unlock_irqrestore(&st_gdata->lock, flags);
		return -EINPROGRESS;
	} else if (st_gdata->protos_registered == ST_EMPTY) {
		pr_info(" protocol list empty :%d ", new_proto->type);
		set_bit(ST_REG_IN_PROGRESS, &st_gdata->st_state);
		st_recv = st_kim_recv;

		/* release lock previously held - re-locked below */
		spin_unlock_irqrestore(&st_gdata->lock, flags);

		/* enable the ST LL - to set default chip state */
		st_ll_enable(st_gdata);
		/* this may take a while to complete
		 * since it involves BT fw download
		 */
		err = st_kim_start(st_gdata->kim_data);
		if (err != 0) {
			clear_bit(ST_REG_IN_PROGRESS, &st_gdata->st_state);
			if ((st_gdata->protos_registered != ST_EMPTY) &&
			    (test_bit(ST_REG_PENDING, &st_gdata->st_state))) {
				pr_err(" KIM failure complete callback ");
				st_reg_complete(st_gdata, -1);
			}

			return -1;
		}

		/* the protocol might require other gpios to be toggled
		 */
		st_kim_chip_toggle(new_proto->type, KIM_GPIO_ACTIVE);

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
		if (st_gdata->list[new_proto->type] != NULL) {
			pr_err(" proto %d already registered ",
				   new_proto->type);
			return -EALREADY;
		}

		spin_lock_irqsave(&st_gdata->lock, flags);
		st_gdata->list[new_proto->type] = new_proto;
		st_gdata->protos_registered++;
		new_proto->write = st_write;
		spin_unlock_irqrestore(&st_gdata->lock, flags);
		return err;
	}
	/* if fw is already downloaded & new stack registers protocol */
	else {
		switch (new_proto->type) {
		case ST_BT:
			/* do nothing */
			break;
		case ST_FM:
		case ST_GPS:
			st_kim_chip_toggle(new_proto->type, KIM_GPIO_ACTIVE);
			break;
		case ST_MAX:
		default:
			pr_err("%d protocol not supported",
				   new_proto->type);
			spin_unlock_irqrestore(&st_gdata->lock, flags);
			return -EPROTONOSUPPORT;
		}
		st_gdata->list[new_proto->type] = new_proto;
		st_gdata->protos_registered++;
		new_proto->write = st_write;

		/* lock already held before entering else */
		spin_unlock_irqrestore(&st_gdata->lock, flags);
		return err;
	}
	pr_debug("done %s(%d) ", __func__, new_proto->type);
}
EXPORT_SYMBOL_GPL(st_register);

/* to unregister a protocol -
 * to be called from protocol stack driver
 */
long st_unregister(enum proto_type type)
{
	long err = 0;
	unsigned long flags = 0;
	struct st_data_s	*st_gdata;

	pr_debug("%s: %d ", __func__, type);

	st_kim_ref(&st_gdata, 0);
	if (type < ST_BT || type >= ST_MAX) {
		pr_err(" protocol %d not supported", type);
		return -EPROTONOSUPPORT;
	}

	spin_lock_irqsave(&st_gdata->lock, flags);

	if (st_gdata->list[type] == NULL) {
		pr_err(" protocol %d not registered", type);
		spin_unlock_irqrestore(&st_gdata->lock, flags);
		return -EPROTONOSUPPORT;
	}

	st_gdata->protos_registered--;
	st_gdata->list[type] = NULL;

	/* kim ignores BT in the below function
	 * and handles the rest, BT is toggled
	 * only in kim_start and kim_stop
	 */
	st_kim_chip_toggle(type, KIM_GPIO_INACTIVE);
	spin_unlock_irqrestore(&st_gdata->lock, flags);

	if ((st_gdata->protos_registered == ST_EMPTY) &&
	    (!test_bit(ST_REG_PENDING, &st_gdata->st_state))) {
		pr_info(" all protocols unregistered ");

		/* stop traffic on tty */
		if (st_gdata->tty) {
			tty_ldisc_flush(st_gdata->tty);
			stop_tty(st_gdata->tty);
		}

		/* all protocols now unregistered */
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
#ifdef DEBUG
	enum proto_type protoid = ST_MAX;
#endif
	long len;

	st_kim_ref(&st_gdata, 0);
	if (unlikely(skb == NULL || st_gdata == NULL
		|| st_gdata->tty == NULL)) {
		pr_err("data/tty unavailable to perform write");
		return -1;
	}
#ifdef DEBUG			/* open-up skb to read the 1st byte */
	switch (skb->data[0]) {
	case HCI_COMMAND_PKT:
	case HCI_ACLDATA_PKT:
	case HCI_SCODATA_PKT:
		protoid = ST_BT;
		break;
	case ST_FM_CH8_PKT:
		protoid = ST_FM;
		break;
	case 0x09:
		protoid = ST_GPS;
		break;
	}
	if (unlikely(st_gdata->list[protoid] == NULL)) {
		pr_err(" protocol %d not registered, and writing? ",
			   protoid);
		return -1;
	}
#endif
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
	unsigned char i = ST_MAX;
	unsigned long flags = 0;
	struct	st_data_s *st_gdata = tty->disc_data;

	pr_info("%s ", __func__);

	/* TODO:
	 * if a protocol has been registered & line discipline
	 * un-installed for some reason - what should be done ?
	 */
	spin_lock_irqsave(&st_gdata->lock, flags);
	for (i = ST_BT; i < ST_MAX; i++) {
		if (st_gdata->list[i] != NULL)
			pr_err("%d not un-registered", i);
		st_gdata->list[i] = NULL;
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

/********************************************************************/
int st_core_init(struct st_data_s **core_data)
{
	struct st_data_s *st_gdata;
	long err;
	static struct tty_ldisc_ops *st_ldisc_ops;

	/* populate and register to TTY line discipline */
	st_ldisc_ops = kzalloc(sizeof(*st_ldisc_ops), GFP_KERNEL);
	if (!st_ldisc_ops) {
		pr_err("no mem to allocate");
		return -ENOMEM;
	}

	st_ldisc_ops->magic = TTY_LDISC_MAGIC;
	st_ldisc_ops->name = "n_st";	/*"n_hci"; */
	st_ldisc_ops->open = st_tty_open;
	st_ldisc_ops->close = st_tty_close;
	st_ldisc_ops->receive_buf = st_tty_receive;
	st_ldisc_ops->write_wakeup = st_tty_wakeup;
	st_ldisc_ops->flush_buffer = st_tty_flush_buffer;
	st_ldisc_ops->owner = THIS_MODULE;

	err = tty_register_ldisc(N_TI_WL, st_ldisc_ops);
	if (err) {
		pr_err("error registering %d line discipline %ld",
			   N_TI_WL, err);
		kfree(st_ldisc_ops);
		return err;
	}
	pr_debug("registered n_shared line discipline");

	st_gdata = kzalloc(sizeof(struct st_data_s), GFP_KERNEL);
	if (!st_gdata) {
		pr_err("memory allocation failed");
		err = tty_unregister_ldisc(N_TI_WL);
		if (err)
			pr_err("unable to un-register ldisc %ld", err);
		kfree(st_ldisc_ops);
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

	/* ldisc_ops ref to be only used in __exit of module */
	st_gdata->ldisc_ops = st_ldisc_ops;

#if 0
	err = st_kim_init();
	if (err) {
		pr_err("error during kim initialization(%ld)", err);
		kfree(st_gdata);
		err = tty_unregister_ldisc(N_TI_WL);
		if (err)
			pr_err("unable to un-register ldisc");
		kfree(st_ldisc_ops);
		return -1;
	}
#endif

	err = st_ll_init(st_gdata);
	if (err) {
		pr_err("error during st_ll initialization(%ld)", err);
		kfree(st_gdata);
		err = tty_unregister_ldisc(N_TI_WL);
		if (err)
			pr_err("unable to un-register ldisc");
		kfree(st_ldisc_ops);
		return -1;
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
#if 0
	err = st_kim_deinit();
	if (err)
		pr_err("error during deinit of ST KIM %ld", err);
#endif
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
		kfree(st_gdata->ldisc_ops);
		/* free the global data pointer */
		kfree(st_gdata);
	}
}


