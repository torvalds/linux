/*
 *  drivers/s390/net/qeth_core_main.c
 *
 *    Copyright IBM Corp. 2007, 2009
 *    Author(s): Utz Bacher <utz.bacher@de.ibm.com>,
 *		 Frank Pavlic <fpavlic@de.ibm.com>,
 *		 Thomas Spatzier <tspat@de.ibm.com>,
 *		 Frank Blaschka <frank.blaschka@de.ibm.com>
 */

#define KMSG_COMPONENT "qeth"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/mii.h>
#include <linux/kthread.h>
#include <linux/slab.h>

#include <asm/ebcdic.h>
#include <asm/io.h>

#include "qeth_core.h"

struct qeth_dbf_info qeth_dbf[QETH_DBF_INFOS] = {
	/* define dbf - Name, Pages, Areas, Maxlen, Level, View, Handle */
	/*                   N  P  A    M  L  V                      H  */
	[QETH_DBF_SETUP] = {"qeth_setup",
				8, 1,   8, 5, &debug_hex_ascii_view, NULL},
	[QETH_DBF_QERR]  = {"qeth_qerr",
				2, 1,   8, 2, &debug_hex_ascii_view, NULL},
	[QETH_DBF_TRACE] = {"qeth_trace",
				4, 1,   8, 3, &debug_hex_ascii_view, NULL},
	[QETH_DBF_MSG]   = {"qeth_msg",
				8, 1, 128, 3, &debug_sprintf_view,   NULL},
	[QETH_DBF_SENSE] = {"qeth_sense",
				2, 1,  64, 2, &debug_hex_ascii_view, NULL},
	[QETH_DBF_MISC]	 = {"qeth_misc",
				2, 1, 256, 2, &debug_hex_ascii_view, NULL},
	[QETH_DBF_CTRL]  = {"qeth_control",
		8, 1, QETH_DBF_CTRL_LEN, 5, &debug_hex_ascii_view, NULL},
};
EXPORT_SYMBOL_GPL(qeth_dbf);

struct qeth_card_list_struct qeth_core_card_list;
EXPORT_SYMBOL_GPL(qeth_core_card_list);
struct kmem_cache *qeth_core_header_cache;
EXPORT_SYMBOL_GPL(qeth_core_header_cache);

static struct device *qeth_core_root_dev;
static unsigned int known_devices[][6] = QETH_MODELLIST_ARRAY;
static struct lock_class_key qdio_out_skb_queue_key;

static void qeth_send_control_data_cb(struct qeth_channel *,
			struct qeth_cmd_buffer *);
static int qeth_issue_next_read(struct qeth_card *);
static struct qeth_cmd_buffer *qeth_get_buffer(struct qeth_channel *);
static void qeth_setup_ccw(struct qeth_channel *, unsigned char *, __u32);
static void qeth_free_buffer_pool(struct qeth_card *);
static int qeth_qdio_establish(struct qeth_card *);


static inline void __qeth_fill_buffer_frag(struct sk_buff *skb,
		struct qdio_buffer *buffer, int is_tso,
		int *next_element_to_fill)
{
	struct skb_frag_struct *frag;
	int fragno;
	unsigned long addr;
	int element, cnt, dlen;

	fragno = skb_shinfo(skb)->nr_frags;
	element = *next_element_to_fill;
	dlen = 0;

	if (is_tso)
		buffer->element[element].flags =
			SBAL_FLAGS_MIDDLE_FRAG;
	else
		buffer->element[element].flags =
			SBAL_FLAGS_FIRST_FRAG;
	dlen = skb->len - skb->data_len;
	if (dlen) {
		buffer->element[element].addr = skb->data;
		buffer->element[element].length = dlen;
		element++;
	}
	for (cnt = 0; cnt < fragno; cnt++) {
		frag = &skb_shinfo(skb)->frags[cnt];
		addr = (page_to_pfn(frag->page) << PAGE_SHIFT) +
			frag->page_offset;
		buffer->element[element].addr = (char *)addr;
		buffer->element[element].length = frag->size;
		if (cnt < (fragno - 1))
			buffer->element[element].flags =
				SBAL_FLAGS_MIDDLE_FRAG;
		else
			buffer->element[element].flags =
				SBAL_FLAGS_LAST_FRAG;
		element++;
	}
	*next_element_to_fill = element;
}

static inline const char *qeth_get_cardname(struct qeth_card *card)
{
	if (card->info.guestlan) {
		switch (card->info.type) {
		case QETH_CARD_TYPE_OSD:
			return " Guest LAN QDIO";
		case QETH_CARD_TYPE_IQD:
			return " Guest LAN Hiper";
		case QETH_CARD_TYPE_OSM:
			return " Guest LAN QDIO - OSM";
		case QETH_CARD_TYPE_OSX:
			return " Guest LAN QDIO - OSX";
		default:
			return " unknown";
		}
	} else {
		switch (card->info.type) {
		case QETH_CARD_TYPE_OSD:
			return " OSD Express";
		case QETH_CARD_TYPE_IQD:
			return " HiperSockets";
		case QETH_CARD_TYPE_OSN:
			return " OSN QDIO";
		case QETH_CARD_TYPE_OSM:
			return " OSM QDIO";
		case QETH_CARD_TYPE_OSX:
			return " OSX QDIO";
		default:
			return " unknown";
		}
	}
	return " n/a";
}

/* max length to be returned: 14 */
const char *qeth_get_cardname_short(struct qeth_card *card)
{
	if (card->info.guestlan) {
		switch (card->info.type) {
		case QETH_CARD_TYPE_OSD:
			return "GuestLAN QDIO";
		case QETH_CARD_TYPE_IQD:
			return "GuestLAN Hiper";
		case QETH_CARD_TYPE_OSM:
			return "GuestLAN OSM";
		case QETH_CARD_TYPE_OSX:
			return "GuestLAN OSX";
		default:
			return "unknown";
		}
	} else {
		switch (card->info.type) {
		case QETH_CARD_TYPE_OSD:
			switch (card->info.link_type) {
			case QETH_LINK_TYPE_FAST_ETH:
				return "OSD_100";
			case QETH_LINK_TYPE_HSTR:
				return "HSTR";
			case QETH_LINK_TYPE_GBIT_ETH:
				return "OSD_1000";
			case QETH_LINK_TYPE_10GBIT_ETH:
				return "OSD_10GIG";
			case QETH_LINK_TYPE_LANE_ETH100:
				return "OSD_FE_LANE";
			case QETH_LINK_TYPE_LANE_TR:
				return "OSD_TR_LANE";
			case QETH_LINK_TYPE_LANE_ETH1000:
				return "OSD_GbE_LANE";
			case QETH_LINK_TYPE_LANE:
				return "OSD_ATM_LANE";
			default:
				return "OSD_Express";
			}
		case QETH_CARD_TYPE_IQD:
			return "HiperSockets";
		case QETH_CARD_TYPE_OSN:
			return "OSN";
		case QETH_CARD_TYPE_OSM:
			return "OSM_1000";
		case QETH_CARD_TYPE_OSX:
			return "OSX_10GIG";
		default:
			return "unknown";
		}
	}
	return "n/a";
}

void qeth_set_allowed_threads(struct qeth_card *card, unsigned long threads,
			 int clear_start_mask)
{
	unsigned long flags;

	spin_lock_irqsave(&card->thread_mask_lock, flags);
	card->thread_allowed_mask = threads;
	if (clear_start_mask)
		card->thread_start_mask &= threads;
	spin_unlock_irqrestore(&card->thread_mask_lock, flags);
	wake_up(&card->wait_q);
}
EXPORT_SYMBOL_GPL(qeth_set_allowed_threads);

int qeth_threads_running(struct qeth_card *card, unsigned long threads)
{
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&card->thread_mask_lock, flags);
	rc = (card->thread_running_mask & threads);
	spin_unlock_irqrestore(&card->thread_mask_lock, flags);
	return rc;
}
EXPORT_SYMBOL_GPL(qeth_threads_running);

int qeth_wait_for_threads(struct qeth_card *card, unsigned long threads)
{
	return wait_event_interruptible(card->wait_q,
			qeth_threads_running(card, threads) == 0);
}
EXPORT_SYMBOL_GPL(qeth_wait_for_threads);

void qeth_clear_working_pool_list(struct qeth_card *card)
{
	struct qeth_buffer_pool_entry *pool_entry, *tmp;

	QETH_DBF_TEXT(TRACE, 5, "clwrklst");
	list_for_each_entry_safe(pool_entry, tmp,
			    &card->qdio.in_buf_pool.entry_list, list){
			list_del(&pool_entry->list);
	}
}
EXPORT_SYMBOL_GPL(qeth_clear_working_pool_list);

static int qeth_alloc_buffer_pool(struct qeth_card *card)
{
	struct qeth_buffer_pool_entry *pool_entry;
	void *ptr;
	int i, j;

	QETH_DBF_TEXT(TRACE, 5, "alocpool");
	for (i = 0; i < card->qdio.init_pool.buf_count; ++i) {
		pool_entry = kmalloc(sizeof(*pool_entry), GFP_KERNEL);
		if (!pool_entry) {
			qeth_free_buffer_pool(card);
			return -ENOMEM;
		}
		for (j = 0; j < QETH_MAX_BUFFER_ELEMENTS(card); ++j) {
			ptr = (void *) __get_free_page(GFP_KERNEL);
			if (!ptr) {
				while (j > 0)
					free_page((unsigned long)
						  pool_entry->elements[--j]);
				kfree(pool_entry);
				qeth_free_buffer_pool(card);
				return -ENOMEM;
			}
			pool_entry->elements[j] = ptr;
		}
		list_add(&pool_entry->init_list,
			 &card->qdio.init_pool.entry_list);
	}
	return 0;
}

int qeth_realloc_buffer_pool(struct qeth_card *card, int bufcnt)
{
	QETH_DBF_TEXT(TRACE, 2, "realcbp");

	if ((card->state != CARD_STATE_DOWN) &&
	    (card->state != CARD_STATE_RECOVER))
		return -EPERM;

	/* TODO: steel/add buffers from/to a running card's buffer pool (?) */
	qeth_clear_working_pool_list(card);
	qeth_free_buffer_pool(card);
	card->qdio.in_buf_pool.buf_count = bufcnt;
	card->qdio.init_pool.buf_count = bufcnt;
	return qeth_alloc_buffer_pool(card);
}
EXPORT_SYMBOL_GPL(qeth_realloc_buffer_pool);

static int qeth_issue_next_read(struct qeth_card *card)
{
	int rc;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(TRACE, 5, "issnxrd");
	if (card->read.state != CH_STATE_UP)
		return -EIO;
	iob = qeth_get_buffer(&card->read);
	if (!iob) {
		dev_warn(&card->gdev->dev, "The qeth device driver "
			"failed to recover an error on the device\n");
		QETH_DBF_MESSAGE(2, "%s issue_next_read failed: no iob "
			"available\n", dev_name(&card->gdev->dev));
		return -ENOMEM;
	}
	qeth_setup_ccw(&card->read, iob->data, QETH_BUFSIZE);
	QETH_DBF_TEXT(TRACE, 6, "noirqpnd");
	rc = ccw_device_start(card->read.ccwdev, &card->read.ccw,
			      (addr_t) iob, 0, 0);
	if (rc) {
		QETH_DBF_MESSAGE(2, "%s error in starting next read ccw! "
			"rc=%i\n", dev_name(&card->gdev->dev), rc);
		atomic_set(&card->read.irq_pending, 0);
		qeth_schedule_recovery(card);
		wake_up(&card->wait_q);
	}
	return rc;
}

static struct qeth_reply *qeth_alloc_reply(struct qeth_card *card)
{
	struct qeth_reply *reply;

	reply = kzalloc(sizeof(struct qeth_reply), GFP_ATOMIC);
	if (reply) {
		atomic_set(&reply->refcnt, 1);
		atomic_set(&reply->received, 0);
		reply->card = card;
	};
	return reply;
}

static void qeth_get_reply(struct qeth_reply *reply)
{
	WARN_ON(atomic_read(&reply->refcnt) <= 0);
	atomic_inc(&reply->refcnt);
}

static void qeth_put_reply(struct qeth_reply *reply)
{
	WARN_ON(atomic_read(&reply->refcnt) <= 0);
	if (atomic_dec_and_test(&reply->refcnt))
		kfree(reply);
}

static void qeth_issue_ipa_msg(struct qeth_ipa_cmd *cmd, int rc,
		struct qeth_card *card)
{
	char *ipa_name;
	int com = cmd->hdr.command;
	ipa_name = qeth_get_ipa_cmd_name(com);
	if (rc)
		QETH_DBF_MESSAGE(2, "IPA: %s(x%X) for %s returned x%X \"%s\"\n",
				ipa_name, com, QETH_CARD_IFNAME(card),
					rc, qeth_get_ipa_msg(rc));
	else
		QETH_DBF_MESSAGE(5, "IPA: %s(x%X) for %s succeeded\n",
				ipa_name, com, QETH_CARD_IFNAME(card));
}

static struct qeth_ipa_cmd *qeth_check_ipa_data(struct qeth_card *card,
		struct qeth_cmd_buffer *iob)
{
	struct qeth_ipa_cmd *cmd = NULL;

	QETH_DBF_TEXT(TRACE, 5, "chkipad");
	if (IS_IPA(iob->data)) {
		cmd = (struct qeth_ipa_cmd *) PDU_ENCAPSULATION(iob->data);
		if (IS_IPA_REPLY(cmd)) {
			if (cmd->hdr.command != IPA_CMD_SETCCID &&
			    cmd->hdr.command != IPA_CMD_DELCCID &&
			    cmd->hdr.command != IPA_CMD_MODCCID &&
			    cmd->hdr.command != IPA_CMD_SET_DIAG_ASS)
				qeth_issue_ipa_msg(cmd,
						cmd->hdr.return_code, card);
			return cmd;
		} else {
			switch (cmd->hdr.command) {
			case IPA_CMD_STOPLAN:
				dev_warn(&card->gdev->dev,
					   "The link for interface %s on CHPID"
					   " 0x%X failed\n",
					   QETH_CARD_IFNAME(card),
					   card->info.chpid);
				card->lan_online = 0;
				if (card->dev && netif_carrier_ok(card->dev))
					netif_carrier_off(card->dev);
				return NULL;
			case IPA_CMD_STARTLAN:
				dev_info(&card->gdev->dev,
					   "The link for %s on CHPID 0x%X has"
					   " been restored\n",
					   QETH_CARD_IFNAME(card),
					   card->info.chpid);
				netif_carrier_on(card->dev);
				card->lan_online = 1;
				qeth_schedule_recovery(card);
				return NULL;
			case IPA_CMD_MODCCID:
				return cmd;
			case IPA_CMD_REGISTER_LOCAL_ADDR:
				QETH_DBF_TEXT(TRACE, 3, "irla");
				break;
			case IPA_CMD_UNREGISTER_LOCAL_ADDR:
				QETH_DBF_TEXT(TRACE, 3, "urla");
				break;
			default:
				QETH_DBF_MESSAGE(2, "Received data is IPA "
					   "but not a reply!\n");
				break;
			}
		}
	}
	return cmd;
}

void qeth_clear_ipacmd_list(struct qeth_card *card)
{
	struct qeth_reply *reply, *r;
	unsigned long flags;

	QETH_DBF_TEXT(TRACE, 4, "clipalst");

	spin_lock_irqsave(&card->lock, flags);
	list_for_each_entry_safe(reply, r, &card->cmd_waiter_list, list) {
		qeth_get_reply(reply);
		reply->rc = -EIO;
		atomic_inc(&reply->received);
		list_del_init(&reply->list);
		wake_up(&reply->wait_q);
		qeth_put_reply(reply);
	}
	spin_unlock_irqrestore(&card->lock, flags);
}
EXPORT_SYMBOL_GPL(qeth_clear_ipacmd_list);

static int qeth_check_idx_response(struct qeth_card *card,
	unsigned char *buffer)
{
	if (!buffer)
		return 0;

	QETH_DBF_HEX(CTRL, 2, buffer, QETH_DBF_CTRL_LEN);
	if ((buffer[2] & 0xc0) == 0xc0) {
		QETH_DBF_MESSAGE(2, "received an IDX TERMINATE "
			   "with cause code 0x%02x%s\n",
			   buffer[4],
			   ((buffer[4] == 0x22) ?
			    " -- try another portname" : ""));
		QETH_DBF_TEXT(TRACE, 2, "ckidxres");
		QETH_DBF_TEXT(TRACE, 2, " idxterm");
		QETH_DBF_TEXT_(TRACE, 2, "  rc%d", -EIO);
		if (buffer[4] == 0xf6) {
			dev_err(&card->gdev->dev,
			"The qeth device is not configured "
			"for the OSI layer required by z/VM\n");
			return -EPERM;
		}
		return -EIO;
	}
	return 0;
}

static void qeth_setup_ccw(struct qeth_channel *channel, unsigned char *iob,
		__u32 len)
{
	struct qeth_card *card;

	QETH_DBF_TEXT(TRACE, 4, "setupccw");
	card = CARD_FROM_CDEV(channel->ccwdev);
	if (channel == &card->read)
		memcpy(&channel->ccw, READ_CCW, sizeof(struct ccw1));
	else
		memcpy(&channel->ccw, WRITE_CCW, sizeof(struct ccw1));
	channel->ccw.count = len;
	channel->ccw.cda = (__u32) __pa(iob);
}

static struct qeth_cmd_buffer *__qeth_get_buffer(struct qeth_channel *channel)
{
	__u8 index;

	QETH_DBF_TEXT(TRACE, 6, "getbuff");
	index = channel->io_buf_no;
	do {
		if (channel->iob[index].state == BUF_STATE_FREE) {
			channel->iob[index].state = BUF_STATE_LOCKED;
			channel->io_buf_no = (channel->io_buf_no + 1) %
				QETH_CMD_BUFFER_NO;
			memset(channel->iob[index].data, 0, QETH_BUFSIZE);
			return channel->iob + index;
		}
		index = (index + 1) % QETH_CMD_BUFFER_NO;
	} while (index != channel->io_buf_no);

	return NULL;
}

void qeth_release_buffer(struct qeth_channel *channel,
		struct qeth_cmd_buffer *iob)
{
	unsigned long flags;

	QETH_DBF_TEXT(TRACE, 6, "relbuff");
	spin_lock_irqsave(&channel->iob_lock, flags);
	memset(iob->data, 0, QETH_BUFSIZE);
	iob->state = BUF_STATE_FREE;
	iob->callback = qeth_send_control_data_cb;
	iob->rc = 0;
	spin_unlock_irqrestore(&channel->iob_lock, flags);
}
EXPORT_SYMBOL_GPL(qeth_release_buffer);

static struct qeth_cmd_buffer *qeth_get_buffer(struct qeth_channel *channel)
{
	struct qeth_cmd_buffer *buffer = NULL;
	unsigned long flags;

	spin_lock_irqsave(&channel->iob_lock, flags);
	buffer = __qeth_get_buffer(channel);
	spin_unlock_irqrestore(&channel->iob_lock, flags);
	return buffer;
}

struct qeth_cmd_buffer *qeth_wait_for_buffer(struct qeth_channel *channel)
{
	struct qeth_cmd_buffer *buffer;
	wait_event(channel->wait_q,
		   ((buffer = qeth_get_buffer(channel)) != NULL));
	return buffer;
}
EXPORT_SYMBOL_GPL(qeth_wait_for_buffer);

void qeth_clear_cmd_buffers(struct qeth_channel *channel)
{
	int cnt;

	for (cnt = 0; cnt < QETH_CMD_BUFFER_NO; cnt++)
		qeth_release_buffer(channel, &channel->iob[cnt]);
	channel->buf_no = 0;
	channel->io_buf_no = 0;
}
EXPORT_SYMBOL_GPL(qeth_clear_cmd_buffers);

static void qeth_send_control_data_cb(struct qeth_channel *channel,
		  struct qeth_cmd_buffer *iob)
{
	struct qeth_card *card;
	struct qeth_reply *reply, *r;
	struct qeth_ipa_cmd *cmd;
	unsigned long flags;
	int keep_reply;
	int rc = 0;

	QETH_DBF_TEXT(TRACE, 4, "sndctlcb");

	card = CARD_FROM_CDEV(channel->ccwdev);
	rc = qeth_check_idx_response(card, iob->data);
	switch (rc) {
	case 0:
		break;
	case -EIO:
		qeth_clear_ipacmd_list(card);
		qeth_schedule_recovery(card);
	default:
		goto out;
	}

	cmd = qeth_check_ipa_data(card, iob);
	if ((cmd == NULL) && (card->state != CARD_STATE_DOWN))
		goto out;
	/*in case of OSN : check if cmd is set */
	if (card->info.type == QETH_CARD_TYPE_OSN &&
	    cmd &&
	    cmd->hdr.command != IPA_CMD_STARTLAN &&
	    card->osn_info.assist_cb != NULL) {
		card->osn_info.assist_cb(card->dev, cmd);
		goto out;
	}

	spin_lock_irqsave(&card->lock, flags);
	list_for_each_entry_safe(reply, r, &card->cmd_waiter_list, list) {
		if ((reply->seqno == QETH_IDX_COMMAND_SEQNO) ||
		    ((cmd) && (reply->seqno == cmd->hdr.seqno))) {
			qeth_get_reply(reply);
			list_del_init(&reply->list);
			spin_unlock_irqrestore(&card->lock, flags);
			keep_reply = 0;
			if (reply->callback != NULL) {
				if (cmd) {
					reply->offset = (__u16)((char *)cmd -
							(char *)iob->data);
					keep_reply = reply->callback(card,
							reply,
							(unsigned long)cmd);
				} else
					keep_reply = reply->callback(card,
							reply,
							(unsigned long)iob);
			}
			if (cmd)
				reply->rc = (u16) cmd->hdr.return_code;
			else if (iob->rc)
				reply->rc = iob->rc;
			if (keep_reply) {
				spin_lock_irqsave(&card->lock, flags);
				list_add_tail(&reply->list,
					      &card->cmd_waiter_list);
				spin_unlock_irqrestore(&card->lock, flags);
			} else {
				atomic_inc(&reply->received);
				wake_up(&reply->wait_q);
			}
			qeth_put_reply(reply);
			goto out;
		}
	}
	spin_unlock_irqrestore(&card->lock, flags);
out:
	memcpy(&card->seqno.pdu_hdr_ack,
		QETH_PDU_HEADER_SEQ_NO(iob->data),
		QETH_SEQ_NO_LENGTH);
	qeth_release_buffer(channel, iob);
}

static int qeth_setup_channel(struct qeth_channel *channel)
{
	int cnt;

	QETH_DBF_TEXT(SETUP, 2, "setupch");
	for (cnt = 0; cnt < QETH_CMD_BUFFER_NO; cnt++) {
		channel->iob[cnt].data =
			kmalloc(QETH_BUFSIZE, GFP_DMA|GFP_KERNEL);
		if (channel->iob[cnt].data == NULL)
			break;
		channel->iob[cnt].state = BUF_STATE_FREE;
		channel->iob[cnt].channel = channel;
		channel->iob[cnt].callback = qeth_send_control_data_cb;
		channel->iob[cnt].rc = 0;
	}
	if (cnt < QETH_CMD_BUFFER_NO) {
		while (cnt-- > 0)
			kfree(channel->iob[cnt].data);
		return -ENOMEM;
	}
	channel->buf_no = 0;
	channel->io_buf_no = 0;
	atomic_set(&channel->irq_pending, 0);
	spin_lock_init(&channel->iob_lock);

	init_waitqueue_head(&channel->wait_q);
	return 0;
}

static int qeth_set_thread_start_bit(struct qeth_card *card,
		unsigned long thread)
{
	unsigned long flags;

	spin_lock_irqsave(&card->thread_mask_lock, flags);
	if (!(card->thread_allowed_mask & thread) ||
	      (card->thread_start_mask & thread)) {
		spin_unlock_irqrestore(&card->thread_mask_lock, flags);
		return -EPERM;
	}
	card->thread_start_mask |= thread;
	spin_unlock_irqrestore(&card->thread_mask_lock, flags);
	return 0;
}

void qeth_clear_thread_start_bit(struct qeth_card *card, unsigned long thread)
{
	unsigned long flags;

	spin_lock_irqsave(&card->thread_mask_lock, flags);
	card->thread_start_mask &= ~thread;
	spin_unlock_irqrestore(&card->thread_mask_lock, flags);
	wake_up(&card->wait_q);
}
EXPORT_SYMBOL_GPL(qeth_clear_thread_start_bit);

void qeth_clear_thread_running_bit(struct qeth_card *card, unsigned long thread)
{
	unsigned long flags;

	spin_lock_irqsave(&card->thread_mask_lock, flags);
	card->thread_running_mask &= ~thread;
	spin_unlock_irqrestore(&card->thread_mask_lock, flags);
	wake_up(&card->wait_q);
}
EXPORT_SYMBOL_GPL(qeth_clear_thread_running_bit);

static int __qeth_do_run_thread(struct qeth_card *card, unsigned long thread)
{
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&card->thread_mask_lock, flags);
	if (card->thread_start_mask & thread) {
		if ((card->thread_allowed_mask & thread) &&
		    !(card->thread_running_mask & thread)) {
			rc = 1;
			card->thread_start_mask &= ~thread;
			card->thread_running_mask |= thread;
		} else
			rc = -EPERM;
	}
	spin_unlock_irqrestore(&card->thread_mask_lock, flags);
	return rc;
}

int qeth_do_run_thread(struct qeth_card *card, unsigned long thread)
{
	int rc = 0;

	wait_event(card->wait_q,
		   (rc = __qeth_do_run_thread(card, thread)) >= 0);
	return rc;
}
EXPORT_SYMBOL_GPL(qeth_do_run_thread);

void qeth_schedule_recovery(struct qeth_card *card)
{
	QETH_DBF_TEXT(TRACE, 2, "startrec");
	if (qeth_set_thread_start_bit(card, QETH_RECOVER_THREAD) == 0)
		schedule_work(&card->kernel_thread_starter);
}
EXPORT_SYMBOL_GPL(qeth_schedule_recovery);

static int qeth_get_problem(struct ccw_device *cdev, struct irb *irb)
{
	int dstat, cstat;
	char *sense;

	sense = (char *) irb->ecw;
	cstat = irb->scsw.cmd.cstat;
	dstat = irb->scsw.cmd.dstat;

	if (cstat & (SCHN_STAT_CHN_CTRL_CHK | SCHN_STAT_INTF_CTRL_CHK |
		     SCHN_STAT_CHN_DATA_CHK | SCHN_STAT_CHAIN_CHECK |
		     SCHN_STAT_PROT_CHECK | SCHN_STAT_PROG_CHECK)) {
		QETH_DBF_TEXT(TRACE, 2, "CGENCHK");
		dev_warn(&cdev->dev, "The qeth device driver "
			"failed to recover an error on the device\n");
		QETH_DBF_MESSAGE(2, "%s check on device dstat=x%x, cstat=x%x\n",
			dev_name(&cdev->dev), dstat, cstat);
		print_hex_dump(KERN_WARNING, "qeth: irb ", DUMP_PREFIX_OFFSET,
				16, 1, irb, 64, 1);
		return 1;
	}

	if (dstat & DEV_STAT_UNIT_CHECK) {
		if (sense[SENSE_RESETTING_EVENT_BYTE] &
		    SENSE_RESETTING_EVENT_FLAG) {
			QETH_DBF_TEXT(TRACE, 2, "REVIND");
			return 1;
		}
		if (sense[SENSE_COMMAND_REJECT_BYTE] &
		    SENSE_COMMAND_REJECT_FLAG) {
			QETH_DBF_TEXT(TRACE, 2, "CMDREJi");
			return 1;
		}
		if ((sense[2] == 0xaf) && (sense[3] == 0xfe)) {
			QETH_DBF_TEXT(TRACE, 2, "AFFE");
			return 1;
		}
		if ((!sense[0]) && (!sense[1]) && (!sense[2]) && (!sense[3])) {
			QETH_DBF_TEXT(TRACE, 2, "ZEROSEN");
			return 0;
		}
		QETH_DBF_TEXT(TRACE, 2, "DGENCHK");
			return 1;
	}
	return 0;
}

static long __qeth_check_irb_error(struct ccw_device *cdev,
		unsigned long intparm, struct irb *irb)
{
	if (!IS_ERR(irb))
		return 0;

	switch (PTR_ERR(irb)) {
	case -EIO:
		QETH_DBF_MESSAGE(2, "%s i/o-error on device\n",
			dev_name(&cdev->dev));
		QETH_DBF_TEXT(TRACE, 2, "ckirberr");
		QETH_DBF_TEXT_(TRACE, 2, "  rc%d", -EIO);
		break;
	case -ETIMEDOUT:
		dev_warn(&cdev->dev, "A hardware operation timed out"
			" on the device\n");
		QETH_DBF_TEXT(TRACE, 2, "ckirberr");
		QETH_DBF_TEXT_(TRACE, 2, "  rc%d", -ETIMEDOUT);
		if (intparm == QETH_RCD_PARM) {
			struct qeth_card *card = CARD_FROM_CDEV(cdev);

			if (card && (card->data.ccwdev == cdev)) {
				card->data.state = CH_STATE_DOWN;
				wake_up(&card->wait_q);
			}
		}
		break;
	default:
		QETH_DBF_MESSAGE(2, "%s unknown error %ld on device\n",
			dev_name(&cdev->dev), PTR_ERR(irb));
		QETH_DBF_TEXT(TRACE, 2, "ckirberr");
		QETH_DBF_TEXT(TRACE, 2, "  rc???");
	}
	return PTR_ERR(irb);
}

static void qeth_irq(struct ccw_device *cdev, unsigned long intparm,
		struct irb *irb)
{
	int rc;
	int cstat, dstat;
	struct qeth_cmd_buffer *buffer;
	struct qeth_channel *channel;
	struct qeth_card *card;
	struct qeth_cmd_buffer *iob;
	__u8 index;

	QETH_DBF_TEXT(TRACE, 5, "irq");

	if (__qeth_check_irb_error(cdev, intparm, irb))
		return;
	cstat = irb->scsw.cmd.cstat;
	dstat = irb->scsw.cmd.dstat;

	card = CARD_FROM_CDEV(cdev);
	if (!card)
		return;

	if (card->read.ccwdev == cdev) {
		channel = &card->read;
		QETH_DBF_TEXT(TRACE, 5, "read");
	} else if (card->write.ccwdev == cdev) {
		channel = &card->write;
		QETH_DBF_TEXT(TRACE, 5, "write");
	} else {
		channel = &card->data;
		QETH_DBF_TEXT(TRACE, 5, "data");
	}
	atomic_set(&channel->irq_pending, 0);

	if (irb->scsw.cmd.fctl & (SCSW_FCTL_CLEAR_FUNC))
		channel->state = CH_STATE_STOPPED;

	if (irb->scsw.cmd.fctl & (SCSW_FCTL_HALT_FUNC))
		channel->state = CH_STATE_HALTED;

	/*let's wake up immediately on data channel*/
	if ((channel == &card->data) && (intparm != 0) &&
	    (intparm != QETH_RCD_PARM))
		goto out;

	if (intparm == QETH_CLEAR_CHANNEL_PARM) {
		QETH_DBF_TEXT(TRACE, 6, "clrchpar");
		/* we don't have to handle this further */
		intparm = 0;
	}
	if (intparm == QETH_HALT_CHANNEL_PARM) {
		QETH_DBF_TEXT(TRACE, 6, "hltchpar");
		/* we don't have to handle this further */
		intparm = 0;
	}
	if ((dstat & DEV_STAT_UNIT_EXCEP) ||
	    (dstat & DEV_STAT_UNIT_CHECK) ||
	    (cstat)) {
		if (irb->esw.esw0.erw.cons) {
			dev_warn(&channel->ccwdev->dev,
				"The qeth device driver failed to recover "
				"an error on the device\n");
			QETH_DBF_MESSAGE(2, "%s sense data available. cstat "
				"0x%X dstat 0x%X\n",
				dev_name(&channel->ccwdev->dev), cstat, dstat);
			print_hex_dump(KERN_WARNING, "qeth: irb ",
				DUMP_PREFIX_OFFSET, 16, 1, irb, 32, 1);
			print_hex_dump(KERN_WARNING, "qeth: sense data ",
				DUMP_PREFIX_OFFSET, 16, 1, irb->ecw, 32, 1);
		}
		if (intparm == QETH_RCD_PARM) {
			channel->state = CH_STATE_DOWN;
			goto out;
		}
		rc = qeth_get_problem(cdev, irb);
		if (rc) {
			qeth_clear_ipacmd_list(card);
			qeth_schedule_recovery(card);
			goto out;
		}
	}

	if (intparm == QETH_RCD_PARM) {
		channel->state = CH_STATE_RCD_DONE;
		goto out;
	}
	if (intparm) {
		buffer = (struct qeth_cmd_buffer *) __va((addr_t)intparm);
		buffer->state = BUF_STATE_PROCESSED;
	}
	if (channel == &card->data)
		return;
	if (channel == &card->read &&
	    channel->state == CH_STATE_UP)
		qeth_issue_next_read(card);

	iob = channel->iob;
	index = channel->buf_no;
	while (iob[index].state == BUF_STATE_PROCESSED) {
		if (iob[index].callback != NULL)
			iob[index].callback(channel, iob + index);

		index = (index + 1) % QETH_CMD_BUFFER_NO;
	}
	channel->buf_no = index;
out:
	wake_up(&card->wait_q);
	return;
}

static void __qeth_clear_output_buffer(struct qeth_qdio_out_q *queue,
		 struct qeth_qdio_out_buffer *buf, unsigned int qeth_skip_skb)
{
	int i;
	struct sk_buff *skb;

	/* is PCI flag set on buffer? */
	if (buf->buffer->element[0].flags & 0x40)
		atomic_dec(&queue->set_pci_flags_count);

	if (!qeth_skip_skb) {
		skb = skb_dequeue(&buf->skb_list);
		while (skb) {
			atomic_dec(&skb->users);
			dev_kfree_skb_any(skb);
			skb = skb_dequeue(&buf->skb_list);
		}
	}
	for (i = 0; i < QETH_MAX_BUFFER_ELEMENTS(queue->card); ++i) {
		if (buf->buffer->element[i].addr && buf->is_header[i])
			kmem_cache_free(qeth_core_header_cache,
				buf->buffer->element[i].addr);
		buf->is_header[i] = 0;
		buf->buffer->element[i].length = 0;
		buf->buffer->element[i].addr = NULL;
		buf->buffer->element[i].flags = 0;
	}
	buf->buffer->element[15].flags = 0;
	buf->next_element_to_fill = 0;
	atomic_set(&buf->state, QETH_QDIO_BUF_EMPTY);
}

static void qeth_clear_output_buffer(struct qeth_qdio_out_q *queue,
		struct qeth_qdio_out_buffer *buf)
{
	__qeth_clear_output_buffer(queue, buf, 0);
}

void qeth_clear_qdio_buffers(struct qeth_card *card)
{
	int i, j;

	QETH_DBF_TEXT(TRACE, 2, "clearqdbf");
	/* clear outbound buffers to free skbs */
	for (i = 0; i < card->qdio.no_out_queues; ++i)
		if (card->qdio.out_qs[i]) {
			for (j = 0; j < QDIO_MAX_BUFFERS_PER_Q; ++j)
				qeth_clear_output_buffer(card->qdio.out_qs[i],
						&card->qdio.out_qs[i]->bufs[j]);
		}
}
EXPORT_SYMBOL_GPL(qeth_clear_qdio_buffers);

static void qeth_free_buffer_pool(struct qeth_card *card)
{
	struct qeth_buffer_pool_entry *pool_entry, *tmp;
	int i = 0;
	QETH_DBF_TEXT(TRACE, 5, "freepool");
	list_for_each_entry_safe(pool_entry, tmp,
				 &card->qdio.init_pool.entry_list, init_list){
		for (i = 0; i < QETH_MAX_BUFFER_ELEMENTS(card); ++i)
			free_page((unsigned long)pool_entry->elements[i]);
		list_del(&pool_entry->init_list);
		kfree(pool_entry);
	}
}

static void qeth_free_qdio_buffers(struct qeth_card *card)
{
	int i, j;

	QETH_DBF_TEXT(TRACE, 2, "freeqdbf");
	if (atomic_xchg(&card->qdio.state, QETH_QDIO_UNINITIALIZED) ==
		QETH_QDIO_UNINITIALIZED)
		return;
	kfree(card->qdio.in_q);
	card->qdio.in_q = NULL;
	/* inbound buffer pool */
	qeth_free_buffer_pool(card);
	/* free outbound qdio_qs */
	if (card->qdio.out_qs) {
		for (i = 0; i < card->qdio.no_out_queues; ++i) {
			for (j = 0; j < QDIO_MAX_BUFFERS_PER_Q; ++j)
				qeth_clear_output_buffer(card->qdio.out_qs[i],
						&card->qdio.out_qs[i]->bufs[j]);
			kfree(card->qdio.out_qs[i]);
		}
		kfree(card->qdio.out_qs);
		card->qdio.out_qs = NULL;
	}
}

static void qeth_clean_channel(struct qeth_channel *channel)
{
	int cnt;

	QETH_DBF_TEXT(SETUP, 2, "freech");
	for (cnt = 0; cnt < QETH_CMD_BUFFER_NO; cnt++)
		kfree(channel->iob[cnt].data);
}

static void qeth_get_channel_path_desc(struct qeth_card *card)
{
	struct ccw_device *ccwdev;
	struct channelPath_dsc {
		u8 flags;
		u8 lsn;
		u8 desc;
		u8 chpid;
		u8 swla;
		u8 zeroes;
		u8 chla;
		u8 chpp;
	} *chp_dsc;

	QETH_DBF_TEXT(SETUP, 2, "chp_desc");

	ccwdev = card->data.ccwdev;
	chp_dsc = (struct channelPath_dsc *)ccw_device_get_chp_desc(ccwdev, 0);
	if (chp_dsc != NULL) {
		/* CHPP field bit 6 == 1 -> single queue */
		if ((chp_dsc->chpp & 0x02) == 0x02)
			card->qdio.no_out_queues = 1;
		card->info.func_level = 0x4100 + chp_dsc->desc;
		kfree(chp_dsc);
	}
	if (card->qdio.no_out_queues == 1) {
		card->qdio.default_out_queue = 0;
		dev_info(&card->gdev->dev,
			"Priority Queueing not supported\n");
	}
	QETH_DBF_TEXT_(SETUP, 2, "nr:%x", card->qdio.no_out_queues);
	QETH_DBF_TEXT_(SETUP, 2, "lvl:%02x", card->info.func_level);
	return;
}

static void qeth_init_qdio_info(struct qeth_card *card)
{
	QETH_DBF_TEXT(SETUP, 4, "intqdinf");
	atomic_set(&card->qdio.state, QETH_QDIO_UNINITIALIZED);
	/* inbound */
	card->qdio.in_buf_size = QETH_IN_BUF_SIZE_DEFAULT;
	card->qdio.init_pool.buf_count = QETH_IN_BUF_COUNT_DEFAULT;
	card->qdio.in_buf_pool.buf_count = card->qdio.init_pool.buf_count;
	INIT_LIST_HEAD(&card->qdio.in_buf_pool.entry_list);
	INIT_LIST_HEAD(&card->qdio.init_pool.entry_list);
}

static void qeth_set_intial_options(struct qeth_card *card)
{
	card->options.route4.type = NO_ROUTER;
	card->options.route6.type = NO_ROUTER;
	card->options.checksum_type = QETH_CHECKSUM_DEFAULT;
	card->options.broadcast_mode = QETH_TR_BROADCAST_ALLRINGS;
	card->options.macaddr_mode = QETH_TR_MACADDR_NONCANONICAL;
	card->options.fake_broadcast = 0;
	card->options.add_hhlen = DEFAULT_ADD_HHLEN;
	card->options.performance_stats = 0;
	card->options.rx_sg_cb = QETH_RX_SG_CB;
	card->options.isolation = ISOLATION_MODE_NONE;
}

static int qeth_do_start_thread(struct qeth_card *card, unsigned long thread)
{
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&card->thread_mask_lock, flags);
	QETH_DBF_TEXT_(TRACE, 4, "  %02x%02x%02x",
			(u8) card->thread_start_mask,
			(u8) card->thread_allowed_mask,
			(u8) card->thread_running_mask);
	rc = (card->thread_start_mask & thread);
	spin_unlock_irqrestore(&card->thread_mask_lock, flags);
	return rc;
}

static void qeth_start_kernel_thread(struct work_struct *work)
{
	struct qeth_card *card = container_of(work, struct qeth_card,
					kernel_thread_starter);
	QETH_DBF_TEXT(TRACE , 2, "strthrd");

	if (card->read.state != CH_STATE_UP &&
	    card->write.state != CH_STATE_UP)
		return;
	if (qeth_do_start_thread(card, QETH_RECOVER_THREAD))
		kthread_run(card->discipline.recover, (void *) card,
				"qeth_recover");
}

static int qeth_setup_card(struct qeth_card *card)
{

	QETH_DBF_TEXT(SETUP, 2, "setupcrd");
	QETH_DBF_HEX(SETUP, 2, &card, sizeof(void *));

	card->read.state  = CH_STATE_DOWN;
	card->write.state = CH_STATE_DOWN;
	card->data.state  = CH_STATE_DOWN;
	card->state = CARD_STATE_DOWN;
	card->lan_online = 0;
	card->use_hard_stop = 0;
	card->dev = NULL;
	spin_lock_init(&card->vlanlock);
	spin_lock_init(&card->mclock);
	card->vlangrp = NULL;
	spin_lock_init(&card->lock);
	spin_lock_init(&card->ip_lock);
	spin_lock_init(&card->thread_mask_lock);
	mutex_init(&card->conf_mutex);
	card->thread_start_mask = 0;
	card->thread_allowed_mask = 0;
	card->thread_running_mask = 0;
	INIT_WORK(&card->kernel_thread_starter, qeth_start_kernel_thread);
	INIT_LIST_HEAD(&card->ip_list);
	INIT_LIST_HEAD(card->ip_tbd_list);
	INIT_LIST_HEAD(&card->cmd_waiter_list);
	init_waitqueue_head(&card->wait_q);
	/* intial options */
	qeth_set_intial_options(card);
	/* IP address takeover */
	INIT_LIST_HEAD(&card->ipato.entries);
	card->ipato.enabled = 0;
	card->ipato.invert4 = 0;
	card->ipato.invert6 = 0;
	/* init QDIO stuff */
	qeth_init_qdio_info(card);
	return 0;
}

static void qeth_core_sl_print(struct seq_file *m, struct service_level *slr)
{
	struct qeth_card *card = container_of(slr, struct qeth_card,
					qeth_service_level);
	if (card->info.mcl_level[0])
		seq_printf(m, "qeth: %s firmware level %s\n",
			CARD_BUS_ID(card), card->info.mcl_level);
}

static struct qeth_card *qeth_alloc_card(void)
{
	struct qeth_card *card;

	QETH_DBF_TEXT(SETUP, 2, "alloccrd");
	card = kzalloc(sizeof(struct qeth_card), GFP_DMA|GFP_KERNEL);
	if (!card)
		goto out;
	QETH_DBF_HEX(SETUP, 2, &card, sizeof(void *));
	card->ip_tbd_list = kmalloc(sizeof(struct list_head), GFP_KERNEL);
	if (!card->ip_tbd_list) {
		QETH_DBF_TEXT(SETUP, 0, "iptbdnom");
		goto out_card;
	}
	if (qeth_setup_channel(&card->read))
		goto out_ip;
	if (qeth_setup_channel(&card->write))
		goto out_channel;
	card->options.layer2 = -1;
	card->qeth_service_level.seq_print = qeth_core_sl_print;
	register_service_level(&card->qeth_service_level);
	return card;

out_channel:
	qeth_clean_channel(&card->read);
out_ip:
	kfree(card->ip_tbd_list);
out_card:
	kfree(card);
out:
	return NULL;
}

static int qeth_determine_card_type(struct qeth_card *card)
{
	int i = 0;

	QETH_DBF_TEXT(SETUP, 2, "detcdtyp");

	card->qdio.do_prio_queueing = QETH_PRIOQ_DEFAULT;
	card->qdio.default_out_queue = QETH_DEFAULT_QUEUE;
	while (known_devices[i][QETH_DEV_MODEL_IND]) {
		if ((CARD_RDEV(card)->id.dev_type ==
				known_devices[i][QETH_DEV_TYPE_IND]) &&
		    (CARD_RDEV(card)->id.dev_model ==
				known_devices[i][QETH_DEV_MODEL_IND])) {
			card->info.type = known_devices[i][QETH_DEV_MODEL_IND];
			card->qdio.no_out_queues =
				known_devices[i][QETH_QUEUE_NO_IND];
			card->info.is_multicast_different =
				known_devices[i][QETH_MULTICAST_IND];
			qeth_get_channel_path_desc(card);
			return 0;
		}
		i++;
	}
	card->info.type = QETH_CARD_TYPE_UNKNOWN;
	dev_err(&card->gdev->dev, "The adapter hardware is of an "
		"unknown type\n");
	return -ENOENT;
}

static int qeth_clear_channel(struct qeth_channel *channel)
{
	unsigned long flags;
	struct qeth_card *card;
	int rc;

	QETH_DBF_TEXT(TRACE, 3, "clearch");
	card = CARD_FROM_CDEV(channel->ccwdev);
	spin_lock_irqsave(get_ccwdev_lock(channel->ccwdev), flags);
	rc = ccw_device_clear(channel->ccwdev, QETH_CLEAR_CHANNEL_PARM);
	spin_unlock_irqrestore(get_ccwdev_lock(channel->ccwdev), flags);

	if (rc)
		return rc;
	rc = wait_event_interruptible_timeout(card->wait_q,
			channel->state == CH_STATE_STOPPED, QETH_TIMEOUT);
	if (rc == -ERESTARTSYS)
		return rc;
	if (channel->state != CH_STATE_STOPPED)
		return -ETIME;
	channel->state = CH_STATE_DOWN;
	return 0;
}

static int qeth_halt_channel(struct qeth_channel *channel)
{
	unsigned long flags;
	struct qeth_card *card;
	int rc;

	QETH_DBF_TEXT(TRACE, 3, "haltch");
	card = CARD_FROM_CDEV(channel->ccwdev);
	spin_lock_irqsave(get_ccwdev_lock(channel->ccwdev), flags);
	rc = ccw_device_halt(channel->ccwdev, QETH_HALT_CHANNEL_PARM);
	spin_unlock_irqrestore(get_ccwdev_lock(channel->ccwdev), flags);

	if (rc)
		return rc;
	rc = wait_event_interruptible_timeout(card->wait_q,
			channel->state == CH_STATE_HALTED, QETH_TIMEOUT);
	if (rc == -ERESTARTSYS)
		return rc;
	if (channel->state != CH_STATE_HALTED)
		return -ETIME;
	return 0;
}

static int qeth_halt_channels(struct qeth_card *card)
{
	int rc1 = 0, rc2 = 0, rc3 = 0;

	QETH_DBF_TEXT(TRACE, 3, "haltchs");
	rc1 = qeth_halt_channel(&card->read);
	rc2 = qeth_halt_channel(&card->write);
	rc3 = qeth_halt_channel(&card->data);
	if (rc1)
		return rc1;
	if (rc2)
		return rc2;
	return rc3;
}

static int qeth_clear_channels(struct qeth_card *card)
{
	int rc1 = 0, rc2 = 0, rc3 = 0;

	QETH_DBF_TEXT(TRACE, 3, "clearchs");
	rc1 = qeth_clear_channel(&card->read);
	rc2 = qeth_clear_channel(&card->write);
	rc3 = qeth_clear_channel(&card->data);
	if (rc1)
		return rc1;
	if (rc2)
		return rc2;
	return rc3;
}

static int qeth_clear_halt_card(struct qeth_card *card, int halt)
{
	int rc = 0;

	QETH_DBF_TEXT(TRACE, 3, "clhacrd");
	QETH_DBF_HEX(TRACE, 3, &card, sizeof(void *));

	if (halt)
		rc = qeth_halt_channels(card);
	if (rc)
		return rc;
	return qeth_clear_channels(card);
}

int qeth_qdio_clear_card(struct qeth_card *card, int use_halt)
{
	int rc = 0;

	QETH_DBF_TEXT(TRACE, 3, "qdioclr");
	switch (atomic_cmpxchg(&card->qdio.state, QETH_QDIO_ESTABLISHED,
		QETH_QDIO_CLEANING)) {
	case QETH_QDIO_ESTABLISHED:
		if (card->info.type == QETH_CARD_TYPE_IQD)
			rc = qdio_shutdown(CARD_DDEV(card),
				QDIO_FLAG_CLEANUP_USING_HALT);
		else
			rc = qdio_shutdown(CARD_DDEV(card),
				QDIO_FLAG_CLEANUP_USING_CLEAR);
		if (rc)
			QETH_DBF_TEXT_(TRACE, 3, "1err%d", rc);
		qdio_free(CARD_DDEV(card));
		atomic_set(&card->qdio.state, QETH_QDIO_ALLOCATED);
		break;
	case QETH_QDIO_CLEANING:
		return rc;
	default:
		break;
	}
	rc = qeth_clear_halt_card(card, use_halt);
	if (rc)
		QETH_DBF_TEXT_(TRACE, 3, "2err%d", rc);
	card->state = CARD_STATE_DOWN;
	return rc;
}
EXPORT_SYMBOL_GPL(qeth_qdio_clear_card);

static int qeth_read_conf_data(struct qeth_card *card, void **buffer,
			       int *length)
{
	struct ciw *ciw;
	char *rcd_buf;
	int ret;
	struct qeth_channel *channel = &card->data;
	unsigned long flags;

	/*
	 * scan for RCD command in extended SenseID data
	 */
	ciw = ccw_device_get_ciw(channel->ccwdev, CIW_TYPE_RCD);
	if (!ciw || ciw->cmd == 0)
		return -EOPNOTSUPP;
	rcd_buf = kzalloc(ciw->count, GFP_KERNEL | GFP_DMA);
	if (!rcd_buf)
		return -ENOMEM;

	channel->ccw.cmd_code = ciw->cmd;
	channel->ccw.cda = (__u32) __pa(rcd_buf);
	channel->ccw.count = ciw->count;
	channel->ccw.flags = CCW_FLAG_SLI;
	channel->state = CH_STATE_RCD;
	spin_lock_irqsave(get_ccwdev_lock(channel->ccwdev), flags);
	ret = ccw_device_start_timeout(channel->ccwdev, &channel->ccw,
				       QETH_RCD_PARM, LPM_ANYPATH, 0,
				       QETH_RCD_TIMEOUT);
	spin_unlock_irqrestore(get_ccwdev_lock(channel->ccwdev), flags);
	if (!ret)
		wait_event(card->wait_q,
			   (channel->state == CH_STATE_RCD_DONE ||
			    channel->state == CH_STATE_DOWN));
	if (channel->state == CH_STATE_DOWN)
		ret = -EIO;
	else
		channel->state = CH_STATE_DOWN;
	if (ret) {
		kfree(rcd_buf);
		*buffer = NULL;
		*length = 0;
	} else {
		*length = ciw->count;
		*buffer = rcd_buf;
	}
	return ret;
}

static void qeth_configure_unitaddr(struct qeth_card *card, char *prcd)
{
	QETH_DBF_TEXT(SETUP, 2, "cfgunit");
	card->info.chpid = prcd[30];
	card->info.unit_addr2 = prcd[31];
	card->info.cula = prcd[63];
	card->info.guestlan = ((prcd[0x10] == _ascebc['V']) &&
			       (prcd[0x11] == _ascebc['M']));
}

static void qeth_configure_blkt_default(struct qeth_card *card, char *prcd)
{
	QETH_DBF_TEXT(SETUP, 2, "cfgblkt");

	if (prcd[74] == 0xF0 && prcd[75] == 0xF0 && prcd[76] == 0xF5) {
		card->info.blkt.time_total = 250;
		card->info.blkt.inter_packet = 5;
		card->info.blkt.inter_packet_jumbo = 15;
	} else {
		card->info.blkt.time_total = 0;
		card->info.blkt.inter_packet = 0;
		card->info.blkt.inter_packet_jumbo = 0;
	}
}

static void qeth_init_tokens(struct qeth_card *card)
{
	card->token.issuer_rm_w = 0x00010103UL;
	card->token.cm_filter_w = 0x00010108UL;
	card->token.cm_connection_w = 0x0001010aUL;
	card->token.ulp_filter_w = 0x0001010bUL;
	card->token.ulp_connection_w = 0x0001010dUL;
}

static void qeth_init_func_level(struct qeth_card *card)
{
	switch (card->info.type) {
	case QETH_CARD_TYPE_IQD:
		if (card->ipato.enabled)
			card->info.func_level =
				QETH_IDX_FUNC_LEVEL_IQD_ENA_IPAT;
		else
			card->info.func_level =
				QETH_IDX_FUNC_LEVEL_IQD_DIS_IPAT;
		break;
	case QETH_CARD_TYPE_OSD:
		card->info.func_level = QETH_IDX_FUNC_LEVEL_OSD;
		break;
	default:
		break;
	}
}

static int qeth_idx_activate_get_answer(struct qeth_channel *channel,
		void (*idx_reply_cb)(struct qeth_channel *,
			struct qeth_cmd_buffer *))
{
	struct qeth_cmd_buffer *iob;
	unsigned long flags;
	int rc;
	struct qeth_card *card;

	QETH_DBF_TEXT(SETUP, 2, "idxanswr");
	card = CARD_FROM_CDEV(channel->ccwdev);
	iob = qeth_get_buffer(channel);
	iob->callback = idx_reply_cb;
	memcpy(&channel->ccw, READ_CCW, sizeof(struct ccw1));
	channel->ccw.count = QETH_BUFSIZE;
	channel->ccw.cda = (__u32) __pa(iob->data);

	wait_event(card->wait_q,
		   atomic_cmpxchg(&channel->irq_pending, 0, 1) == 0);
	QETH_DBF_TEXT(SETUP, 6, "noirqpnd");
	spin_lock_irqsave(get_ccwdev_lock(channel->ccwdev), flags);
	rc = ccw_device_start(channel->ccwdev,
			      &channel->ccw, (addr_t) iob, 0, 0);
	spin_unlock_irqrestore(get_ccwdev_lock(channel->ccwdev), flags);

	if (rc) {
		QETH_DBF_MESSAGE(2, "Error2 in activating channel rc=%d\n", rc);
		QETH_DBF_TEXT_(SETUP, 2, "2err%d", rc);
		atomic_set(&channel->irq_pending, 0);
		wake_up(&card->wait_q);
		return rc;
	}
	rc = wait_event_interruptible_timeout(card->wait_q,
			 channel->state == CH_STATE_UP, QETH_TIMEOUT);
	if (rc == -ERESTARTSYS)
		return rc;
	if (channel->state != CH_STATE_UP) {
		rc = -ETIME;
		QETH_DBF_TEXT_(SETUP, 2, "3err%d", rc);
		qeth_clear_cmd_buffers(channel);
	} else
		rc = 0;
	return rc;
}

static int qeth_idx_activate_channel(struct qeth_channel *channel,
		void (*idx_reply_cb)(struct qeth_channel *,
			struct qeth_cmd_buffer *))
{
	struct qeth_card *card;
	struct qeth_cmd_buffer *iob;
	unsigned long flags;
	__u16 temp;
	__u8 tmp;
	int rc;
	struct ccw_dev_id temp_devid;

	card = CARD_FROM_CDEV(channel->ccwdev);

	QETH_DBF_TEXT(SETUP, 2, "idxactch");

	iob = qeth_get_buffer(channel);
	iob->callback = idx_reply_cb;
	memcpy(&channel->ccw, WRITE_CCW, sizeof(struct ccw1));
	channel->ccw.count = IDX_ACTIVATE_SIZE;
	channel->ccw.cda = (__u32) __pa(iob->data);
	if (channel == &card->write) {
		memcpy(iob->data, IDX_ACTIVATE_WRITE, IDX_ACTIVATE_SIZE);
		memcpy(QETH_TRANSPORT_HEADER_SEQ_NO(iob->data),
		       &card->seqno.trans_hdr, QETH_SEQ_NO_LENGTH);
		card->seqno.trans_hdr++;
	} else {
		memcpy(iob->data, IDX_ACTIVATE_READ, IDX_ACTIVATE_SIZE);
		memcpy(QETH_TRANSPORT_HEADER_SEQ_NO(iob->data),
		       &card->seqno.trans_hdr, QETH_SEQ_NO_LENGTH);
	}
	tmp = ((__u8)card->info.portno) | 0x80;
	memcpy(QETH_IDX_ACT_PNO(iob->data), &tmp, 1);
	memcpy(QETH_IDX_ACT_ISSUER_RM_TOKEN(iob->data),
	       &card->token.issuer_rm_w, QETH_MPC_TOKEN_LENGTH);
	memcpy(QETH_IDX_ACT_FUNC_LEVEL(iob->data),
	       &card->info.func_level, sizeof(__u16));
	ccw_device_get_id(CARD_DDEV(card), &temp_devid);
	memcpy(QETH_IDX_ACT_QDIO_DEV_CUA(iob->data), &temp_devid.devno, 2);
	temp = (card->info.cula << 8) + card->info.unit_addr2;
	memcpy(QETH_IDX_ACT_QDIO_DEV_REALADDR(iob->data), &temp, 2);

	wait_event(card->wait_q,
		   atomic_cmpxchg(&channel->irq_pending, 0, 1) == 0);
	QETH_DBF_TEXT(SETUP, 6, "noirqpnd");
	spin_lock_irqsave(get_ccwdev_lock(channel->ccwdev), flags);
	rc = ccw_device_start(channel->ccwdev,
			      &channel->ccw, (addr_t) iob, 0, 0);
	spin_unlock_irqrestore(get_ccwdev_lock(channel->ccwdev), flags);

	if (rc) {
		QETH_DBF_MESSAGE(2, "Error1 in activating channel. rc=%d\n",
			rc);
		QETH_DBF_TEXT_(SETUP, 2, "1err%d", rc);
		atomic_set(&channel->irq_pending, 0);
		wake_up(&card->wait_q);
		return rc;
	}
	rc = wait_event_interruptible_timeout(card->wait_q,
			channel->state == CH_STATE_ACTIVATING, QETH_TIMEOUT);
	if (rc == -ERESTARTSYS)
		return rc;
	if (channel->state != CH_STATE_ACTIVATING) {
		dev_warn(&channel->ccwdev->dev, "The qeth device driver"
			" failed to recover an error on the device\n");
		QETH_DBF_MESSAGE(2, "%s IDX activate timed out\n",
			dev_name(&channel->ccwdev->dev));
		QETH_DBF_TEXT_(SETUP, 2, "2err%d", -ETIME);
		qeth_clear_cmd_buffers(channel);
		return -ETIME;
	}
	return qeth_idx_activate_get_answer(channel, idx_reply_cb);
}

static int qeth_peer_func_level(int level)
{
	if ((level & 0xff) == 8)
		return (level & 0xff) + 0x400;
	if (((level >> 8) & 3) == 1)
		return (level & 0xff) + 0x200;
	return level;
}

static void qeth_idx_write_cb(struct qeth_channel *channel,
		struct qeth_cmd_buffer *iob)
{
	struct qeth_card *card;
	__u16 temp;

	QETH_DBF_TEXT(SETUP , 2, "idxwrcb");

	if (channel->state == CH_STATE_DOWN) {
		channel->state = CH_STATE_ACTIVATING;
		goto out;
	}
	card = CARD_FROM_CDEV(channel->ccwdev);

	if (!(QETH_IS_IDX_ACT_POS_REPLY(iob->data))) {
		if (QETH_IDX_ACT_CAUSE_CODE(iob->data) == QETH_IDX_ACT_ERR_EXCL)
			dev_err(&card->write.ccwdev->dev,
				"The adapter is used exclusively by another "
				"host\n");
		else
			QETH_DBF_MESSAGE(2, "%s IDX_ACTIVATE on write channel:"
				" negative reply\n",
				dev_name(&card->write.ccwdev->dev));
		goto out;
	}
	memcpy(&temp, QETH_IDX_ACT_FUNC_LEVEL(iob->data), 2);
	if ((temp & ~0x0100) != qeth_peer_func_level(card->info.func_level)) {
		QETH_DBF_MESSAGE(2, "%s IDX_ACTIVATE on write channel: "
			"function level mismatch (sent: 0x%x, received: "
			"0x%x)\n", dev_name(&card->write.ccwdev->dev),
			card->info.func_level, temp);
		goto out;
	}
	channel->state = CH_STATE_UP;
out:
	qeth_release_buffer(channel, iob);
}

static void qeth_idx_read_cb(struct qeth_channel *channel,
		struct qeth_cmd_buffer *iob)
{
	struct qeth_card *card;
	__u16 temp;

	QETH_DBF_TEXT(SETUP , 2, "idxrdcb");
	if (channel->state == CH_STATE_DOWN) {
		channel->state = CH_STATE_ACTIVATING;
		goto out;
	}

	card = CARD_FROM_CDEV(channel->ccwdev);
	if (qeth_check_idx_response(card, iob->data))
			goto out;

	if (!(QETH_IS_IDX_ACT_POS_REPLY(iob->data))) {
		switch (QETH_IDX_ACT_CAUSE_CODE(iob->data)) {
		case QETH_IDX_ACT_ERR_EXCL:
			dev_err(&card->write.ccwdev->dev,
				"The adapter is used exclusively by another "
				"host\n");
			break;
		case QETH_IDX_ACT_ERR_AUTH:
			dev_err(&card->read.ccwdev->dev,
				"Setting the device online failed because of "
				"insufficient LPAR authorization\n");
			break;
		default:
			QETH_DBF_MESSAGE(2, "%s IDX_ACTIVATE on read channel:"
				" negative reply\n",
				dev_name(&card->read.ccwdev->dev));
		}
		goto out;
	}

/**
 *  * temporary fix for microcode bug
 *   * to revert it,replace OR by AND
 *    */
	if ((!QETH_IDX_NO_PORTNAME_REQUIRED(iob->data)) ||
	     (card->info.type == QETH_CARD_TYPE_OSD))
		card->info.portname_required = 1;

	memcpy(&temp, QETH_IDX_ACT_FUNC_LEVEL(iob->data), 2);
	if (temp != qeth_peer_func_level(card->info.func_level)) {
		QETH_DBF_MESSAGE(2, "%s IDX_ACTIVATE on read channel: function "
			"level mismatch (sent: 0x%x, received: 0x%x)\n",
			dev_name(&card->read.ccwdev->dev),
			card->info.func_level, temp);
		goto out;
	}
	memcpy(&card->token.issuer_rm_r,
	       QETH_IDX_ACT_ISSUER_RM_TOKEN(iob->data),
	       QETH_MPC_TOKEN_LENGTH);
	memcpy(&card->info.mcl_level[0],
	       QETH_IDX_REPLY_LEVEL(iob->data), QETH_MCL_LENGTH);
	channel->state = CH_STATE_UP;
out:
	qeth_release_buffer(channel, iob);
}

void qeth_prepare_control_data(struct qeth_card *card, int len,
		struct qeth_cmd_buffer *iob)
{
	qeth_setup_ccw(&card->write, iob->data, len);
	iob->callback = qeth_release_buffer;

	memcpy(QETH_TRANSPORT_HEADER_SEQ_NO(iob->data),
	       &card->seqno.trans_hdr, QETH_SEQ_NO_LENGTH);
	card->seqno.trans_hdr++;
	memcpy(QETH_PDU_HEADER_SEQ_NO(iob->data),
	       &card->seqno.pdu_hdr, QETH_SEQ_NO_LENGTH);
	card->seqno.pdu_hdr++;
	memcpy(QETH_PDU_HEADER_ACK_SEQ_NO(iob->data),
	       &card->seqno.pdu_hdr_ack, QETH_SEQ_NO_LENGTH);
	QETH_DBF_HEX(CTRL, 2, iob->data, QETH_DBF_CTRL_LEN);
}
EXPORT_SYMBOL_GPL(qeth_prepare_control_data);

int qeth_send_control_data(struct qeth_card *card, int len,
		struct qeth_cmd_buffer *iob,
		int (*reply_cb)(struct qeth_card *, struct qeth_reply *,
			unsigned long),
		void *reply_param)
{
	int rc;
	unsigned long flags;
	struct qeth_reply *reply = NULL;
	unsigned long timeout, event_timeout;
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(TRACE, 2, "sendctl");

	reply = qeth_alloc_reply(card);
	if (!reply) {
		return -ENOMEM;
	}
	reply->callback = reply_cb;
	reply->param = reply_param;
	if (card->state == CARD_STATE_DOWN)
		reply->seqno = QETH_IDX_COMMAND_SEQNO;
	else
		reply->seqno = card->seqno.ipa++;
	init_waitqueue_head(&reply->wait_q);
	spin_lock_irqsave(&card->lock, flags);
	list_add_tail(&reply->list, &card->cmd_waiter_list);
	spin_unlock_irqrestore(&card->lock, flags);
	QETH_DBF_HEX(CTRL, 2, iob->data, QETH_DBF_CTRL_LEN);

	while (atomic_cmpxchg(&card->write.irq_pending, 0, 1)) ;
	qeth_prepare_control_data(card, len, iob);

	if (IS_IPA(iob->data))
		event_timeout = QETH_IPA_TIMEOUT;
	else
		event_timeout = QETH_TIMEOUT;
	timeout = jiffies + event_timeout;

	QETH_DBF_TEXT(TRACE, 6, "noirqpnd");
	spin_lock_irqsave(get_ccwdev_lock(card->write.ccwdev), flags);
	rc = ccw_device_start(card->write.ccwdev, &card->write.ccw,
			      (addr_t) iob, 0, 0);
	spin_unlock_irqrestore(get_ccwdev_lock(card->write.ccwdev), flags);
	if (rc) {
		QETH_DBF_MESSAGE(2, "%s qeth_send_control_data: "
			"ccw_device_start rc = %i\n",
			dev_name(&card->write.ccwdev->dev), rc);
		QETH_DBF_TEXT_(TRACE, 2, " err%d", rc);
		spin_lock_irqsave(&card->lock, flags);
		list_del_init(&reply->list);
		qeth_put_reply(reply);
		spin_unlock_irqrestore(&card->lock, flags);
		qeth_release_buffer(iob->channel, iob);
		atomic_set(&card->write.irq_pending, 0);
		wake_up(&card->wait_q);
		return rc;
	}

	/* we have only one long running ipassist, since we can ensure
	   process context of this command we can sleep */
	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
	if ((cmd->hdr.command == IPA_CMD_SETIP) &&
	    (cmd->hdr.prot_version == QETH_PROT_IPV4)) {
		if (!wait_event_timeout(reply->wait_q,
		    atomic_read(&reply->received), event_timeout))
			goto time_err;
	} else {
		while (!atomic_read(&reply->received)) {
			if (time_after(jiffies, timeout))
				goto time_err;
			cpu_relax();
		};
	}

	rc = reply->rc;
	qeth_put_reply(reply);
	return rc;

time_err:
	spin_lock_irqsave(&reply->card->lock, flags);
	list_del_init(&reply->list);
	spin_unlock_irqrestore(&reply->card->lock, flags);
	reply->rc = -ETIME;
	atomic_inc(&reply->received);
	wake_up(&reply->wait_q);
	rc = reply->rc;
	qeth_put_reply(reply);
	return rc;
}
EXPORT_SYMBOL_GPL(qeth_send_control_data);

static int qeth_cm_enable_cb(struct qeth_card *card, struct qeth_reply *reply,
		unsigned long data)
{
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(SETUP, 2, "cmenblcb");

	iob = (struct qeth_cmd_buffer *) data;
	memcpy(&card->token.cm_filter_r,
	       QETH_CM_ENABLE_RESP_FILTER_TOKEN(iob->data),
	       QETH_MPC_TOKEN_LENGTH);
	QETH_DBF_TEXT_(SETUP, 2, "  rc%d", iob->rc);
	return 0;
}

static int qeth_cm_enable(struct qeth_card *card)
{
	int rc;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(SETUP, 2, "cmenable");

	iob = qeth_wait_for_buffer(&card->write);
	memcpy(iob->data, CM_ENABLE, CM_ENABLE_SIZE);
	memcpy(QETH_CM_ENABLE_ISSUER_RM_TOKEN(iob->data),
	       &card->token.issuer_rm_r, QETH_MPC_TOKEN_LENGTH);
	memcpy(QETH_CM_ENABLE_FILTER_TOKEN(iob->data),
	       &card->token.cm_filter_w, QETH_MPC_TOKEN_LENGTH);

	rc = qeth_send_control_data(card, CM_ENABLE_SIZE, iob,
				    qeth_cm_enable_cb, NULL);
	return rc;
}

static int qeth_cm_setup_cb(struct qeth_card *card, struct qeth_reply *reply,
		unsigned long data)
{

	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(SETUP, 2, "cmsetpcb");

	iob = (struct qeth_cmd_buffer *) data;
	memcpy(&card->token.cm_connection_r,
	       QETH_CM_SETUP_RESP_DEST_ADDR(iob->data),
	       QETH_MPC_TOKEN_LENGTH);
	QETH_DBF_TEXT_(SETUP, 2, "  rc%d", iob->rc);
	return 0;
}

static int qeth_cm_setup(struct qeth_card *card)
{
	int rc;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(SETUP, 2, "cmsetup");

	iob = qeth_wait_for_buffer(&card->write);
	memcpy(iob->data, CM_SETUP, CM_SETUP_SIZE);
	memcpy(QETH_CM_SETUP_DEST_ADDR(iob->data),
	       &card->token.issuer_rm_r, QETH_MPC_TOKEN_LENGTH);
	memcpy(QETH_CM_SETUP_CONNECTION_TOKEN(iob->data),
	       &card->token.cm_connection_w, QETH_MPC_TOKEN_LENGTH);
	memcpy(QETH_CM_SETUP_FILTER_TOKEN(iob->data),
	       &card->token.cm_filter_r, QETH_MPC_TOKEN_LENGTH);
	rc = qeth_send_control_data(card, CM_SETUP_SIZE, iob,
				    qeth_cm_setup_cb, NULL);
	return rc;

}

static inline int qeth_get_initial_mtu_for_card(struct qeth_card *card)
{
	switch (card->info.type) {
	case QETH_CARD_TYPE_UNKNOWN:
		return 1500;
	case QETH_CARD_TYPE_IQD:
		return card->info.max_mtu;
	case QETH_CARD_TYPE_OSD:
		switch (card->info.link_type) {
		case QETH_LINK_TYPE_HSTR:
		case QETH_LINK_TYPE_LANE_TR:
			return 2000;
		default:
			return 1492;
		}
	case QETH_CARD_TYPE_OSM:
	case QETH_CARD_TYPE_OSX:
		return 1492;
	default:
		return 1500;
	}
}

static inline int qeth_get_max_mtu_for_card(int cardtype)
{
	switch (cardtype) {

	case QETH_CARD_TYPE_UNKNOWN:
	case QETH_CARD_TYPE_OSD:
	case QETH_CARD_TYPE_OSN:
	case QETH_CARD_TYPE_OSM:
	case QETH_CARD_TYPE_OSX:
		return 61440;
	case QETH_CARD_TYPE_IQD:
		return 57344;
	default:
		return 1500;
	}
}

static inline int qeth_get_mtu_out_of_mpc(int cardtype)
{
	switch (cardtype) {
	case QETH_CARD_TYPE_IQD:
		return 1;
	default:
		return 0;
	}
}

static inline int qeth_get_mtu_outof_framesize(int framesize)
{
	switch (framesize) {
	case 0x4000:
		return 8192;
	case 0x6000:
		return 16384;
	case 0xa000:
		return 32768;
	case 0xffff:
		return 57344;
	default:
		return 0;
	}
}

static inline int qeth_mtu_is_valid(struct qeth_card *card, int mtu)
{
	switch (card->info.type) {
	case QETH_CARD_TYPE_OSD:
	case QETH_CARD_TYPE_OSM:
	case QETH_CARD_TYPE_OSX:
		return ((mtu >= 576) && (mtu <= 61440));
	case QETH_CARD_TYPE_IQD:
		return ((mtu >= 576) &&
			(mtu <= card->info.max_mtu + 4096 - 32));
	case QETH_CARD_TYPE_OSN:
	case QETH_CARD_TYPE_UNKNOWN:
	default:
		return 1;
	}
}

static int qeth_ulp_enable_cb(struct qeth_card *card, struct qeth_reply *reply,
		unsigned long data)
{

	__u16 mtu, framesize;
	__u16 len;
	__u8 link_type;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(SETUP, 2, "ulpenacb");

	iob = (struct qeth_cmd_buffer *) data;
	memcpy(&card->token.ulp_filter_r,
	       QETH_ULP_ENABLE_RESP_FILTER_TOKEN(iob->data),
	       QETH_MPC_TOKEN_LENGTH);
	if (qeth_get_mtu_out_of_mpc(card->info.type)) {
		memcpy(&framesize, QETH_ULP_ENABLE_RESP_MAX_MTU(iob->data), 2);
		mtu = qeth_get_mtu_outof_framesize(framesize);
		if (!mtu) {
			iob->rc = -EINVAL;
			QETH_DBF_TEXT_(SETUP, 2, "  rc%d", iob->rc);
			return 0;
		}
		card->info.max_mtu = mtu;
		card->info.initial_mtu = mtu;
		card->qdio.in_buf_size = mtu + 2 * PAGE_SIZE;
	} else {
		card->info.initial_mtu = qeth_get_initial_mtu_for_card(card);
		card->info.max_mtu = qeth_get_max_mtu_for_card(card->info.type);
		card->qdio.in_buf_size = QETH_IN_BUF_SIZE_DEFAULT;
	}

	memcpy(&len, QETH_ULP_ENABLE_RESP_DIFINFO_LEN(iob->data), 2);
	if (len >= QETH_MPC_DIFINFO_LEN_INDICATES_LINK_TYPE) {
		memcpy(&link_type,
		       QETH_ULP_ENABLE_RESP_LINK_TYPE(iob->data), 1);
		card->info.link_type = link_type;
	} else
		card->info.link_type = 0;
	QETH_DBF_TEXT_(SETUP, 2, "link%d", link_type);
	QETH_DBF_TEXT_(SETUP, 2, "  rc%d", iob->rc);
	return 0;
}

static int qeth_ulp_enable(struct qeth_card *card)
{
	int rc;
	char prot_type;
	struct qeth_cmd_buffer *iob;

	/*FIXME: trace view callbacks*/
	QETH_DBF_TEXT(SETUP, 2, "ulpenabl");

	iob = qeth_wait_for_buffer(&card->write);
	memcpy(iob->data, ULP_ENABLE, ULP_ENABLE_SIZE);

	*(QETH_ULP_ENABLE_LINKNUM(iob->data)) =
		(__u8) card->info.portno;
	if (card->options.layer2)
		if (card->info.type == QETH_CARD_TYPE_OSN)
			prot_type = QETH_PROT_OSN2;
		else
			prot_type = QETH_PROT_LAYER2;
	else
		prot_type = QETH_PROT_TCPIP;

	memcpy(QETH_ULP_ENABLE_PROT_TYPE(iob->data), &prot_type, 1);
	memcpy(QETH_ULP_ENABLE_DEST_ADDR(iob->data),
	       &card->token.cm_connection_r, QETH_MPC_TOKEN_LENGTH);
	memcpy(QETH_ULP_ENABLE_FILTER_TOKEN(iob->data),
	       &card->token.ulp_filter_w, QETH_MPC_TOKEN_LENGTH);
	memcpy(QETH_ULP_ENABLE_PORTNAME_AND_LL(iob->data),
	       card->info.portname, 9);
	rc = qeth_send_control_data(card, ULP_ENABLE_SIZE, iob,
				    qeth_ulp_enable_cb, NULL);
	return rc;

}

static int qeth_ulp_setup_cb(struct qeth_card *card, struct qeth_reply *reply,
		unsigned long data)
{
	struct qeth_cmd_buffer *iob;
	int rc = 0;

	QETH_DBF_TEXT(SETUP, 2, "ulpstpcb");

	iob = (struct qeth_cmd_buffer *) data;
	memcpy(&card->token.ulp_connection_r,
	       QETH_ULP_SETUP_RESP_CONNECTION_TOKEN(iob->data),
	       QETH_MPC_TOKEN_LENGTH);
	if (!strncmp("00S", QETH_ULP_SETUP_RESP_CONNECTION_TOKEN(iob->data),
		     3)) {
		QETH_DBF_TEXT(SETUP, 2, "olmlimit");
		dev_err(&card->gdev->dev, "A connection could not be "
			"established because of an OLM limit\n");
		rc = -EMLINK;
	}
	QETH_DBF_TEXT_(SETUP, 2, "  rc%d", iob->rc);
	return rc;
}

static int qeth_ulp_setup(struct qeth_card *card)
{
	int rc;
	__u16 temp;
	struct qeth_cmd_buffer *iob;
	struct ccw_dev_id dev_id;

	QETH_DBF_TEXT(SETUP, 2, "ulpsetup");

	iob = qeth_wait_for_buffer(&card->write);
	memcpy(iob->data, ULP_SETUP, ULP_SETUP_SIZE);

	memcpy(QETH_ULP_SETUP_DEST_ADDR(iob->data),
	       &card->token.cm_connection_r, QETH_MPC_TOKEN_LENGTH);
	memcpy(QETH_ULP_SETUP_CONNECTION_TOKEN(iob->data),
	       &card->token.ulp_connection_w, QETH_MPC_TOKEN_LENGTH);
	memcpy(QETH_ULP_SETUP_FILTER_TOKEN(iob->data),
	       &card->token.ulp_filter_r, QETH_MPC_TOKEN_LENGTH);

	ccw_device_get_id(CARD_DDEV(card), &dev_id);
	memcpy(QETH_ULP_SETUP_CUA(iob->data), &dev_id.devno, 2);
	temp = (card->info.cula << 8) + card->info.unit_addr2;
	memcpy(QETH_ULP_SETUP_REAL_DEVADDR(iob->data), &temp, 2);
	rc = qeth_send_control_data(card, ULP_SETUP_SIZE, iob,
				    qeth_ulp_setup_cb, NULL);
	return rc;
}

static int qeth_alloc_qdio_buffers(struct qeth_card *card)
{
	int i, j;

	QETH_DBF_TEXT(SETUP, 2, "allcqdbf");

	if (atomic_cmpxchg(&card->qdio.state, QETH_QDIO_UNINITIALIZED,
		QETH_QDIO_ALLOCATED) != QETH_QDIO_UNINITIALIZED)
		return 0;

	card->qdio.in_q = kmalloc(sizeof(struct qeth_qdio_q),
				  GFP_KERNEL);
	if (!card->qdio.in_q)
		goto out_nomem;
	QETH_DBF_TEXT(SETUP, 2, "inq");
	QETH_DBF_HEX(SETUP, 2, &card->qdio.in_q, sizeof(void *));
	memset(card->qdio.in_q, 0, sizeof(struct qeth_qdio_q));
	/* give inbound qeth_qdio_buffers their qdio_buffers */
	for (i = 0; i < QDIO_MAX_BUFFERS_PER_Q; ++i)
		card->qdio.in_q->bufs[i].buffer =
			&card->qdio.in_q->qdio_bufs[i];
	/* inbound buffer pool */
	if (qeth_alloc_buffer_pool(card))
		goto out_freeinq;
	/* outbound */
	card->qdio.out_qs =
		kmalloc(card->qdio.no_out_queues *
			sizeof(struct qeth_qdio_out_q *), GFP_KERNEL);
	if (!card->qdio.out_qs)
		goto out_freepool;
	for (i = 0; i < card->qdio.no_out_queues; ++i) {
		card->qdio.out_qs[i] = kmalloc(sizeof(struct qeth_qdio_out_q),
					       GFP_KERNEL);
		if (!card->qdio.out_qs[i])
			goto out_freeoutq;
		QETH_DBF_TEXT_(SETUP, 2, "outq %i", i);
		QETH_DBF_HEX(SETUP, 2, &card->qdio.out_qs[i], sizeof(void *));
		memset(card->qdio.out_qs[i], 0, sizeof(struct qeth_qdio_out_q));
		card->qdio.out_qs[i]->queue_no = i;
		/* give outbound qeth_qdio_buffers their qdio_buffers */
		for (j = 0; j < QDIO_MAX_BUFFERS_PER_Q; ++j) {
			card->qdio.out_qs[i]->bufs[j].buffer =
				&card->qdio.out_qs[i]->qdio_bufs[j];
			skb_queue_head_init(&card->qdio.out_qs[i]->bufs[j].
					    skb_list);
			lockdep_set_class(
				&card->qdio.out_qs[i]->bufs[j].skb_list.lock,
				&qdio_out_skb_queue_key);
			INIT_LIST_HEAD(&card->qdio.out_qs[i]->bufs[j].ctx_list);
		}
	}
	return 0;

out_freeoutq:
	while (i > 0)
		kfree(card->qdio.out_qs[--i]);
	kfree(card->qdio.out_qs);
	card->qdio.out_qs = NULL;
out_freepool:
	qeth_free_buffer_pool(card);
out_freeinq:
	kfree(card->qdio.in_q);
	card->qdio.in_q = NULL;
out_nomem:
	atomic_set(&card->qdio.state, QETH_QDIO_UNINITIALIZED);
	return -ENOMEM;
}

static void qeth_create_qib_param_field(struct qeth_card *card,
		char *param_field)
{

	param_field[0] = _ascebc['P'];
	param_field[1] = _ascebc['C'];
	param_field[2] = _ascebc['I'];
	param_field[3] = _ascebc['T'];
	*((unsigned int *) (&param_field[4])) = QETH_PCI_THRESHOLD_A(card);
	*((unsigned int *) (&param_field[8])) = QETH_PCI_THRESHOLD_B(card);
	*((unsigned int *) (&param_field[12])) = QETH_PCI_TIMER_VALUE(card);
}

static void qeth_create_qib_param_field_blkt(struct qeth_card *card,
		char *param_field)
{
	param_field[16] = _ascebc['B'];
	param_field[17] = _ascebc['L'];
	param_field[18] = _ascebc['K'];
	param_field[19] = _ascebc['T'];
	*((unsigned int *) (&param_field[20])) = card->info.blkt.time_total;
	*((unsigned int *) (&param_field[24])) = card->info.blkt.inter_packet;
	*((unsigned int *) (&param_field[28])) =
		card->info.blkt.inter_packet_jumbo;
}

static int qeth_qdio_activate(struct qeth_card *card)
{
	QETH_DBF_TEXT(SETUP, 3, "qdioact");
	return qdio_activate(CARD_DDEV(card));
}

static int qeth_dm_act(struct qeth_card *card)
{
	int rc;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(SETUP, 2, "dmact");

	iob = qeth_wait_for_buffer(&card->write);
	memcpy(iob->data, DM_ACT, DM_ACT_SIZE);

	memcpy(QETH_DM_ACT_DEST_ADDR(iob->data),
	       &card->token.cm_connection_r, QETH_MPC_TOKEN_LENGTH);
	memcpy(QETH_DM_ACT_CONNECTION_TOKEN(iob->data),
	       &card->token.ulp_connection_r, QETH_MPC_TOKEN_LENGTH);
	rc = qeth_send_control_data(card, DM_ACT_SIZE, iob, NULL, NULL);
	return rc;
}

static int qeth_mpc_initialize(struct qeth_card *card)
{
	int rc;

	QETH_DBF_TEXT(SETUP, 2, "mpcinit");

	rc = qeth_issue_next_read(card);
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "1err%d", rc);
		return rc;
	}
	rc = qeth_cm_enable(card);
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "2err%d", rc);
		goto out_qdio;
	}
	rc = qeth_cm_setup(card);
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "3err%d", rc);
		goto out_qdio;
	}
	rc = qeth_ulp_enable(card);
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "4err%d", rc);
		goto out_qdio;
	}
	rc = qeth_ulp_setup(card);
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "5err%d", rc);
		goto out_qdio;
	}
	rc = qeth_alloc_qdio_buffers(card);
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "5err%d", rc);
		goto out_qdio;
	}
	rc = qeth_qdio_establish(card);
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "6err%d", rc);
		qeth_free_qdio_buffers(card);
		goto out_qdio;
	}
	rc = qeth_qdio_activate(card);
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "7err%d", rc);
		goto out_qdio;
	}
	rc = qeth_dm_act(card);
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "8err%d", rc);
		goto out_qdio;
	}

	return 0;
out_qdio:
	qeth_qdio_clear_card(card, card->info.type != QETH_CARD_TYPE_IQD);
	return rc;
}

static void qeth_print_status_with_portname(struct qeth_card *card)
{
	char dbf_text[15];
	int i;

	sprintf(dbf_text, "%s", card->info.portname + 1);
	for (i = 0; i < 8; i++)
		dbf_text[i] =
			(char) _ebcasc[(__u8) dbf_text[i]];
	dbf_text[8] = 0;
	dev_info(&card->gdev->dev, "Device is a%s card%s%s%s\n"
	       "with link type %s (portname: %s)\n",
	       qeth_get_cardname(card),
	       (card->info.mcl_level[0]) ? " (level: " : "",
	       (card->info.mcl_level[0]) ? card->info.mcl_level : "",
	       (card->info.mcl_level[0]) ? ")" : "",
	       qeth_get_cardname_short(card),
	       dbf_text);

}

static void qeth_print_status_no_portname(struct qeth_card *card)
{
	if (card->info.portname[0])
		dev_info(&card->gdev->dev, "Device is a%s "
		       "card%s%s%s\nwith link type %s "
		       "(no portname needed by interface).\n",
		       qeth_get_cardname(card),
		       (card->info.mcl_level[0]) ? " (level: " : "",
		       (card->info.mcl_level[0]) ? card->info.mcl_level : "",
		       (card->info.mcl_level[0]) ? ")" : "",
		       qeth_get_cardname_short(card));
	else
		dev_info(&card->gdev->dev, "Device is a%s "
		       "card%s%s%s\nwith link type %s.\n",
		       qeth_get_cardname(card),
		       (card->info.mcl_level[0]) ? " (level: " : "",
		       (card->info.mcl_level[0]) ? card->info.mcl_level : "",
		       (card->info.mcl_level[0]) ? ")" : "",
		       qeth_get_cardname_short(card));
}

void qeth_print_status_message(struct qeth_card *card)
{
	switch (card->info.type) {
	case QETH_CARD_TYPE_OSD:
	case QETH_CARD_TYPE_OSM:
	case QETH_CARD_TYPE_OSX:
		/* VM will use a non-zero first character
		 * to indicate a HiperSockets like reporting
		 * of the level OSA sets the first character to zero
		 * */
		if (!card->info.mcl_level[0]) {
			sprintf(card->info.mcl_level, "%02x%02x",
				card->info.mcl_level[2],
				card->info.mcl_level[3]);

			card->info.mcl_level[QETH_MCL_LENGTH] = 0;
			break;
		}
		/* fallthrough */
	case QETH_CARD_TYPE_IQD:
		if ((card->info.guestlan) ||
		    (card->info.mcl_level[0] & 0x80)) {
			card->info.mcl_level[0] = (char) _ebcasc[(__u8)
				card->info.mcl_level[0]];
			card->info.mcl_level[1] = (char) _ebcasc[(__u8)
				card->info.mcl_level[1]];
			card->info.mcl_level[2] = (char) _ebcasc[(__u8)
				card->info.mcl_level[2]];
			card->info.mcl_level[3] = (char) _ebcasc[(__u8)
				card->info.mcl_level[3]];
			card->info.mcl_level[QETH_MCL_LENGTH] = 0;
		}
		break;
	default:
		memset(&card->info.mcl_level[0], 0, QETH_MCL_LENGTH + 1);
	}
	if (card->info.portname_required)
		qeth_print_status_with_portname(card);
	else
		qeth_print_status_no_portname(card);
}
EXPORT_SYMBOL_GPL(qeth_print_status_message);

static void qeth_initialize_working_pool_list(struct qeth_card *card)
{
	struct qeth_buffer_pool_entry *entry;

	QETH_DBF_TEXT(TRACE, 5, "inwrklst");

	list_for_each_entry(entry,
			    &card->qdio.init_pool.entry_list, init_list) {
		qeth_put_buffer_pool_entry(card, entry);
	}
}

static inline struct qeth_buffer_pool_entry *qeth_find_free_buffer_pool_entry(
		struct qeth_card *card)
{
	struct list_head *plh;
	struct qeth_buffer_pool_entry *entry;
	int i, free;
	struct page *page;

	if (list_empty(&card->qdio.in_buf_pool.entry_list))
		return NULL;

	list_for_each(plh, &card->qdio.in_buf_pool.entry_list) {
		entry = list_entry(plh, struct qeth_buffer_pool_entry, list);
		free = 1;
		for (i = 0; i < QETH_MAX_BUFFER_ELEMENTS(card); ++i) {
			if (page_count(virt_to_page(entry->elements[i])) > 1) {
				free = 0;
				break;
			}
		}
		if (free) {
			list_del_init(&entry->list);
			return entry;
		}
	}

	/* no free buffer in pool so take first one and swap pages */
	entry = list_entry(card->qdio.in_buf_pool.entry_list.next,
			struct qeth_buffer_pool_entry, list);
	for (i = 0; i < QETH_MAX_BUFFER_ELEMENTS(card); ++i) {
		if (page_count(virt_to_page(entry->elements[i])) > 1) {
			page = alloc_page(GFP_ATOMIC);
			if (!page) {
				return NULL;
			} else {
				free_page((unsigned long)entry->elements[i]);
				entry->elements[i] = page_address(page);
				if (card->options.performance_stats)
					card->perf_stats.sg_alloc_page_rx++;
			}
		}
	}
	list_del_init(&entry->list);
	return entry;
}

static int qeth_init_input_buffer(struct qeth_card *card,
		struct qeth_qdio_buffer *buf)
{
	struct qeth_buffer_pool_entry *pool_entry;
	int i;

	pool_entry = qeth_find_free_buffer_pool_entry(card);
	if (!pool_entry)
		return 1;

	/*
	 * since the buffer is accessed only from the input_tasklet
	 * there shouldn't be a need to synchronize; also, since we use
	 * the QETH_IN_BUF_REQUEUE_THRESHOLD we should never run  out off
	 * buffers
	 */

	buf->pool_entry = pool_entry;
	for (i = 0; i < QETH_MAX_BUFFER_ELEMENTS(card); ++i) {
		buf->buffer->element[i].length = PAGE_SIZE;
		buf->buffer->element[i].addr =  pool_entry->elements[i];
		if (i == QETH_MAX_BUFFER_ELEMENTS(card) - 1)
			buf->buffer->element[i].flags = SBAL_FLAGS_LAST_ENTRY;
		else
			buf->buffer->element[i].flags = 0;
	}
	return 0;
}

int qeth_init_qdio_queues(struct qeth_card *card)
{
	int i, j;
	int rc;

	QETH_DBF_TEXT(SETUP, 2, "initqdqs");

	/* inbound queue */
	memset(card->qdio.in_q->qdio_bufs, 0,
	       QDIO_MAX_BUFFERS_PER_Q * sizeof(struct qdio_buffer));
	qeth_initialize_working_pool_list(card);
	/*give only as many buffers to hardware as we have buffer pool entries*/
	for (i = 0; i < card->qdio.in_buf_pool.buf_count - 1; ++i)
		qeth_init_input_buffer(card, &card->qdio.in_q->bufs[i]);
	card->qdio.in_q->next_buf_to_init =
		card->qdio.in_buf_pool.buf_count - 1;
	rc = do_QDIO(CARD_DDEV(card), QDIO_FLAG_SYNC_INPUT, 0, 0,
		     card->qdio.in_buf_pool.buf_count - 1);
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "1err%d", rc);
		return rc;
	}
	/* outbound queue */
	for (i = 0; i < card->qdio.no_out_queues; ++i) {
		memset(card->qdio.out_qs[i]->qdio_bufs, 0,
		       QDIO_MAX_BUFFERS_PER_Q * sizeof(struct qdio_buffer));
		for (j = 0; j < QDIO_MAX_BUFFERS_PER_Q; ++j) {
			qeth_clear_output_buffer(card->qdio.out_qs[i],
					&card->qdio.out_qs[i]->bufs[j]);
		}
		card->qdio.out_qs[i]->card = card;
		card->qdio.out_qs[i]->next_buf_to_fill = 0;
		card->qdio.out_qs[i]->do_pack = 0;
		atomic_set(&card->qdio.out_qs[i]->used_buffers, 0);
		atomic_set(&card->qdio.out_qs[i]->set_pci_flags_count, 0);
		atomic_set(&card->qdio.out_qs[i]->state,
			   QETH_OUT_Q_UNLOCKED);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(qeth_init_qdio_queues);

static inline __u8 qeth_get_ipa_adp_type(enum qeth_link_types link_type)
{
	switch (link_type) {
	case QETH_LINK_TYPE_HSTR:
		return 2;
	default:
		return 1;
	}
}

static void qeth_fill_ipacmd_header(struct qeth_card *card,
		struct qeth_ipa_cmd *cmd, __u8 command,
		enum qeth_prot_versions prot)
{
	memset(cmd, 0, sizeof(struct qeth_ipa_cmd));
	cmd->hdr.command = command;
	cmd->hdr.initiator = IPA_CMD_INITIATOR_HOST;
	cmd->hdr.seqno = card->seqno.ipa;
	cmd->hdr.adapter_type = qeth_get_ipa_adp_type(card->info.link_type);
	cmd->hdr.rel_adapter_no = (__u8) card->info.portno;
	if (card->options.layer2)
		cmd->hdr.prim_version_no = 2;
	else
		cmd->hdr.prim_version_no = 1;
	cmd->hdr.param_count = 1;
	cmd->hdr.prot_version = prot;
	cmd->hdr.ipa_supported = 0;
	cmd->hdr.ipa_enabled = 0;
}

struct qeth_cmd_buffer *qeth_get_ipacmd_buffer(struct qeth_card *card,
		enum qeth_ipa_cmds ipacmd, enum qeth_prot_versions prot)
{
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;

	iob = qeth_wait_for_buffer(&card->write);
	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
	qeth_fill_ipacmd_header(card, cmd, ipacmd, prot);

	return iob;
}
EXPORT_SYMBOL_GPL(qeth_get_ipacmd_buffer);

void qeth_prepare_ipa_cmd(struct qeth_card *card, struct qeth_cmd_buffer *iob,
		char prot_type)
{
	memcpy(iob->data, IPA_PDU_HEADER, IPA_PDU_HEADER_SIZE);
	memcpy(QETH_IPA_CMD_PROT_TYPE(iob->data), &prot_type, 1);
	memcpy(QETH_IPA_CMD_DEST_ADDR(iob->data),
	       &card->token.ulp_connection_r, QETH_MPC_TOKEN_LENGTH);
}
EXPORT_SYMBOL_GPL(qeth_prepare_ipa_cmd);

int qeth_send_ipa_cmd(struct qeth_card *card, struct qeth_cmd_buffer *iob,
		int (*reply_cb)(struct qeth_card *, struct qeth_reply*,
			unsigned long),
		void *reply_param)
{
	int rc;
	char prot_type;

	QETH_DBF_TEXT(TRACE, 4, "sendipa");

	if (card->options.layer2)
		if (card->info.type == QETH_CARD_TYPE_OSN)
			prot_type = QETH_PROT_OSN2;
		else
			prot_type = QETH_PROT_LAYER2;
	else
		prot_type = QETH_PROT_TCPIP;
	qeth_prepare_ipa_cmd(card, iob, prot_type);
	rc = qeth_send_control_data(card, IPA_CMD_LENGTH,
						iob, reply_cb, reply_param);
	return rc;
}
EXPORT_SYMBOL_GPL(qeth_send_ipa_cmd);

static int qeth_send_startstoplan(struct qeth_card *card,
		enum qeth_ipa_cmds ipacmd, enum qeth_prot_versions prot)
{
	int rc;
	struct qeth_cmd_buffer *iob;

	iob = qeth_get_ipacmd_buffer(card, ipacmd, prot);
	rc = qeth_send_ipa_cmd(card, iob, NULL, NULL);

	return rc;
}

int qeth_send_startlan(struct qeth_card *card)
{
	int rc;

	QETH_DBF_TEXT(SETUP, 2, "strtlan");

	rc = qeth_send_startstoplan(card, IPA_CMD_STARTLAN, 0);
	return rc;
}
EXPORT_SYMBOL_GPL(qeth_send_startlan);

int qeth_send_stoplan(struct qeth_card *card)
{
	int rc = 0;

	/*
	 * TODO: according to the IPA format document page 14,
	 * TCP/IP (we!) never issue a STOPLAN
	 * is this right ?!?
	 */
	QETH_DBF_TEXT(SETUP, 2, "stoplan");

	rc = qeth_send_startstoplan(card, IPA_CMD_STOPLAN, 0);
	return rc;
}
EXPORT_SYMBOL_GPL(qeth_send_stoplan);

int qeth_default_setadapterparms_cb(struct qeth_card *card,
		struct qeth_reply *reply, unsigned long data)
{
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(TRACE, 4, "defadpcb");

	cmd = (struct qeth_ipa_cmd *) data;
	if (cmd->hdr.return_code == 0)
		cmd->hdr.return_code =
			cmd->data.setadapterparms.hdr.return_code;
	return 0;
}
EXPORT_SYMBOL_GPL(qeth_default_setadapterparms_cb);

static int qeth_query_setadapterparms_cb(struct qeth_card *card,
		struct qeth_reply *reply, unsigned long data)
{
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(TRACE, 3, "quyadpcb");

	cmd = (struct qeth_ipa_cmd *) data;
	if (cmd->data.setadapterparms.data.query_cmds_supp.lan_type & 0x7f) {
		card->info.link_type =
		      cmd->data.setadapterparms.data.query_cmds_supp.lan_type;
		QETH_DBF_TEXT_(SETUP, 2, "lnk %d", card->info.link_type);
	}
	card->options.adp.supported_funcs =
		cmd->data.setadapterparms.data.query_cmds_supp.supported_cmds;
	return qeth_default_setadapterparms_cb(card, reply, (unsigned long)cmd);
}

struct qeth_cmd_buffer *qeth_get_adapter_cmd(struct qeth_card *card,
		__u32 command, __u32 cmdlen)
{
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;

	iob = qeth_get_ipacmd_buffer(card, IPA_CMD_SETADAPTERPARMS,
				     QETH_PROT_IPV4);
	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
	cmd->data.setadapterparms.hdr.cmdlength = cmdlen;
	cmd->data.setadapterparms.hdr.command_code = command;
	cmd->data.setadapterparms.hdr.used_total = 1;
	cmd->data.setadapterparms.hdr.seq_no = 1;

	return iob;
}
EXPORT_SYMBOL_GPL(qeth_get_adapter_cmd);

int qeth_query_setadapterparms(struct qeth_card *card)
{
	int rc;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(TRACE, 3, "queryadp");
	iob = qeth_get_adapter_cmd(card, IPA_SETADP_QUERY_COMMANDS_SUPPORTED,
				   sizeof(struct qeth_ipacmd_setadpparms));
	rc = qeth_send_ipa_cmd(card, iob, qeth_query_setadapterparms_cb, NULL);
	return rc;
}
EXPORT_SYMBOL_GPL(qeth_query_setadapterparms);

int qeth_check_qdio_errors(struct qeth_card *card, struct qdio_buffer *buf,
		unsigned int qdio_error, const char *dbftext)
{
	if (qdio_error) {
		QETH_DBF_TEXT(TRACE, 2, dbftext);
		QETH_DBF_TEXT(QERR, 2, dbftext);
		QETH_DBF_TEXT_(QERR, 2, " F15=%02X",
			       buf->element[15].flags & 0xff);
		QETH_DBF_TEXT_(QERR, 2, " F14=%02X",
			       buf->element[14].flags & 0xff);
		QETH_DBF_TEXT_(QERR, 2, " qerr=%X", qdio_error);
		if ((buf->element[15].flags & 0xff) == 0x12) {
			card->stats.rx_dropped++;
			return 0;
		} else
			return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(qeth_check_qdio_errors);

void qeth_queue_input_buffer(struct qeth_card *card, int index)
{
	struct qeth_qdio_q *queue = card->qdio.in_q;
	int count;
	int i;
	int rc;
	int newcount = 0;

	count = (index < queue->next_buf_to_init)?
		card->qdio.in_buf_pool.buf_count -
		(queue->next_buf_to_init - index) :
		card->qdio.in_buf_pool.buf_count -
		(queue->next_buf_to_init + QDIO_MAX_BUFFERS_PER_Q - index);
	/* only requeue at a certain threshold to avoid SIGAs */
	if (count >= QETH_IN_BUF_REQUEUE_THRESHOLD(card)) {
		for (i = queue->next_buf_to_init;
		     i < queue->next_buf_to_init + count; ++i) {
			if (qeth_init_input_buffer(card,
				&queue->bufs[i % QDIO_MAX_BUFFERS_PER_Q])) {
				break;
			} else {
				newcount++;
			}
		}

		if (newcount < count) {
			/* we are in memory shortage so we switch back to
			   traditional skb allocation and drop packages */
			atomic_set(&card->force_alloc_skb, 3);
			count = newcount;
		} else {
			atomic_add_unless(&card->force_alloc_skb, -1, 0);
		}

		/*
		 * according to old code it should be avoided to requeue all
		 * 128 buffers in order to benefit from PCI avoidance.
		 * this function keeps at least one buffer (the buffer at
		 * 'index') un-requeued -> this buffer is the first buffer that
		 * will be requeued the next time
		 */
		if (card->options.performance_stats) {
			card->perf_stats.inbound_do_qdio_cnt++;
			card->perf_stats.inbound_do_qdio_start_time =
				qeth_get_micros();
		}
		rc = do_QDIO(CARD_DDEV(card), QDIO_FLAG_SYNC_INPUT, 0,
			     queue->next_buf_to_init, count);
		if (card->options.performance_stats)
			card->perf_stats.inbound_do_qdio_time +=
				qeth_get_micros() -
				card->perf_stats.inbound_do_qdio_start_time;
		if (rc) {
			dev_warn(&card->gdev->dev,
				"QDIO reported an error, rc=%i\n", rc);
			QETH_DBF_TEXT(TRACE, 2, "qinberr");
			QETH_DBF_TEXT_(TRACE, 2, "%s", CARD_BUS_ID(card));
		}
		queue->next_buf_to_init = (queue->next_buf_to_init + count) %
					  QDIO_MAX_BUFFERS_PER_Q;
	}
}
EXPORT_SYMBOL_GPL(qeth_queue_input_buffer);

static int qeth_handle_send_error(struct qeth_card *card,
		struct qeth_qdio_out_buffer *buffer, unsigned int qdio_err)
{
	int sbalf15 = buffer->buffer->element[15].flags & 0xff;

	QETH_DBF_TEXT(TRACE, 6, "hdsnderr");
	if (card->info.type == QETH_CARD_TYPE_IQD) {
		if (sbalf15 == 0) {
			qdio_err = 0;
		} else {
			qdio_err = 1;
		}
	}
	qeth_check_qdio_errors(card, buffer->buffer, qdio_err, "qouterr");

	if (!qdio_err)
		return QETH_SEND_ERROR_NONE;

	if ((sbalf15 >= 15) && (sbalf15 <= 31))
		return QETH_SEND_ERROR_RETRY;

	QETH_DBF_TEXT(TRACE, 1, "lnkfail");
	QETH_DBF_TEXT_(TRACE, 1, "%s", CARD_BUS_ID(card));
	QETH_DBF_TEXT_(TRACE, 1, "%04x %02x",
		       (u16)qdio_err, (u8)sbalf15);
	return QETH_SEND_ERROR_LINK_FAILURE;
}

/*
 * Switched to packing state if the number of used buffers on a queue
 * reaches a certain limit.
 */
static void qeth_switch_to_packing_if_needed(struct qeth_qdio_out_q *queue)
{
	if (!queue->do_pack) {
		if (atomic_read(&queue->used_buffers)
		    >= QETH_HIGH_WATERMARK_PACK){
			/* switch non-PACKING -> PACKING */
			QETH_DBF_TEXT(TRACE, 6, "np->pack");
			if (queue->card->options.performance_stats)
				queue->card->perf_stats.sc_dp_p++;
			queue->do_pack = 1;
		}
	}
}

/*
 * Switches from packing to non-packing mode. If there is a packing
 * buffer on the queue this buffer will be prepared to be flushed.
 * In that case 1 is returned to inform the caller. If no buffer
 * has to be flushed, zero is returned.
 */
static int qeth_switch_to_nonpacking_if_needed(struct qeth_qdio_out_q *queue)
{
	struct qeth_qdio_out_buffer *buffer;
	int flush_count = 0;

	if (queue->do_pack) {
		if (atomic_read(&queue->used_buffers)
		    <= QETH_LOW_WATERMARK_PACK) {
			/* switch PACKING -> non-PACKING */
			QETH_DBF_TEXT(TRACE, 6, "pack->np");
			if (queue->card->options.performance_stats)
				queue->card->perf_stats.sc_p_dp++;
			queue->do_pack = 0;
			/* flush packing buffers */
			buffer = &queue->bufs[queue->next_buf_to_fill];
			if ((atomic_read(&buffer->state) ==
						QETH_QDIO_BUF_EMPTY) &&
			    (buffer->next_element_to_fill > 0)) {
				atomic_set(&buffer->state,
						QETH_QDIO_BUF_PRIMED);
				flush_count++;
				queue->next_buf_to_fill =
					(queue->next_buf_to_fill + 1) %
					QDIO_MAX_BUFFERS_PER_Q;
			}
		}
	}
	return flush_count;
}

/*
 * Called to flush a packing buffer if no more pci flags are on the queue.
 * Checks if there is a packing buffer and prepares it to be flushed.
 * In that case returns 1, otherwise zero.
 */
static int qeth_flush_buffers_on_no_pci(struct qeth_qdio_out_q *queue)
{
	struct qeth_qdio_out_buffer *buffer;

	buffer = &queue->bufs[queue->next_buf_to_fill];
	if ((atomic_read(&buffer->state) == QETH_QDIO_BUF_EMPTY) &&
	   (buffer->next_element_to_fill > 0)) {
		/* it's a packing buffer */
		atomic_set(&buffer->state, QETH_QDIO_BUF_PRIMED);
		queue->next_buf_to_fill =
			(queue->next_buf_to_fill + 1) % QDIO_MAX_BUFFERS_PER_Q;
		return 1;
	}
	return 0;
}

static void qeth_flush_buffers(struct qeth_qdio_out_q *queue, int index,
			       int count)
{
	struct qeth_qdio_out_buffer *buf;
	int rc;
	int i;
	unsigned int qdio_flags;

	for (i = index; i < index + count; ++i) {
		buf = &queue->bufs[i % QDIO_MAX_BUFFERS_PER_Q];
		buf->buffer->element[buf->next_element_to_fill - 1].flags |=
				SBAL_FLAGS_LAST_ENTRY;

		if (queue->card->info.type == QETH_CARD_TYPE_IQD)
			continue;

		if (!queue->do_pack) {
			if ((atomic_read(&queue->used_buffers) >=
				(QETH_HIGH_WATERMARK_PACK -
				 QETH_WATERMARK_PACK_FUZZ)) &&
			    !atomic_read(&queue->set_pci_flags_count)) {
				/* it's likely that we'll go to packing
				 * mode soon */
				atomic_inc(&queue->set_pci_flags_count);
				buf->buffer->element[0].flags |= 0x40;
			}
		} else {
			if (!atomic_read(&queue->set_pci_flags_count)) {
				/*
				 * there's no outstanding PCI any more, so we
				 * have to request a PCI to be sure the the PCI
				 * will wake at some time in the future then we
				 * can flush packed buffers that might still be
				 * hanging around, which can happen if no
				 * further send was requested by the stack
				 */
				atomic_inc(&queue->set_pci_flags_count);
				buf->buffer->element[0].flags |= 0x40;
			}
		}
	}

	queue->sync_iqdio_error = 0;
	queue->card->dev->trans_start = jiffies;
	if (queue->card->options.performance_stats) {
		queue->card->perf_stats.outbound_do_qdio_cnt++;
		queue->card->perf_stats.outbound_do_qdio_start_time =
			qeth_get_micros();
	}
	qdio_flags = QDIO_FLAG_SYNC_OUTPUT;
	if (atomic_read(&queue->set_pci_flags_count))
		qdio_flags |= QDIO_FLAG_PCI_OUT;
	rc = do_QDIO(CARD_DDEV(queue->card), qdio_flags,
		     queue->queue_no, index, count);
	if (queue->card->options.performance_stats)
		queue->card->perf_stats.outbound_do_qdio_time +=
			qeth_get_micros() -
			queue->card->perf_stats.outbound_do_qdio_start_time;
	if (rc > 0) {
		if (!(rc & QDIO_ERROR_SIGA_BUSY))
			queue->sync_iqdio_error = rc & 3;
	}
	if (rc) {
		queue->card->stats.tx_errors += count;
		/* ignore temporary SIGA errors without busy condition */
		if (rc == QDIO_ERROR_SIGA_TARGET)
			return;
		QETH_DBF_TEXT(TRACE, 2, "flushbuf");
		QETH_DBF_TEXT_(TRACE, 2, " err%d", rc);
		QETH_DBF_TEXT_(TRACE, 2, "%s", CARD_DDEV_ID(queue->card));

		/* this must not happen under normal circumstances. if it
		 * happens something is really wrong -> recover */
		qeth_schedule_recovery(queue->card);
		return;
	}
	atomic_add(count, &queue->used_buffers);
	if (queue->card->options.performance_stats)
		queue->card->perf_stats.bufs_sent += count;
}

static void qeth_check_outbound_queue(struct qeth_qdio_out_q *queue)
{
	int index;
	int flush_cnt = 0;
	int q_was_packing = 0;

	/*
	 * check if weed have to switch to non-packing mode or if
	 * we have to get a pci flag out on the queue
	 */
	if ((atomic_read(&queue->used_buffers) <= QETH_LOW_WATERMARK_PACK) ||
	    !atomic_read(&queue->set_pci_flags_count)) {
		if (atomic_xchg(&queue->state, QETH_OUT_Q_LOCKED_FLUSH) ==
				QETH_OUT_Q_UNLOCKED) {
			/*
			 * If we get in here, there was no action in
			 * do_send_packet. So, we check if there is a
			 * packing buffer to be flushed here.
			 */
			netif_stop_queue(queue->card->dev);
			index = queue->next_buf_to_fill;
			q_was_packing = queue->do_pack;
			/* queue->do_pack may change */
			barrier();
			flush_cnt += qeth_switch_to_nonpacking_if_needed(queue);
			if (!flush_cnt &&
			    !atomic_read(&queue->set_pci_flags_count))
				flush_cnt +=
					qeth_flush_buffers_on_no_pci(queue);
			if (queue->card->options.performance_stats &&
			    q_was_packing)
				queue->card->perf_stats.bufs_sent_pack +=
					flush_cnt;
			if (flush_cnt)
				qeth_flush_buffers(queue, index, flush_cnt);
			atomic_set(&queue->state, QETH_OUT_Q_UNLOCKED);
		}
	}
}

void qeth_qdio_output_handler(struct ccw_device *ccwdev,
		unsigned int qdio_error, int __queue, int first_element,
		int count, unsigned long card_ptr)
{
	struct qeth_card *card        = (struct qeth_card *) card_ptr;
	struct qeth_qdio_out_q *queue = card->qdio.out_qs[__queue];
	struct qeth_qdio_out_buffer *buffer;
	int i;
	unsigned qeth_send_err;

	QETH_DBF_TEXT(TRACE, 6, "qdouhdl");
	if (qdio_error & QDIO_ERROR_ACTIVATE_CHECK_CONDITION) {
		QETH_DBF_TEXT(TRACE, 2, "achkcond");
		QETH_DBF_TEXT_(TRACE, 2, "%s", CARD_BUS_ID(card));
		netif_stop_queue(card->dev);
		qeth_schedule_recovery(card);
		return;
	}
	if (card->options.performance_stats) {
		card->perf_stats.outbound_handler_cnt++;
		card->perf_stats.outbound_handler_start_time =
			qeth_get_micros();
	}
	for (i = first_element; i < (first_element + count); ++i) {
		buffer = &queue->bufs[i % QDIO_MAX_BUFFERS_PER_Q];
		qeth_send_err = qeth_handle_send_error(card, buffer, qdio_error);
		__qeth_clear_output_buffer(queue, buffer,
			(qeth_send_err == QETH_SEND_ERROR_RETRY) ? 1 : 0);
	}
	atomic_sub(count, &queue->used_buffers);
	/* check if we need to do something on this outbound queue */
	if (card->info.type != QETH_CARD_TYPE_IQD)
		qeth_check_outbound_queue(queue);

	netif_wake_queue(queue->card->dev);
	if (card->options.performance_stats)
		card->perf_stats.outbound_handler_time += qeth_get_micros() -
			card->perf_stats.outbound_handler_start_time;
}
EXPORT_SYMBOL_GPL(qeth_qdio_output_handler);

int qeth_get_priority_queue(struct qeth_card *card, struct sk_buff *skb,
			int ipv, int cast_type)
{
	if (!ipv && (card->info.type == QETH_CARD_TYPE_OSD ||
		     card->info.type == QETH_CARD_TYPE_OSX))
		return card->qdio.default_out_queue;
	switch (card->qdio.no_out_queues) {
	case 4:
		if (cast_type && card->info.is_multicast_different)
			return card->info.is_multicast_different &
				(card->qdio.no_out_queues - 1);
		if (card->qdio.do_prio_queueing && (ipv == 4)) {
			const u8 tos = ip_hdr(skb)->tos;

			if (card->qdio.do_prio_queueing ==
				QETH_PRIO_Q_ING_TOS) {
				if (tos & IP_TOS_NOTIMPORTANT)
					return 3;
				if (tos & IP_TOS_HIGHRELIABILITY)
					return 2;
				if (tos & IP_TOS_HIGHTHROUGHPUT)
					return 1;
				if (tos & IP_TOS_LOWDELAY)
					return 0;
			}
			if (card->qdio.do_prio_queueing ==
				QETH_PRIO_Q_ING_PREC)
				return 3 - (tos >> 6);
		} else if (card->qdio.do_prio_queueing && (ipv == 6)) {
			/* TODO: IPv6!!! */
		}
		return card->qdio.default_out_queue;
	case 1: /* fallthrough for single-out-queue 1920-device */
	default:
		return card->qdio.default_out_queue;
	}
}
EXPORT_SYMBOL_GPL(qeth_get_priority_queue);

int qeth_get_elements_no(struct qeth_card *card, void *hdr,
		     struct sk_buff *skb, int elems)
{
	int elements_needed = 0;

	if (skb_shinfo(skb)->nr_frags > 0)
		elements_needed = (skb_shinfo(skb)->nr_frags + 1);
	if (elements_needed == 0)
		elements_needed = 1 + (((((unsigned long) skb->data) %
				PAGE_SIZE) + skb->len) >> PAGE_SHIFT);
	if ((elements_needed + elems) > QETH_MAX_BUFFER_ELEMENTS(card)) {
		QETH_DBF_MESSAGE(2, "Invalid size of IP packet "
			"(Number=%d / Length=%d). Discarded.\n",
			(elements_needed+elems), skb->len);
		return 0;
	}
	return elements_needed;
}
EXPORT_SYMBOL_GPL(qeth_get_elements_no);

static inline void __qeth_fill_buffer(struct sk_buff *skb,
	struct qdio_buffer *buffer, int is_tso, int *next_element_to_fill,
	int offset)
{
	int length = skb->len;
	int length_here;
	int element;
	char *data;
	int first_lap ;

	element = *next_element_to_fill;
	data = skb->data;
	first_lap = (is_tso == 0 ? 1 : 0);

	if (offset >= 0) {
		data = skb->data + offset;
		length -= offset;
		first_lap = 0;
	}

	while (length > 0) {
		/* length_here is the remaining amount of data in this page */
		length_here = PAGE_SIZE - ((unsigned long) data % PAGE_SIZE);
		if (length < length_here)
			length_here = length;

		buffer->element[element].addr = data;
		buffer->element[element].length = length_here;
		length -= length_here;
		if (!length) {
			if (first_lap)
				buffer->element[element].flags = 0;
			else
				buffer->element[element].flags =
				    SBAL_FLAGS_LAST_FRAG;
		} else {
			if (first_lap)
				buffer->element[element].flags =
				    SBAL_FLAGS_FIRST_FRAG;
			else
				buffer->element[element].flags =
				    SBAL_FLAGS_MIDDLE_FRAG;
		}
		data += length_here;
		element++;
		first_lap = 0;
	}
	*next_element_to_fill = element;
}

static inline int qeth_fill_buffer(struct qeth_qdio_out_q *queue,
		struct qeth_qdio_out_buffer *buf, struct sk_buff *skb,
		struct qeth_hdr *hdr, int offset, int hd_len)
{
	struct qdio_buffer *buffer;
	int flush_cnt = 0, hdr_len, large_send = 0;

	buffer = buf->buffer;
	atomic_inc(&skb->users);
	skb_queue_tail(&buf->skb_list, skb);

	/*check first on TSO ....*/
	if (hdr->hdr.l3.id == QETH_HEADER_TYPE_TSO) {
		int element = buf->next_element_to_fill;

		hdr_len = sizeof(struct qeth_hdr_tso) +
			((struct qeth_hdr_tso *)hdr)->ext.dg_hdr_len;
		/*fill first buffer entry only with header information */
		buffer->element[element].addr = skb->data;
		buffer->element[element].length = hdr_len;
		buffer->element[element].flags = SBAL_FLAGS_FIRST_FRAG;
		buf->next_element_to_fill++;
		skb->data += hdr_len;
		skb->len  -= hdr_len;
		large_send = 1;
	}

	if (offset >= 0) {
		int element = buf->next_element_to_fill;
		buffer->element[element].addr = hdr;
		buffer->element[element].length = sizeof(struct qeth_hdr) +
							hd_len;
		buffer->element[element].flags = SBAL_FLAGS_FIRST_FRAG;
		buf->is_header[element] = 1;
		buf->next_element_to_fill++;
	}

	if (skb_shinfo(skb)->nr_frags == 0)
		__qeth_fill_buffer(skb, buffer, large_send,
				(int *)&buf->next_element_to_fill, offset);
	else
		__qeth_fill_buffer_frag(skb, buffer, large_send,
					(int *)&buf->next_element_to_fill);

	if (!queue->do_pack) {
		QETH_DBF_TEXT(TRACE, 6, "fillbfnp");
		/* set state to PRIMED -> will be flushed */
		atomic_set(&buf->state, QETH_QDIO_BUF_PRIMED);
		flush_cnt = 1;
	} else {
		QETH_DBF_TEXT(TRACE, 6, "fillbfpa");
		if (queue->card->options.performance_stats)
			queue->card->perf_stats.skbs_sent_pack++;
		if (buf->next_element_to_fill >=
				QETH_MAX_BUFFER_ELEMENTS(queue->card)) {
			/*
			 * packed buffer if full -> set state PRIMED
			 * -> will be flushed
			 */
			atomic_set(&buf->state, QETH_QDIO_BUF_PRIMED);
			flush_cnt = 1;
		}
	}
	return flush_cnt;
}

int qeth_do_send_packet_fast(struct qeth_card *card,
		struct qeth_qdio_out_q *queue, struct sk_buff *skb,
		struct qeth_hdr *hdr, int elements_needed,
		int offset, int hd_len)
{
	struct qeth_qdio_out_buffer *buffer;
	struct sk_buff *skb1;
	struct qeth_skb_data *retry_ctrl;
	int index;
	int rc;

	/* spin until we get the queue ... */
	while (atomic_cmpxchg(&queue->state, QETH_OUT_Q_UNLOCKED,
			      QETH_OUT_Q_LOCKED) != QETH_OUT_Q_UNLOCKED);
	/* ... now we've got the queue */
	index = queue->next_buf_to_fill;
	buffer = &queue->bufs[queue->next_buf_to_fill];
	/*
	 * check if buffer is empty to make sure that we do not 'overtake'
	 * ourselves and try to fill a buffer that is already primed
	 */
	if (atomic_read(&buffer->state) != QETH_QDIO_BUF_EMPTY)
		goto out;
	queue->next_buf_to_fill = (queue->next_buf_to_fill + 1) %
					  QDIO_MAX_BUFFERS_PER_Q;
	atomic_set(&queue->state, QETH_OUT_Q_UNLOCKED);
	qeth_fill_buffer(queue, buffer, skb, hdr, offset, hd_len);
	qeth_flush_buffers(queue, index, 1);
	if (queue->sync_iqdio_error == 2) {
		skb1 = skb_dequeue(&buffer->skb_list);
		while (skb1) {
			atomic_dec(&skb1->users);
			skb1 = skb_dequeue(&buffer->skb_list);
		}
		retry_ctrl = (struct qeth_skb_data *) &skb->cb[16];
		if (retry_ctrl->magic != QETH_SKB_MAGIC) {
			retry_ctrl->magic = QETH_SKB_MAGIC;
			retry_ctrl->count = 0;
		}
		if (retry_ctrl->count < QETH_SIGA_CC2_RETRIES) {
			retry_ctrl->count++;
			rc = dev_queue_xmit(skb);
		} else {
			dev_kfree_skb_any(skb);
			QETH_DBF_TEXT(QERR, 2, "qrdrop");
		}
	}
	return 0;
out:
	atomic_set(&queue->state, QETH_OUT_Q_UNLOCKED);
	return -EBUSY;
}
EXPORT_SYMBOL_GPL(qeth_do_send_packet_fast);

int qeth_do_send_packet(struct qeth_card *card, struct qeth_qdio_out_q *queue,
		struct sk_buff *skb, struct qeth_hdr *hdr,
		int elements_needed)
{
	struct qeth_qdio_out_buffer *buffer;
	int start_index;
	int flush_count = 0;
	int do_pack = 0;
	int tmp;
	int rc = 0;

	/* spin until we get the queue ... */
	while (atomic_cmpxchg(&queue->state, QETH_OUT_Q_UNLOCKED,
			      QETH_OUT_Q_LOCKED) != QETH_OUT_Q_UNLOCKED);
	start_index = queue->next_buf_to_fill;
	buffer = &queue->bufs[queue->next_buf_to_fill];
	/*
	 * check if buffer is empty to make sure that we do not 'overtake'
	 * ourselves and try to fill a buffer that is already primed
	 */
	if (atomic_read(&buffer->state) != QETH_QDIO_BUF_EMPTY) {
		atomic_set(&queue->state, QETH_OUT_Q_UNLOCKED);
		return -EBUSY;
	}
	/* check if we need to switch packing state of this queue */
	qeth_switch_to_packing_if_needed(queue);
	if (queue->do_pack) {
		do_pack = 1;
		/* does packet fit in current buffer? */
		if ((QETH_MAX_BUFFER_ELEMENTS(card) -
		    buffer->next_element_to_fill) < elements_needed) {
			/* ... no -> set state PRIMED */
			atomic_set(&buffer->state, QETH_QDIO_BUF_PRIMED);
			flush_count++;
			queue->next_buf_to_fill =
				(queue->next_buf_to_fill + 1) %
				QDIO_MAX_BUFFERS_PER_Q;
			buffer = &queue->bufs[queue->next_buf_to_fill];
			/* we did a step forward, so check buffer state
			 * again */
			if (atomic_read(&buffer->state) !=
			    QETH_QDIO_BUF_EMPTY) {
				qeth_flush_buffers(queue, start_index,
							   flush_count);
				atomic_set(&queue->state,
						QETH_OUT_Q_UNLOCKED);
				return -EBUSY;
			}
		}
	}
	tmp = qeth_fill_buffer(queue, buffer, skb, hdr, -1, 0);
	queue->next_buf_to_fill = (queue->next_buf_to_fill + tmp) %
				  QDIO_MAX_BUFFERS_PER_Q;
	flush_count += tmp;
	if (flush_count)
		qeth_flush_buffers(queue, start_index, flush_count);
	else if (!atomic_read(&queue->set_pci_flags_count))
		atomic_xchg(&queue->state, QETH_OUT_Q_LOCKED_FLUSH);
	/*
	 * queue->state will go from LOCKED -> UNLOCKED or from
	 * LOCKED_FLUSH -> LOCKED if output_handler wanted to 'notify' us
	 * (switch packing state or flush buffer to get another pci flag out).
	 * In that case we will enter this loop
	 */
	while (atomic_dec_return(&queue->state)) {
		flush_count = 0;
		start_index = queue->next_buf_to_fill;
		/* check if we can go back to non-packing state */
		flush_count += qeth_switch_to_nonpacking_if_needed(queue);
		/*
		 * check if we need to flush a packing buffer to get a pci
		 * flag out on the queue
		 */
		if (!flush_count && !atomic_read(&queue->set_pci_flags_count))
			flush_count += qeth_flush_buffers_on_no_pci(queue);
		if (flush_count)
			qeth_flush_buffers(queue, start_index, flush_count);
	}
	/* at this point the queue is UNLOCKED again */
	if (queue->card->options.performance_stats && do_pack)
		queue->card->perf_stats.bufs_sent_pack += flush_count;

	return rc;
}
EXPORT_SYMBOL_GPL(qeth_do_send_packet);

static int qeth_setadp_promisc_mode_cb(struct qeth_card *card,
		struct qeth_reply *reply, unsigned long data)
{
	struct qeth_ipa_cmd *cmd;
	struct qeth_ipacmd_setadpparms *setparms;

	QETH_DBF_TEXT(TRACE, 4, "prmadpcb");

	cmd = (struct qeth_ipa_cmd *) data;
	setparms = &(cmd->data.setadapterparms);

	qeth_default_setadapterparms_cb(card, reply, (unsigned long)cmd);
	if (cmd->hdr.return_code) {
		QETH_DBF_TEXT_(TRACE, 4, "prmrc%2.2x", cmd->hdr.return_code);
		setparms->data.mode = SET_PROMISC_MODE_OFF;
	}
	card->info.promisc_mode = setparms->data.mode;
	return 0;
}

void qeth_setadp_promisc_mode(struct qeth_card *card)
{
	enum qeth_ipa_promisc_modes mode;
	struct net_device *dev = card->dev;
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(TRACE, 4, "setprom");

	if (((dev->flags & IFF_PROMISC) &&
	     (card->info.promisc_mode == SET_PROMISC_MODE_ON)) ||
	    (!(dev->flags & IFF_PROMISC) &&
	     (card->info.promisc_mode == SET_PROMISC_MODE_OFF)))
		return;
	mode = SET_PROMISC_MODE_OFF;
	if (dev->flags & IFF_PROMISC)
		mode = SET_PROMISC_MODE_ON;
	QETH_DBF_TEXT_(TRACE, 4, "mode:%x", mode);

	iob = qeth_get_adapter_cmd(card, IPA_SETADP_SET_PROMISC_MODE,
			sizeof(struct qeth_ipacmd_setadpparms));
	cmd = (struct qeth_ipa_cmd *)(iob->data + IPA_PDU_HEADER_SIZE);
	cmd->data.setadapterparms.data.mode = mode;
	qeth_send_ipa_cmd(card, iob, qeth_setadp_promisc_mode_cb, NULL);
}
EXPORT_SYMBOL_GPL(qeth_setadp_promisc_mode);

int qeth_change_mtu(struct net_device *dev, int new_mtu)
{
	struct qeth_card *card;
	char dbf_text[15];

	card = dev->ml_priv;

	QETH_DBF_TEXT(TRACE, 4, "chgmtu");
	sprintf(dbf_text, "%8x", new_mtu);
	QETH_DBF_TEXT(TRACE, 4, dbf_text);

	if (new_mtu < 64)
		return -EINVAL;
	if (new_mtu > 65535)
		return -EINVAL;
	if ((!qeth_is_supported(card, IPA_IP_FRAGMENTATION)) &&
	    (!qeth_mtu_is_valid(card, new_mtu)))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}
EXPORT_SYMBOL_GPL(qeth_change_mtu);

struct net_device_stats *qeth_get_stats(struct net_device *dev)
{
	struct qeth_card *card;

	card = dev->ml_priv;

	QETH_DBF_TEXT(TRACE, 5, "getstat");

	return &card->stats;
}
EXPORT_SYMBOL_GPL(qeth_get_stats);

static int qeth_setadpparms_change_macaddr_cb(struct qeth_card *card,
		struct qeth_reply *reply, unsigned long data)
{
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(TRACE, 4, "chgmaccb");

	cmd = (struct qeth_ipa_cmd *) data;
	if (!card->options.layer2 ||
	    !(card->info.mac_bits & QETH_LAYER2_MAC_READ)) {
		memcpy(card->dev->dev_addr,
		       &cmd->data.setadapterparms.data.change_addr.addr,
		       OSA_ADDR_LEN);
		card->info.mac_bits |= QETH_LAYER2_MAC_READ;
	}
	qeth_default_setadapterparms_cb(card, reply, (unsigned long) cmd);
	return 0;
}

int qeth_setadpparms_change_macaddr(struct qeth_card *card)
{
	int rc;
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(TRACE, 4, "chgmac");

	iob = qeth_get_adapter_cmd(card, IPA_SETADP_ALTER_MAC_ADDRESS,
				   sizeof(struct qeth_ipacmd_setadpparms));
	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
	cmd->data.setadapterparms.data.change_addr.cmd = CHANGE_ADDR_READ_MAC;
	cmd->data.setadapterparms.data.change_addr.addr_size = OSA_ADDR_LEN;
	memcpy(&cmd->data.setadapterparms.data.change_addr.addr,
	       card->dev->dev_addr, OSA_ADDR_LEN);
	rc = qeth_send_ipa_cmd(card, iob, qeth_setadpparms_change_macaddr_cb,
			       NULL);
	return rc;
}
EXPORT_SYMBOL_GPL(qeth_setadpparms_change_macaddr);

static int qeth_setadpparms_set_access_ctrl_cb(struct qeth_card *card,
		struct qeth_reply *reply, unsigned long data)
{
	struct qeth_ipa_cmd *cmd;
	struct qeth_set_access_ctrl *access_ctrl_req;
	int rc;

	QETH_DBF_TEXT(TRACE, 4, "setaccb");

	cmd = (struct qeth_ipa_cmd *) data;
	access_ctrl_req = &cmd->data.setadapterparms.data.set_access_ctrl;
	QETH_DBF_TEXT_(SETUP, 2, "setaccb");
	QETH_DBF_TEXT_(SETUP, 2, "%s", card->gdev->dev.kobj.name);
	QETH_DBF_TEXT_(SETUP, 2, "rc=%d",
		cmd->data.setadapterparms.hdr.return_code);
	switch (cmd->data.setadapterparms.hdr.return_code) {
	case SET_ACCESS_CTRL_RC_SUCCESS:
	case SET_ACCESS_CTRL_RC_ALREADY_NOT_ISOLATED:
	case SET_ACCESS_CTRL_RC_ALREADY_ISOLATED:
	{
		card->options.isolation = access_ctrl_req->subcmd_code;
		if (card->options.isolation == ISOLATION_MODE_NONE) {
			dev_info(&card->gdev->dev,
			    "QDIO data connection isolation is deactivated\n");
		} else {
			dev_info(&card->gdev->dev,
			    "QDIO data connection isolation is activated\n");
		}
		QETH_DBF_MESSAGE(3, "OK:SET_ACCESS_CTRL(%s, %d)==%d\n",
			card->gdev->dev.kobj.name,
			access_ctrl_req->subcmd_code,
			cmd->data.setadapterparms.hdr.return_code);
		rc = 0;
		break;
	}
	case SET_ACCESS_CTRL_RC_NOT_SUPPORTED:
	{
		QETH_DBF_MESSAGE(3, "ERR:SET_ACCESS_CTRL(%s,%d)==%d\n",
			card->gdev->dev.kobj.name,
			access_ctrl_req->subcmd_code,
			cmd->data.setadapterparms.hdr.return_code);
		dev_err(&card->gdev->dev, "Adapter does not "
			"support QDIO data connection isolation\n");

		/* ensure isolation mode is "none" */
		card->options.isolation = ISOLATION_MODE_NONE;
		rc = -EOPNOTSUPP;
		break;
	}
	case SET_ACCESS_CTRL_RC_NONE_SHARED_ADAPTER:
	{
		QETH_DBF_MESSAGE(3, "ERR:SET_ACCESS_MODE(%s,%d)==%d\n",
			card->gdev->dev.kobj.name,
			access_ctrl_req->subcmd_code,
			cmd->data.setadapterparms.hdr.return_code);
		dev_err(&card->gdev->dev,
			"Adapter is dedicated. "
			"QDIO data connection isolation not supported\n");

		/* ensure isolation mode is "none" */
		card->options.isolation = ISOLATION_MODE_NONE;
		rc = -EOPNOTSUPP;
		break;
	}
	case SET_ACCESS_CTRL_RC_ACTIVE_CHECKSUM_OFF:
	{
		QETH_DBF_MESSAGE(3, "ERR:SET_ACCESS_MODE(%s,%d)==%d\n",
			card->gdev->dev.kobj.name,
			access_ctrl_req->subcmd_code,
			cmd->data.setadapterparms.hdr.return_code);
		dev_err(&card->gdev->dev,
			"TSO does not permit QDIO data connection isolation\n");

		/* ensure isolation mode is "none" */
		card->options.isolation = ISOLATION_MODE_NONE;
		rc = -EPERM;
		break;
	}
	default:
	{
		/* this should never happen */
		QETH_DBF_MESSAGE(3, "ERR:SET_ACCESS_MODE(%s,%d)==%d"
			"==UNKNOWN\n",
			card->gdev->dev.kobj.name,
			access_ctrl_req->subcmd_code,
			cmd->data.setadapterparms.hdr.return_code);

		/* ensure isolation mode is "none" */
		card->options.isolation = ISOLATION_MODE_NONE;
		rc = 0;
		break;
	}
	}
	qeth_default_setadapterparms_cb(card, reply, (unsigned long) cmd);
	return rc;
}

static int qeth_setadpparms_set_access_ctrl(struct qeth_card *card,
		enum qeth_ipa_isolation_modes isolation)
{
	int rc;
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;
	struct qeth_set_access_ctrl *access_ctrl_req;

	QETH_DBF_TEXT(TRACE, 4, "setacctl");

	QETH_DBF_TEXT_(SETUP, 2, "setacctl");
	QETH_DBF_TEXT_(SETUP, 2, "%s", card->gdev->dev.kobj.name);

	iob = qeth_get_adapter_cmd(card, IPA_SETADP_SET_ACCESS_CONTROL,
				   sizeof(struct qeth_ipacmd_setadpparms_hdr) +
				   sizeof(struct qeth_set_access_ctrl));
	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
	access_ctrl_req = &cmd->data.setadapterparms.data.set_access_ctrl;
	access_ctrl_req->subcmd_code = isolation;

	rc = qeth_send_ipa_cmd(card, iob, qeth_setadpparms_set_access_ctrl_cb,
			       NULL);
	QETH_DBF_TEXT_(SETUP, 2, "rc=%d", rc);
	return rc;
}

int qeth_set_access_ctrl_online(struct qeth_card *card)
{
	int rc = 0;

	QETH_DBF_TEXT(TRACE, 4, "setactlo");

	if ((card->info.type == QETH_CARD_TYPE_OSD ||
	     card->info.type == QETH_CARD_TYPE_OSX) &&
	     qeth_adp_supported(card, IPA_SETADP_SET_ACCESS_CONTROL)) {
		rc = qeth_setadpparms_set_access_ctrl(card,
			card->options.isolation);
		if (rc) {
			QETH_DBF_MESSAGE(3,
				"IPA(SET_ACCESS_CTRL,%s,%d) sent failed\n",
				card->gdev->dev.kobj.name,
				rc);
		}
	} else if (card->options.isolation != ISOLATION_MODE_NONE) {
		card->options.isolation = ISOLATION_MODE_NONE;

		dev_err(&card->gdev->dev, "Adapter does not "
			"support QDIO data connection isolation\n");
		rc = -EOPNOTSUPP;
	}
	return rc;
}
EXPORT_SYMBOL_GPL(qeth_set_access_ctrl_online);

void qeth_tx_timeout(struct net_device *dev)
{
	struct qeth_card *card;

	QETH_DBF_TEXT(TRACE, 4, "txtimeo");
	card = dev->ml_priv;
	card->stats.tx_errors++;
	qeth_schedule_recovery(card);
}
EXPORT_SYMBOL_GPL(qeth_tx_timeout);

int qeth_mdio_read(struct net_device *dev, int phy_id, int regnum)
{
	struct qeth_card *card = dev->ml_priv;
	int rc = 0;

	switch (regnum) {
	case MII_BMCR: /* Basic mode control register */
		rc = BMCR_FULLDPLX;
		if ((card->info.link_type != QETH_LINK_TYPE_GBIT_ETH) &&
		    (card->info.link_type != QETH_LINK_TYPE_OSN) &&
		    (card->info.link_type != QETH_LINK_TYPE_10GBIT_ETH))
			rc |= BMCR_SPEED100;
		break;
	case MII_BMSR: /* Basic mode status register */
		rc = BMSR_ERCAP | BMSR_ANEGCOMPLETE | BMSR_LSTATUS |
		     BMSR_10HALF | BMSR_10FULL | BMSR_100HALF | BMSR_100FULL |
		     BMSR_100BASE4;
		break;
	case MII_PHYSID1: /* PHYS ID 1 */
		rc = (dev->dev_addr[0] << 16) | (dev->dev_addr[1] << 8) |
		     dev->dev_addr[2];
		rc = (rc >> 5) & 0xFFFF;
		break;
	case MII_PHYSID2: /* PHYS ID 2 */
		rc = (dev->dev_addr[2] << 10) & 0xFFFF;
		break;
	case MII_ADVERTISE: /* Advertisement control reg */
		rc = ADVERTISE_ALL;
		break;
	case MII_LPA: /* Link partner ability reg */
		rc = LPA_10HALF | LPA_10FULL | LPA_100HALF | LPA_100FULL |
		     LPA_100BASE4 | LPA_LPACK;
		break;
	case MII_EXPANSION: /* Expansion register */
		break;
	case MII_DCOUNTER: /* disconnect counter */
		break;
	case MII_FCSCOUNTER: /* false carrier counter */
		break;
	case MII_NWAYTEST: /* N-way auto-neg test register */
		break;
	case MII_RERRCOUNTER: /* rx error counter */
		rc = card->stats.rx_errors;
		break;
	case MII_SREVISION: /* silicon revision */
		break;
	case MII_RESV1: /* reserved 1 */
		break;
	case MII_LBRERROR: /* loopback, rx, bypass error */
		break;
	case MII_PHYADDR: /* physical address */
		break;
	case MII_RESV2: /* reserved 2 */
		break;
	case MII_TPISTATUS: /* TPI status for 10mbps */
		break;
	case MII_NCONFIG: /* network interface config */
		break;
	default:
		break;
	}
	return rc;
}
EXPORT_SYMBOL_GPL(qeth_mdio_read);

static int qeth_send_ipa_snmp_cmd(struct qeth_card *card,
		struct qeth_cmd_buffer *iob, int len,
		int (*reply_cb)(struct qeth_card *, struct qeth_reply *,
			unsigned long),
		void *reply_param)
{
	u16 s1, s2;

	QETH_DBF_TEXT(TRACE, 4, "sendsnmp");

	memcpy(iob->data, IPA_PDU_HEADER, IPA_PDU_HEADER_SIZE);
	memcpy(QETH_IPA_CMD_DEST_ADDR(iob->data),
	       &card->token.ulp_connection_r, QETH_MPC_TOKEN_LENGTH);
	/* adjust PDU length fields in IPA_PDU_HEADER */
	s1 = (u32) IPA_PDU_HEADER_SIZE + len;
	s2 = (u32) len;
	memcpy(QETH_IPA_PDU_LEN_TOTAL(iob->data), &s1, 2);
	memcpy(QETH_IPA_PDU_LEN_PDU1(iob->data), &s2, 2);
	memcpy(QETH_IPA_PDU_LEN_PDU2(iob->data), &s2, 2);
	memcpy(QETH_IPA_PDU_LEN_PDU3(iob->data), &s2, 2);
	return qeth_send_control_data(card, IPA_PDU_HEADER_SIZE + len, iob,
				      reply_cb, reply_param);
}

static int qeth_snmp_command_cb(struct qeth_card *card,
		struct qeth_reply *reply, unsigned long sdata)
{
	struct qeth_ipa_cmd *cmd;
	struct qeth_arp_query_info *qinfo;
	struct qeth_snmp_cmd *snmp;
	unsigned char *data;
	__u16 data_len;

	QETH_DBF_TEXT(TRACE, 3, "snpcmdcb");

	cmd = (struct qeth_ipa_cmd *) sdata;
	data = (unsigned char *)((char *)cmd - reply->offset);
	qinfo = (struct qeth_arp_query_info *) reply->param;
	snmp = &cmd->data.setadapterparms.data.snmp;

	if (cmd->hdr.return_code) {
		QETH_DBF_TEXT_(TRACE, 4, "scer1%i", cmd->hdr.return_code);
		return 0;
	}
	if (cmd->data.setadapterparms.hdr.return_code) {
		cmd->hdr.return_code =
			cmd->data.setadapterparms.hdr.return_code;
		QETH_DBF_TEXT_(TRACE, 4, "scer2%i", cmd->hdr.return_code);
		return 0;
	}
	data_len = *((__u16 *)QETH_IPA_PDU_LEN_PDU1(data));
	if (cmd->data.setadapterparms.hdr.seq_no == 1)
		data_len -= (__u16)((char *)&snmp->data - (char *)cmd);
	else
		data_len -= (__u16)((char *)&snmp->request - (char *)cmd);

	/* check if there is enough room in userspace */
	if ((qinfo->udata_len - qinfo->udata_offset) < data_len) {
		QETH_DBF_TEXT_(TRACE, 4, "scer3%i", -ENOMEM);
		cmd->hdr.return_code = -ENOMEM;
		return 0;
	}
	QETH_DBF_TEXT_(TRACE, 4, "snore%i",
		       cmd->data.setadapterparms.hdr.used_total);
	QETH_DBF_TEXT_(TRACE, 4, "sseqn%i",
		cmd->data.setadapterparms.hdr.seq_no);
	/*copy entries to user buffer*/
	if (cmd->data.setadapterparms.hdr.seq_no == 1) {
		memcpy(qinfo->udata + qinfo->udata_offset,
		       (char *)snmp,
		       data_len + offsetof(struct qeth_snmp_cmd, data));
		qinfo->udata_offset += offsetof(struct qeth_snmp_cmd, data);
	} else {
		memcpy(qinfo->udata + qinfo->udata_offset,
		       (char *)&snmp->request, data_len);
	}
	qinfo->udata_offset += data_len;
	/* check if all replies received ... */
		QETH_DBF_TEXT_(TRACE, 4, "srtot%i",
			       cmd->data.setadapterparms.hdr.used_total);
		QETH_DBF_TEXT_(TRACE, 4, "srseq%i",
			       cmd->data.setadapterparms.hdr.seq_no);
	if (cmd->data.setadapterparms.hdr.seq_no <
	    cmd->data.setadapterparms.hdr.used_total)
		return 1;
	return 0;
}

int qeth_snmp_command(struct qeth_card *card, char __user *udata)
{
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;
	struct qeth_snmp_ureq *ureq;
	int req_len;
	struct qeth_arp_query_info qinfo = {0, };
	int rc = 0;

	QETH_DBF_TEXT(TRACE, 3, "snmpcmd");

	if (card->info.guestlan)
		return -EOPNOTSUPP;

	if ((!qeth_adp_supported(card, IPA_SETADP_SET_SNMP_CONTROL)) &&
	    (!card->options.layer2)) {
		return -EOPNOTSUPP;
	}
	/* skip 4 bytes (data_len struct member) to get req_len */
	if (copy_from_user(&req_len, udata + sizeof(int), sizeof(int)))
		return -EFAULT;
	ureq = kmalloc(req_len+sizeof(struct qeth_snmp_ureq_hdr), GFP_KERNEL);
	if (!ureq) {
		QETH_DBF_TEXT(TRACE, 2, "snmpnome");
		return -ENOMEM;
	}
	if (copy_from_user(ureq, udata,
			req_len + sizeof(struct qeth_snmp_ureq_hdr))) {
		kfree(ureq);
		return -EFAULT;
	}
	qinfo.udata_len = ureq->hdr.data_len;
	qinfo.udata = kzalloc(qinfo.udata_len, GFP_KERNEL);
	if (!qinfo.udata) {
		kfree(ureq);
		return -ENOMEM;
	}
	qinfo.udata_offset = sizeof(struct qeth_snmp_ureq_hdr);

	iob = qeth_get_adapter_cmd(card, IPA_SETADP_SET_SNMP_CONTROL,
				   QETH_SNMP_SETADP_CMDLENGTH + req_len);
	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
	memcpy(&cmd->data.setadapterparms.data.snmp, &ureq->cmd, req_len);
	rc = qeth_send_ipa_snmp_cmd(card, iob, QETH_SETADP_BASE_LEN + req_len,
				    qeth_snmp_command_cb, (void *)&qinfo);
	if (rc)
		QETH_DBF_MESSAGE(2, "SNMP command failed on %s: (0x%x)\n",
			   QETH_CARD_IFNAME(card), rc);
	else {
		if (copy_to_user(udata, qinfo.udata, qinfo.udata_len))
			rc = -EFAULT;
	}

	kfree(ureq);
	kfree(qinfo.udata);
	return rc;
}
EXPORT_SYMBOL_GPL(qeth_snmp_command);

static inline int qeth_get_qdio_q_format(struct qeth_card *card)
{
	switch (card->info.type) {
	case QETH_CARD_TYPE_IQD:
		return 2;
	default:
		return 0;
	}
}

static int qeth_qdio_establish(struct qeth_card *card)
{
	struct qdio_initialize init_data;
	char *qib_param_field;
	struct qdio_buffer **in_sbal_ptrs;
	struct qdio_buffer **out_sbal_ptrs;
	int i, j, k;
	int rc = 0;

	QETH_DBF_TEXT(SETUP, 2, "qdioest");

	qib_param_field = kzalloc(QDIO_MAX_BUFFERS_PER_Q * sizeof(char),
			      GFP_KERNEL);
	if (!qib_param_field)
		return -ENOMEM;

	qeth_create_qib_param_field(card, qib_param_field);
	qeth_create_qib_param_field_blkt(card, qib_param_field);

	in_sbal_ptrs = kmalloc(QDIO_MAX_BUFFERS_PER_Q * sizeof(void *),
			       GFP_KERNEL);
	if (!in_sbal_ptrs) {
		kfree(qib_param_field);
		return -ENOMEM;
	}
	for (i = 0; i < QDIO_MAX_BUFFERS_PER_Q; ++i)
		in_sbal_ptrs[i] = (struct qdio_buffer *)
			virt_to_phys(card->qdio.in_q->bufs[i].buffer);

	out_sbal_ptrs =
		kmalloc(card->qdio.no_out_queues * QDIO_MAX_BUFFERS_PER_Q *
			sizeof(void *), GFP_KERNEL);
	if (!out_sbal_ptrs) {
		kfree(in_sbal_ptrs);
		kfree(qib_param_field);
		return -ENOMEM;
	}
	for (i = 0, k = 0; i < card->qdio.no_out_queues; ++i)
		for (j = 0; j < QDIO_MAX_BUFFERS_PER_Q; ++j, ++k) {
			out_sbal_ptrs[k] = (struct qdio_buffer *)virt_to_phys(
				card->qdio.out_qs[i]->bufs[j].buffer);
		}

	memset(&init_data, 0, sizeof(struct qdio_initialize));
	init_data.cdev                   = CARD_DDEV(card);
	init_data.q_format               = qeth_get_qdio_q_format(card);
	init_data.qib_param_field_format = 0;
	init_data.qib_param_field        = qib_param_field;
	init_data.no_input_qs            = 1;
	init_data.no_output_qs           = card->qdio.no_out_queues;
	init_data.input_handler          = card->discipline.input_handler;
	init_data.output_handler         = card->discipline.output_handler;
	init_data.int_parm               = (unsigned long) card;
	init_data.input_sbal_addr_array  = (void **) in_sbal_ptrs;
	init_data.output_sbal_addr_array = (void **) out_sbal_ptrs;

	if (atomic_cmpxchg(&card->qdio.state, QETH_QDIO_ALLOCATED,
		QETH_QDIO_ESTABLISHED) == QETH_QDIO_ALLOCATED) {
		rc = qdio_allocate(&init_data);
		if (rc) {
			atomic_set(&card->qdio.state, QETH_QDIO_ALLOCATED);
			goto out;
		}
		rc = qdio_establish(&init_data);
		if (rc) {
			atomic_set(&card->qdio.state, QETH_QDIO_ALLOCATED);
			qdio_free(CARD_DDEV(card));
		}
	}
out:
	kfree(out_sbal_ptrs);
	kfree(in_sbal_ptrs);
	kfree(qib_param_field);
	return rc;
}

static void qeth_core_free_card(struct qeth_card *card)
{

	QETH_DBF_TEXT(SETUP, 2, "freecrd");
	QETH_DBF_HEX(SETUP, 2, &card, sizeof(void *));
	qeth_clean_channel(&card->read);
	qeth_clean_channel(&card->write);
	if (card->dev)
		free_netdev(card->dev);
	kfree(card->ip_tbd_list);
	qeth_free_qdio_buffers(card);
	unregister_service_level(&card->qeth_service_level);
	kfree(card);
}

static struct ccw_device_id qeth_ids[] = {
	{CCW_DEVICE_DEVTYPE(0x1731, 0x01, 0x1732, 0x01),
					.driver_info = QETH_CARD_TYPE_OSD},
	{CCW_DEVICE_DEVTYPE(0x1731, 0x05, 0x1732, 0x05),
					.driver_info = QETH_CARD_TYPE_IQD},
	{CCW_DEVICE_DEVTYPE(0x1731, 0x06, 0x1732, 0x06),
					.driver_info = QETH_CARD_TYPE_OSN},
	{CCW_DEVICE_DEVTYPE(0x1731, 0x02, 0x1732, 0x03),
					.driver_info = QETH_CARD_TYPE_OSM},
	{CCW_DEVICE_DEVTYPE(0x1731, 0x02, 0x1732, 0x02),
					.driver_info = QETH_CARD_TYPE_OSX},
	{},
};
MODULE_DEVICE_TABLE(ccw, qeth_ids);

static struct ccw_driver qeth_ccw_driver = {
	.name = "qeth",
	.ids = qeth_ids,
	.probe = ccwgroup_probe_ccwdev,
	.remove = ccwgroup_remove_ccwdev,
};

static int qeth_core_driver_group(const char *buf, struct device *root_dev,
				unsigned long driver_id)
{
	return ccwgroup_create_from_string(root_dev, driver_id,
					   &qeth_ccw_driver, 3, buf);
}

int qeth_core_hardsetup_card(struct qeth_card *card)
{
	int retries = 0;
	int rc;

	QETH_DBF_TEXT(SETUP, 2, "hrdsetup");
	atomic_set(&card->force_alloc_skb, 0);
retry:
	if (retries)
		QETH_DBF_MESSAGE(2, "%s Retrying to do IDX activates.\n",
			dev_name(&card->gdev->dev));
	ccw_device_set_offline(CARD_DDEV(card));
	ccw_device_set_offline(CARD_WDEV(card));
	ccw_device_set_offline(CARD_RDEV(card));
	rc = ccw_device_set_online(CARD_RDEV(card));
	if (rc)
		goto retriable;
	rc = ccw_device_set_online(CARD_WDEV(card));
	if (rc)
		goto retriable;
	rc = ccw_device_set_online(CARD_DDEV(card));
	if (rc)
		goto retriable;
	rc = qeth_qdio_clear_card(card, card->info.type != QETH_CARD_TYPE_IQD);
retriable:
	if (rc == -ERESTARTSYS) {
		QETH_DBF_TEXT(SETUP, 2, "break1");
		return rc;
	} else if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "1err%d", rc);
		if (++retries > 3)
			goto out;
		else
			goto retry;
	}
	qeth_init_tokens(card);
	qeth_init_func_level(card);
	rc = qeth_idx_activate_channel(&card->read, qeth_idx_read_cb);
	if (rc == -ERESTARTSYS) {
		QETH_DBF_TEXT(SETUP, 2, "break2");
		return rc;
	} else if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "3err%d", rc);
		if (--retries < 0)
			goto out;
		else
			goto retry;
	}
	rc = qeth_idx_activate_channel(&card->write, qeth_idx_write_cb);
	if (rc == -ERESTARTSYS) {
		QETH_DBF_TEXT(SETUP, 2, "break3");
		return rc;
	} else if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "4err%d", rc);
		if (--retries < 0)
			goto out;
		else
			goto retry;
	}
	rc = qeth_mpc_initialize(card);
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "5err%d", rc);
		goto out;
	}
	return 0;
out:
	dev_warn(&card->gdev->dev, "The qeth device driver failed to recover "
		"an error on the device\n");
	QETH_DBF_MESSAGE(2, "%s Initialization in hardsetup failed! rc=%d\n",
		dev_name(&card->gdev->dev), rc);
	return rc;
}
EXPORT_SYMBOL_GPL(qeth_core_hardsetup_card);

static inline int qeth_create_skb_frag(struct qdio_buffer_element *element,
		struct sk_buff **pskb, int offset, int *pfrag, int data_len)
{
	struct page *page = virt_to_page(element->addr);
	if (*pskb == NULL) {
		/* the upper protocol layers assume that there is data in the
		 * skb itself. Copy a small amount (64 bytes) to make them
		 * happy. */
		*pskb = dev_alloc_skb(64 + ETH_HLEN);
		if (!(*pskb))
			return -ENOMEM;
		skb_reserve(*pskb, ETH_HLEN);
		if (data_len <= 64) {
			memcpy(skb_put(*pskb, data_len), element->addr + offset,
				data_len);
		} else {
			get_page(page);
			memcpy(skb_put(*pskb, 64), element->addr + offset, 64);
			skb_fill_page_desc(*pskb, *pfrag, page, offset + 64,
				data_len - 64);
			(*pskb)->data_len += data_len - 64;
			(*pskb)->len      += data_len - 64;
			(*pskb)->truesize += data_len - 64;
			(*pfrag)++;
		}
	} else {
		get_page(page);
		skb_fill_page_desc(*pskb, *pfrag, page, offset, data_len);
		(*pskb)->data_len += data_len;
		(*pskb)->len      += data_len;
		(*pskb)->truesize += data_len;
		(*pfrag)++;
	}
	return 0;
}

struct sk_buff *qeth_core_get_next_skb(struct qeth_card *card,
		struct qdio_buffer *buffer,
		struct qdio_buffer_element **__element, int *__offset,
		struct qeth_hdr **hdr)
{
	struct qdio_buffer_element *element = *__element;
	int offset = *__offset;
	struct sk_buff *skb = NULL;
	int skb_len = 0;
	void *data_ptr;
	int data_len;
	int headroom = 0;
	int use_rx_sg = 0;
	int frag = 0;

	/* qeth_hdr must not cross element boundaries */
	if (element->length < offset + sizeof(struct qeth_hdr)) {
		if (qeth_is_last_sbale(element))
			return NULL;
		element++;
		offset = 0;
		if (element->length < sizeof(struct qeth_hdr))
			return NULL;
	}
	*hdr = element->addr + offset;

	offset += sizeof(struct qeth_hdr);
	switch ((*hdr)->hdr.l2.id) {
	case QETH_HEADER_TYPE_LAYER2:
		skb_len = (*hdr)->hdr.l2.pkt_length;
		break;
	case QETH_HEADER_TYPE_LAYER3:
		skb_len = (*hdr)->hdr.l3.length;
		if ((card->info.link_type == QETH_LINK_TYPE_LANE_TR) ||
		    (card->info.link_type == QETH_LINK_TYPE_HSTR))
			headroom = TR_HLEN;
		else
			headroom = ETH_HLEN;
		break;
	case QETH_HEADER_TYPE_OSN:
		skb_len = (*hdr)->hdr.osn.pdu_length;
		headroom = sizeof(struct qeth_hdr);
		break;
	default:
		break;
	}

	if (!skb_len)
		return NULL;

	if ((skb_len >= card->options.rx_sg_cb) &&
	    (!(card->info.type == QETH_CARD_TYPE_OSN)) &&
	    (!atomic_read(&card->force_alloc_skb))) {
		use_rx_sg = 1;
	} else {
		skb = dev_alloc_skb(skb_len + headroom);
		if (!skb)
			goto no_mem;
		if (headroom)
			skb_reserve(skb, headroom);
	}

	data_ptr = element->addr + offset;
	while (skb_len) {
		data_len = min(skb_len, (int)(element->length - offset));
		if (data_len) {
			if (use_rx_sg) {
				if (qeth_create_skb_frag(element, &skb, offset,
				    &frag, data_len))
					goto no_mem;
			} else {
				memcpy(skb_put(skb, data_len), data_ptr,
					data_len);
			}
		}
		skb_len -= data_len;
		if (skb_len) {
			if (qeth_is_last_sbale(element)) {
				QETH_DBF_TEXT(TRACE, 4, "unexeob");
				QETH_DBF_TEXT_(TRACE, 4, "%s",
					CARD_BUS_ID(card));
				QETH_DBF_TEXT(QERR, 2, "unexeob");
				QETH_DBF_TEXT_(QERR, 2, "%s",
					CARD_BUS_ID(card));
				QETH_DBF_HEX(MISC, 4, buffer, sizeof(*buffer));
				dev_kfree_skb_any(skb);
				card->stats.rx_errors++;
				return NULL;
			}
			element++;
			offset = 0;
			data_ptr = element->addr;
		} else {
			offset += data_len;
		}
	}
	*__element = element;
	*__offset = offset;
	if (use_rx_sg && card->options.performance_stats) {
		card->perf_stats.sg_skbs_rx++;
		card->perf_stats.sg_frags_rx += skb_shinfo(skb)->nr_frags;
	}
	return skb;
no_mem:
	if (net_ratelimit()) {
		QETH_DBF_TEXT(TRACE, 2, "noskbmem");
		QETH_DBF_TEXT_(TRACE, 2, "%s", CARD_BUS_ID(card));
	}
	card->stats.rx_dropped++;
	return NULL;
}
EXPORT_SYMBOL_GPL(qeth_core_get_next_skb);

static void qeth_unregister_dbf_views(void)
{
	int x;
	for (x = 0; x < QETH_DBF_INFOS; x++) {
		debug_unregister(qeth_dbf[x].id);
		qeth_dbf[x].id = NULL;
	}
}

void qeth_dbf_longtext(enum qeth_dbf_names dbf_nix, int level, char *fmt, ...)
{
	char dbf_txt_buf[32];
	va_list args;

	if (level > (qeth_dbf[dbf_nix].id)->level)
		return;
	va_start(args, fmt);
	vsnprintf(dbf_txt_buf, sizeof(dbf_txt_buf), fmt, args);
	va_end(args);
	debug_text_event(qeth_dbf[dbf_nix].id, level, dbf_txt_buf);
}
EXPORT_SYMBOL_GPL(qeth_dbf_longtext);

static int qeth_register_dbf_views(void)
{
	int ret;
	int x;

	for (x = 0; x < QETH_DBF_INFOS; x++) {
		/* register the areas */
		qeth_dbf[x].id = debug_register(qeth_dbf[x].name,
						qeth_dbf[x].pages,
						qeth_dbf[x].areas,
						qeth_dbf[x].len);
		if (qeth_dbf[x].id == NULL) {
			qeth_unregister_dbf_views();
			return -ENOMEM;
		}

		/* register a view */
		ret = debug_register_view(qeth_dbf[x].id, qeth_dbf[x].view);
		if (ret) {
			qeth_unregister_dbf_views();
			return ret;
		}

		/* set a passing level */
		debug_set_level(qeth_dbf[x].id, qeth_dbf[x].level);
	}

	return 0;
}

int qeth_core_load_discipline(struct qeth_card *card,
		enum qeth_discipline_id discipline)
{
	int rc = 0;
	switch (discipline) {
	case QETH_DISCIPLINE_LAYER3:
		card->discipline.ccwgdriver = try_then_request_module(
			symbol_get(qeth_l3_ccwgroup_driver),
			"qeth_l3");
		break;
	case QETH_DISCIPLINE_LAYER2:
		card->discipline.ccwgdriver = try_then_request_module(
			symbol_get(qeth_l2_ccwgroup_driver),
			"qeth_l2");
		break;
	}
	if (!card->discipline.ccwgdriver) {
		dev_err(&card->gdev->dev, "There is no kernel module to "
			"support discipline %d\n", discipline);
		rc = -EINVAL;
	}
	return rc;
}

void qeth_core_free_discipline(struct qeth_card *card)
{
	if (card->options.layer2)
		symbol_put(qeth_l2_ccwgroup_driver);
	else
		symbol_put(qeth_l3_ccwgroup_driver);
	card->discipline.ccwgdriver = NULL;
}

static void qeth_determine_capabilities(struct qeth_card *card)
{
	int rc;
	int length;
	char *prcd;

	QETH_DBF_TEXT(SETUP, 2, "detcapab");
	rc = ccw_device_set_online(CARD_DDEV(card));
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "3err%d", rc);
		goto out;
	}


	rc = qeth_read_conf_data(card, (void **) &prcd, &length);
	if (rc) {
		QETH_DBF_MESSAGE(2, "%s qeth_read_conf_data returned %i\n",
			dev_name(&card->gdev->dev), rc);
		QETH_DBF_TEXT_(SETUP, 2, "5err%d", rc);
		goto out_offline;
	}
	qeth_configure_unitaddr(card, prcd);
	qeth_configure_blkt_default(card, prcd);
	kfree(prcd);

	rc = qdio_get_ssqd_desc(CARD_DDEV(card), &card->ssqd);
	if (rc)
		QETH_DBF_TEXT_(SETUP, 2, "6err%d", rc);

out_offline:
	ccw_device_set_offline(CARD_DDEV(card));
out:
	return;
}

static int qeth_core_probe_device(struct ccwgroup_device *gdev)
{
	struct qeth_card *card;
	struct device *dev;
	int rc;
	unsigned long flags;

	QETH_DBF_TEXT(SETUP, 2, "probedev");

	dev = &gdev->dev;
	if (!get_device(dev))
		return -ENODEV;

	QETH_DBF_TEXT_(SETUP, 2, "%s", dev_name(&gdev->dev));

	card = qeth_alloc_card();
	if (!card) {
		QETH_DBF_TEXT_(SETUP, 2, "1err%d", -ENOMEM);
		rc = -ENOMEM;
		goto err_dev;
	}
	card->read.ccwdev  = gdev->cdev[0];
	card->write.ccwdev = gdev->cdev[1];
	card->data.ccwdev  = gdev->cdev[2];
	dev_set_drvdata(&gdev->dev, card);
	card->gdev = gdev;
	gdev->cdev[0]->handler = qeth_irq;
	gdev->cdev[1]->handler = qeth_irq;
	gdev->cdev[2]->handler = qeth_irq;

	rc = qeth_determine_card_type(card);
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "3err%d", rc);
		goto err_card;
	}
	rc = qeth_setup_card(card);
	if (rc) {
		QETH_DBF_TEXT_(SETUP, 2, "2err%d", rc);
		goto err_card;
	}

	if (card->info.type == QETH_CARD_TYPE_OSN)
		rc = qeth_core_create_osn_attributes(dev);
	else
		rc = qeth_core_create_device_attributes(dev);
	if (rc)
		goto err_card;
	switch (card->info.type) {
	case QETH_CARD_TYPE_OSN:
	case QETH_CARD_TYPE_OSM:
		rc = qeth_core_load_discipline(card, QETH_DISCIPLINE_LAYER2);
		if (rc)
			goto err_attr;
		rc = card->discipline.ccwgdriver->probe(card->gdev);
		if (rc)
			goto err_disc;
	case QETH_CARD_TYPE_OSD:
	case QETH_CARD_TYPE_OSX:
	default:
		break;
	}

	write_lock_irqsave(&qeth_core_card_list.rwlock, flags);
	list_add_tail(&card->list, &qeth_core_card_list.list);
	write_unlock_irqrestore(&qeth_core_card_list.rwlock, flags);

	qeth_determine_capabilities(card);
	return 0;

err_disc:
	qeth_core_free_discipline(card);
err_attr:
	if (card->info.type == QETH_CARD_TYPE_OSN)
		qeth_core_remove_osn_attributes(dev);
	else
		qeth_core_remove_device_attributes(dev);
err_card:
	qeth_core_free_card(card);
err_dev:
	put_device(dev);
	return rc;
}

static void qeth_core_remove_device(struct ccwgroup_device *gdev)
{
	unsigned long flags;
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);

	QETH_DBF_TEXT(SETUP, 2, "removedv");
	if (card->discipline.ccwgdriver) {
		card->discipline.ccwgdriver->remove(gdev);
		qeth_core_free_discipline(card);
	}

	if (card->info.type == QETH_CARD_TYPE_OSN) {
		qeth_core_remove_osn_attributes(&gdev->dev);
	} else {
		qeth_core_remove_device_attributes(&gdev->dev);
	}
	write_lock_irqsave(&qeth_core_card_list.rwlock, flags);
	list_del(&card->list);
	write_unlock_irqrestore(&qeth_core_card_list.rwlock, flags);
	qeth_core_free_card(card);
	dev_set_drvdata(&gdev->dev, NULL);
	put_device(&gdev->dev);
	return;
}

static int qeth_core_set_online(struct ccwgroup_device *gdev)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);
	int rc = 0;
	int def_discipline;

	if (!card->discipline.ccwgdriver) {
		if (card->info.type == QETH_CARD_TYPE_IQD)
			def_discipline = QETH_DISCIPLINE_LAYER3;
		else
			def_discipline = QETH_DISCIPLINE_LAYER2;
		rc = qeth_core_load_discipline(card, def_discipline);
		if (rc)
			goto err;
		rc = card->discipline.ccwgdriver->probe(card->gdev);
		if (rc)
			goto err;
	}
	rc = card->discipline.ccwgdriver->set_online(gdev);
err:
	return rc;
}

static int qeth_core_set_offline(struct ccwgroup_device *gdev)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);
	return card->discipline.ccwgdriver->set_offline(gdev);
}

static void qeth_core_shutdown(struct ccwgroup_device *gdev)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);
	if (card->discipline.ccwgdriver &&
	    card->discipline.ccwgdriver->shutdown)
		card->discipline.ccwgdriver->shutdown(gdev);
}

static int qeth_core_prepare(struct ccwgroup_device *gdev)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);
	if (card->discipline.ccwgdriver &&
	    card->discipline.ccwgdriver->prepare)
		return card->discipline.ccwgdriver->prepare(gdev);
	return 0;
}

static void qeth_core_complete(struct ccwgroup_device *gdev)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);
	if (card->discipline.ccwgdriver &&
	    card->discipline.ccwgdriver->complete)
		card->discipline.ccwgdriver->complete(gdev);
}

static int qeth_core_freeze(struct ccwgroup_device *gdev)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);
	if (card->discipline.ccwgdriver &&
	    card->discipline.ccwgdriver->freeze)
		return card->discipline.ccwgdriver->freeze(gdev);
	return 0;
}

static int qeth_core_thaw(struct ccwgroup_device *gdev)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);
	if (card->discipline.ccwgdriver &&
	    card->discipline.ccwgdriver->thaw)
		return card->discipline.ccwgdriver->thaw(gdev);
	return 0;
}

static int qeth_core_restore(struct ccwgroup_device *gdev)
{
	struct qeth_card *card = dev_get_drvdata(&gdev->dev);
	if (card->discipline.ccwgdriver &&
	    card->discipline.ccwgdriver->restore)
		return card->discipline.ccwgdriver->restore(gdev);
	return 0;
}

static struct ccwgroup_driver qeth_core_ccwgroup_driver = {
	.owner = THIS_MODULE,
	.name = "qeth",
	.driver_id = 0xD8C5E3C8,
	.probe = qeth_core_probe_device,
	.remove = qeth_core_remove_device,
	.set_online = qeth_core_set_online,
	.set_offline = qeth_core_set_offline,
	.shutdown = qeth_core_shutdown,
	.prepare = qeth_core_prepare,
	.complete = qeth_core_complete,
	.freeze = qeth_core_freeze,
	.thaw = qeth_core_thaw,
	.restore = qeth_core_restore,
};

static ssize_t
qeth_core_driver_group_store(struct device_driver *ddrv, const char *buf,
			   size_t count)
{
	int err;
	err = qeth_core_driver_group(buf, qeth_core_root_dev,
					qeth_core_ccwgroup_driver.driver_id);
	if (err)
		return err;
	else
		return count;
}

static DRIVER_ATTR(group, 0200, NULL, qeth_core_driver_group_store);

static struct {
	const char str[ETH_GSTRING_LEN];
} qeth_ethtool_stats_keys[] = {
/*  0 */{"rx skbs"},
	{"rx buffers"},
	{"tx skbs"},
	{"tx buffers"},
	{"tx skbs no packing"},
	{"tx buffers no packing"},
	{"tx skbs packing"},
	{"tx buffers packing"},
	{"tx sg skbs"},
	{"tx sg frags"},
/* 10 */{"rx sg skbs"},
	{"rx sg frags"},
	{"rx sg page allocs"},
	{"tx large kbytes"},
	{"tx large count"},
	{"tx pk state ch n->p"},
	{"tx pk state ch p->n"},
	{"tx pk watermark low"},
	{"tx pk watermark high"},
	{"queue 0 buffer usage"},
/* 20 */{"queue 1 buffer usage"},
	{"queue 2 buffer usage"},
	{"queue 3 buffer usage"},
	{"rx handler time"},
	{"rx handler count"},
	{"rx do_QDIO time"},
	{"rx do_QDIO count"},
	{"tx handler time"},
	{"tx handler count"},
	{"tx time"},
/* 30 */{"tx count"},
	{"tx do_QDIO time"},
	{"tx do_QDIO count"},
	{"tx csum"},
	{"tx lin"},
};

int qeth_core_get_sset_count(struct net_device *dev, int stringset)
{
	switch (stringset) {
	case ETH_SS_STATS:
		return (sizeof(qeth_ethtool_stats_keys) / ETH_GSTRING_LEN);
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(qeth_core_get_sset_count);

void qeth_core_get_ethtool_stats(struct net_device *dev,
		struct ethtool_stats *stats, u64 *data)
{
	struct qeth_card *card = dev->ml_priv;
	data[0] = card->stats.rx_packets -
				card->perf_stats.initial_rx_packets;
	data[1] = card->perf_stats.bufs_rec;
	data[2] = card->stats.tx_packets -
				card->perf_stats.initial_tx_packets;
	data[3] = card->perf_stats.bufs_sent;
	data[4] = card->stats.tx_packets - card->perf_stats.initial_tx_packets
			- card->perf_stats.skbs_sent_pack;
	data[5] = card->perf_stats.bufs_sent - card->perf_stats.bufs_sent_pack;
	data[6] = card->perf_stats.skbs_sent_pack;
	data[7] = card->perf_stats.bufs_sent_pack;
	data[8] = card->perf_stats.sg_skbs_sent;
	data[9] = card->perf_stats.sg_frags_sent;
	data[10] = card->perf_stats.sg_skbs_rx;
	data[11] = card->perf_stats.sg_frags_rx;
	data[12] = card->perf_stats.sg_alloc_page_rx;
	data[13] = (card->perf_stats.large_send_bytes >> 10);
	data[14] = card->perf_stats.large_send_cnt;
	data[15] = card->perf_stats.sc_dp_p;
	data[16] = card->perf_stats.sc_p_dp;
	data[17] = QETH_LOW_WATERMARK_PACK;
	data[18] = QETH_HIGH_WATERMARK_PACK;
	data[19] = atomic_read(&card->qdio.out_qs[0]->used_buffers);
	data[20] = (card->qdio.no_out_queues > 1) ?
			atomic_read(&card->qdio.out_qs[1]->used_buffers) : 0;
	data[21] = (card->qdio.no_out_queues > 2) ?
			atomic_read(&card->qdio.out_qs[2]->used_buffers) : 0;
	data[22] = (card->qdio.no_out_queues > 3) ?
			atomic_read(&card->qdio.out_qs[3]->used_buffers) : 0;
	data[23] = card->perf_stats.inbound_time;
	data[24] = card->perf_stats.inbound_cnt;
	data[25] = card->perf_stats.inbound_do_qdio_time;
	data[26] = card->perf_stats.inbound_do_qdio_cnt;
	data[27] = card->perf_stats.outbound_handler_time;
	data[28] = card->perf_stats.outbound_handler_cnt;
	data[29] = card->perf_stats.outbound_time;
	data[30] = card->perf_stats.outbound_cnt;
	data[31] = card->perf_stats.outbound_do_qdio_time;
	data[32] = card->perf_stats.outbound_do_qdio_cnt;
	data[33] = card->perf_stats.tx_csum;
	data[34] = card->perf_stats.tx_lin;
}
EXPORT_SYMBOL_GPL(qeth_core_get_ethtool_stats);

void qeth_core_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(data, &qeth_ethtool_stats_keys,
			sizeof(qeth_ethtool_stats_keys));
		break;
	default:
		WARN_ON(1);
		break;
	}
}
EXPORT_SYMBOL_GPL(qeth_core_get_strings);

void qeth_core_get_drvinfo(struct net_device *dev,
		struct ethtool_drvinfo *info)
{
	struct qeth_card *card = dev->ml_priv;
	if (card->options.layer2)
		strcpy(info->driver, "qeth_l2");
	else
		strcpy(info->driver, "qeth_l3");

	strcpy(info->version, "1.0");
	strcpy(info->fw_version, card->info.mcl_level);
	sprintf(info->bus_info, "%s/%s/%s",
			CARD_RDEV_ID(card),
			CARD_WDEV_ID(card),
			CARD_DDEV_ID(card));
}
EXPORT_SYMBOL_GPL(qeth_core_get_drvinfo);

int qeth_core_ethtool_get_settings(struct net_device *netdev,
					struct ethtool_cmd *ecmd)
{
	struct qeth_card *card = netdev->ml_priv;
	enum qeth_link_types link_type;

	if ((card->info.type == QETH_CARD_TYPE_IQD) || (card->info.guestlan))
		link_type = QETH_LINK_TYPE_10GBIT_ETH;
	else
		link_type = card->info.link_type;

	ecmd->transceiver = XCVR_INTERNAL;
	ecmd->supported = SUPPORTED_Autoneg;
	ecmd->advertising = ADVERTISED_Autoneg;
	ecmd->duplex = DUPLEX_FULL;
	ecmd->autoneg = AUTONEG_ENABLE;

	switch (link_type) {
	case QETH_LINK_TYPE_FAST_ETH:
	case QETH_LINK_TYPE_LANE_ETH100:
		ecmd->supported |= SUPPORTED_10baseT_Half |
					SUPPORTED_10baseT_Full |
					SUPPORTED_100baseT_Half |
					SUPPORTED_100baseT_Full |
					SUPPORTED_TP;
		ecmd->advertising |= ADVERTISED_10baseT_Half |
					ADVERTISED_10baseT_Full |
					ADVERTISED_100baseT_Half |
					ADVERTISED_100baseT_Full |
					ADVERTISED_TP;
		ecmd->speed = SPEED_100;
		ecmd->port = PORT_TP;
		break;

	case QETH_LINK_TYPE_GBIT_ETH:
	case QETH_LINK_TYPE_LANE_ETH1000:
		ecmd->supported |= SUPPORTED_10baseT_Half |
					SUPPORTED_10baseT_Full |
					SUPPORTED_100baseT_Half |
					SUPPORTED_100baseT_Full |
					SUPPORTED_1000baseT_Half |
					SUPPORTED_1000baseT_Full |
					SUPPORTED_FIBRE;
		ecmd->advertising |= ADVERTISED_10baseT_Half |
					ADVERTISED_10baseT_Full |
					ADVERTISED_100baseT_Half |
					ADVERTISED_100baseT_Full |
					ADVERTISED_1000baseT_Half |
					ADVERTISED_1000baseT_Full |
					ADVERTISED_FIBRE;
		ecmd->speed = SPEED_1000;
		ecmd->port = PORT_FIBRE;
		break;

	case QETH_LINK_TYPE_10GBIT_ETH:
		ecmd->supported |= SUPPORTED_10baseT_Half |
					SUPPORTED_10baseT_Full |
					SUPPORTED_100baseT_Half |
					SUPPORTED_100baseT_Full |
					SUPPORTED_1000baseT_Half |
					SUPPORTED_1000baseT_Full |
					SUPPORTED_10000baseT_Full |
					SUPPORTED_FIBRE;
		ecmd->advertising |= ADVERTISED_10baseT_Half |
					ADVERTISED_10baseT_Full |
					ADVERTISED_100baseT_Half |
					ADVERTISED_100baseT_Full |
					ADVERTISED_1000baseT_Half |
					ADVERTISED_1000baseT_Full |
					ADVERTISED_10000baseT_Full |
					ADVERTISED_FIBRE;
		ecmd->speed = SPEED_10000;
		ecmd->port = PORT_FIBRE;
		break;

	default:
		ecmd->supported |= SUPPORTED_10baseT_Half |
					SUPPORTED_10baseT_Full |
					SUPPORTED_TP;
		ecmd->advertising |= ADVERTISED_10baseT_Half |
					ADVERTISED_10baseT_Full |
					ADVERTISED_TP;
		ecmd->speed = SPEED_10;
		ecmd->port = PORT_TP;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(qeth_core_ethtool_get_settings);

static int __init qeth_core_init(void)
{
	int rc;

	pr_info("loading core functions\n");
	INIT_LIST_HEAD(&qeth_core_card_list.list);
	rwlock_init(&qeth_core_card_list.rwlock);

	rc = qeth_register_dbf_views();
	if (rc)
		goto out_err;
	rc = ccw_driver_register(&qeth_ccw_driver);
	if (rc)
		goto ccw_err;
	rc = ccwgroup_driver_register(&qeth_core_ccwgroup_driver);
	if (rc)
		goto ccwgroup_err;
	rc = driver_create_file(&qeth_core_ccwgroup_driver.driver,
				&driver_attr_group);
	if (rc)
		goto driver_err;
	qeth_core_root_dev = root_device_register("qeth");
	rc = IS_ERR(qeth_core_root_dev) ? PTR_ERR(qeth_core_root_dev) : 0;
	if (rc)
		goto register_err;

	qeth_core_header_cache = kmem_cache_create("qeth_hdr",
			sizeof(struct qeth_hdr) + ETH_HLEN, 64, 0, NULL);
	if (!qeth_core_header_cache) {
		rc = -ENOMEM;
		goto slab_err;
	}

	return 0;
slab_err:
	root_device_unregister(qeth_core_root_dev);
register_err:
	driver_remove_file(&qeth_core_ccwgroup_driver.driver,
			   &driver_attr_group);
driver_err:
	ccwgroup_driver_unregister(&qeth_core_ccwgroup_driver);
ccwgroup_err:
	ccw_driver_unregister(&qeth_ccw_driver);
ccw_err:
	QETH_DBF_MESSAGE(2, "Initialization failed with code %d\n", rc);
	qeth_unregister_dbf_views();
out_err:
	pr_err("Initializing the qeth device driver failed\n");
	return rc;
}

static void __exit qeth_core_exit(void)
{
	root_device_unregister(qeth_core_root_dev);
	driver_remove_file(&qeth_core_ccwgroup_driver.driver,
			   &driver_attr_group);
	ccwgroup_driver_unregister(&qeth_core_ccwgroup_driver);
	ccw_driver_unregister(&qeth_ccw_driver);
	kmem_cache_destroy(qeth_core_header_cache);
	qeth_unregister_dbf_views();
	pr_info("core functions removed\n");
}

module_init(qeth_core_init);
module_exit(qeth_core_exit);
MODULE_AUTHOR("Frank Blaschka <frank.blaschka@de.ibm.com>");
MODULE_DESCRIPTION("qeth core functions");
MODULE_LICENSE("GPL");
