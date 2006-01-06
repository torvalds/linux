/*
 *
 * linux/drivers/s390/net/qeth_main.c ($Revision: 1.251 $)
 *
 * Linux on zSeries OSA Express and HiperSockets support
 *
 * Copyright 2000,2003 IBM Corporation
 *
 *    Author(s): Original Code written by
 *			  Utz Bacher (utz.bacher@de.ibm.com)
 *		 Rewritten by
 *			  Frank Pavlic (fpavlic@de.ibm.com) and
 *		 	  Thomas Spatzier <tspat@de.ibm.com>
 *
 *    $Revision: 1.251 $	 $Date: 2005/05/04 20:19:18 $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/ip.h>
#include <linux/inetdevice.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/tcp.h>
#include <linux/icmp.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/igmp.h>
#include <linux/init.h>
#include <linux/reboot.h>
#include <linux/mii.h>
#include <linux/rcupdate.h>
#include <linux/ethtool.h>

#include <net/arp.h>
#include <net/ip.h>
#include <net/route.h>

#include <asm/ebcdic.h>
#include <asm/io.h>
#include <asm/qeth.h>
#include <asm/timex.h>
#include <asm/semaphore.h>
#include <asm/uaccess.h>
#include <asm/s390_rdev.h>

#include "qeth.h"
#include "qeth_mpc.h"
#include "qeth_fs.h"
#include "qeth_eddp.h"
#include "qeth_tso.h"

#define VERSION_QETH_C "$Revision: 1.251 $"
static const char *version = "qeth S/390 OSA-Express driver";

/**
 * Debug Facility Stuff
 */
static debug_info_t *qeth_dbf_setup = NULL;
static debug_info_t *qeth_dbf_data = NULL;
static debug_info_t *qeth_dbf_misc = NULL;
static debug_info_t *qeth_dbf_control = NULL;
debug_info_t *qeth_dbf_trace = NULL;
static debug_info_t *qeth_dbf_sense = NULL;
static debug_info_t *qeth_dbf_qerr = NULL;

DEFINE_PER_CPU(char[256], qeth_dbf_txt_buf);

/**
 * some more definitions and declarations
 */
static unsigned int known_devices[][10] = QETH_MODELLIST_ARRAY;

/* list of our cards */
struct qeth_card_list_struct qeth_card_list;
/*process list want to be notified*/
spinlock_t qeth_notify_lock;
struct list_head qeth_notify_list;

static void qeth_send_control_data_cb(struct qeth_channel *,
				      struct qeth_cmd_buffer *);

/**
 * here we go with function implementation
 */
static void
qeth_init_qdio_info(struct qeth_card *card);

static int
qeth_init_qdio_queues(struct qeth_card *card);

static int
qeth_alloc_qdio_buffers(struct qeth_card *card);

static void
qeth_free_qdio_buffers(struct qeth_card *);

static void
qeth_clear_qdio_buffers(struct qeth_card *);

static void
qeth_clear_ip_list(struct qeth_card *, int, int);

static void
qeth_clear_ipacmd_list(struct qeth_card *);

static int
qeth_qdio_clear_card(struct qeth_card *, int);

static void
qeth_clear_working_pool_list(struct qeth_card *);

static void
qeth_clear_cmd_buffers(struct qeth_channel *);

static int
qeth_stop(struct net_device *);

static void
qeth_clear_ipato_list(struct qeth_card *);

static int
qeth_is_addr_covered_by_ipato(struct qeth_card *, struct qeth_ipaddr *);

static void
qeth_irq_tasklet(unsigned long);

static int
qeth_set_online(struct ccwgroup_device *);

static int
__qeth_set_online(struct ccwgroup_device *gdev, int recovery_mode);

static struct qeth_ipaddr *
qeth_get_addr_buffer(enum qeth_prot_versions);

static void
qeth_set_multicast_list(struct net_device *);

static void
qeth_setadp_promisc_mode(struct qeth_card *);

static void
qeth_notify_processes(void)
{
	/*notify all  registered processes */
	struct qeth_notify_list_struct *n_entry;

	QETH_DBF_TEXT(trace,3,"procnoti");
	spin_lock(&qeth_notify_lock);
	list_for_each_entry(n_entry, &qeth_notify_list, list) {
		send_sig(n_entry->signum, n_entry->task, 1);
	}
	spin_unlock(&qeth_notify_lock);

}
int
qeth_notifier_unregister(struct task_struct *p)
{
	struct qeth_notify_list_struct *n_entry, *tmp;

	QETH_DBF_TEXT(trace, 2, "notunreg");
	spin_lock(&qeth_notify_lock);
	list_for_each_entry_safe(n_entry, tmp, &qeth_notify_list, list) {
		if (n_entry->task == p) {
			list_del(&n_entry->list);
			kfree(n_entry);
			goto out;
		}
	}
out:
	spin_unlock(&qeth_notify_lock);
	return 0;
}
int
qeth_notifier_register(struct task_struct *p, int signum)
{
	struct qeth_notify_list_struct *n_entry;

	/*check first if entry already exists*/
	spin_lock(&qeth_notify_lock);
	list_for_each_entry(n_entry, &qeth_notify_list, list) {
		if (n_entry->task == p) {
			n_entry->signum = signum;
			spin_unlock(&qeth_notify_lock);
			return 0;
		}
	}
	spin_unlock(&qeth_notify_lock);

	n_entry = (struct qeth_notify_list_struct *)
		kmalloc(sizeof(struct qeth_notify_list_struct),GFP_KERNEL);
	if (!n_entry)
		return -ENOMEM;
	n_entry->task = p;
	n_entry->signum = signum;
	spin_lock(&qeth_notify_lock);
	list_add(&n_entry->list,&qeth_notify_list);
	spin_unlock(&qeth_notify_lock);
	return 0;
}


/**
 * free channel command buffers
 */
static void
qeth_clean_channel(struct qeth_channel *channel)
{
	int cnt;

	QETH_DBF_TEXT(setup, 2, "freech");
	for (cnt = 0; cnt < QETH_CMD_BUFFER_NO; cnt++)
		kfree(channel->iob[cnt].data);
}

/**
 * free card
 */
static void
qeth_free_card(struct qeth_card *card)
{

	QETH_DBF_TEXT(setup, 2, "freecrd");
	QETH_DBF_HEX(setup, 2, &card, sizeof(void *));
	qeth_clean_channel(&card->read);
	qeth_clean_channel(&card->write);
	if (card->dev)
		free_netdev(card->dev);
	qeth_clear_ip_list(card, 0, 0);
	qeth_clear_ipato_list(card);
	kfree(card->ip_tbd_list);
	qeth_free_qdio_buffers(card);
	kfree(card);
}

/**
 * alloc memory for command buffer per channel
 */
static int
qeth_setup_channel(struct qeth_channel *channel)
{
	int cnt;

	QETH_DBF_TEXT(setup, 2, "setupch");
	for (cnt=0; cnt < QETH_CMD_BUFFER_NO; cnt++) {
		channel->iob[cnt].data = (char *)
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
	channel->irq_tasklet.data = (unsigned long) channel;
	channel->irq_tasklet.func = qeth_irq_tasklet;
	return 0;
}

/**
 * alloc memory for card structure
 */
static struct qeth_card *
qeth_alloc_card(void)
{
	struct qeth_card *card;

	QETH_DBF_TEXT(setup, 2, "alloccrd");
	card = (struct qeth_card *) kmalloc(sizeof(struct qeth_card),
					    GFP_DMA|GFP_KERNEL);
	if (!card)
		return NULL;
	QETH_DBF_HEX(setup, 2, &card, sizeof(void *));
	memset(card, 0, sizeof(struct qeth_card));
	if (qeth_setup_channel(&card->read)) {
		kfree(card);
		return NULL;
	}
	if (qeth_setup_channel(&card->write)) {
		qeth_clean_channel(&card->read);
		kfree(card);
		return NULL;
	}
	return card;
}

static long
__qeth_check_irb_error(struct ccw_device *cdev, struct irb *irb)
{
	if (!IS_ERR(irb))
		return 0;

	switch (PTR_ERR(irb)) {
	case -EIO:
		PRINT_WARN("i/o-error on device %s\n", cdev->dev.bus_id);
		QETH_DBF_TEXT(trace, 2, "ckirberr");
		QETH_DBF_TEXT_(trace, 2, "  rc%d", -EIO);
		break;
	case -ETIMEDOUT:
		PRINT_WARN("timeout on device %s\n", cdev->dev.bus_id);
		QETH_DBF_TEXT(trace, 2, "ckirberr");
		QETH_DBF_TEXT_(trace, 2, "  rc%d", -ETIMEDOUT);
		break;
	default:
		PRINT_WARN("unknown error %ld on device %s\n", PTR_ERR(irb),
			   cdev->dev.bus_id);
		QETH_DBF_TEXT(trace, 2, "ckirberr");
		QETH_DBF_TEXT(trace, 2, "  rc???");
	}
	return PTR_ERR(irb);
}

static int
qeth_get_problem(struct ccw_device *cdev, struct irb *irb)
{
	int dstat,cstat;
	char *sense;

	sense = (char *) irb->ecw;
	cstat = irb->scsw.cstat;
	dstat = irb->scsw.dstat;

	if (cstat & (SCHN_STAT_CHN_CTRL_CHK | SCHN_STAT_INTF_CTRL_CHK |
		     SCHN_STAT_CHN_DATA_CHK | SCHN_STAT_CHAIN_CHECK |
		     SCHN_STAT_PROT_CHECK | SCHN_STAT_PROG_CHECK)) {
		QETH_DBF_TEXT(trace,2, "CGENCHK");
		PRINT_WARN("check on device %s, dstat=x%x, cstat=x%x ",
			   cdev->dev.bus_id, dstat, cstat);
		HEXDUMP16(WARN, "irb: ", irb);
		HEXDUMP16(WARN, "irb: ", ((char *) irb) + 32);
		return 1;
	}

	if (dstat & DEV_STAT_UNIT_CHECK) {
		if (sense[SENSE_RESETTING_EVENT_BYTE] &
		    SENSE_RESETTING_EVENT_FLAG) {
			QETH_DBF_TEXT(trace,2,"REVIND");
			return 1;
		}
		if (sense[SENSE_COMMAND_REJECT_BYTE] &
		    SENSE_COMMAND_REJECT_FLAG) {
			QETH_DBF_TEXT(trace,2,"CMDREJi");
			return 0;
		}
		if ((sense[2] == 0xaf) && (sense[3] == 0xfe)) {
			QETH_DBF_TEXT(trace,2,"AFFE");
			return 1;
		}
		if ((!sense[0]) && (!sense[1]) && (!sense[2]) && (!sense[3])) {
			QETH_DBF_TEXT(trace,2,"ZEROSEN");
			return 0;
		}
		QETH_DBF_TEXT(trace,2,"DGENCHK");
			return 1;
	}
	return 0;
}
static int qeth_issue_next_read(struct qeth_card *);

/**
 * interrupt handler
 */
static void
qeth_irq(struct ccw_device *cdev, unsigned long intparm, struct irb *irb)
{
	int rc;
	int cstat,dstat;
	struct qeth_cmd_buffer *buffer;
	struct qeth_channel *channel;
	struct qeth_card *card;

	QETH_DBF_TEXT(trace,5,"irq");

	if (__qeth_check_irb_error(cdev, irb))
		return;
	cstat = irb->scsw.cstat;
	dstat = irb->scsw.dstat;

	card = CARD_FROM_CDEV(cdev);
	if (!card)
		return;

	if (card->read.ccwdev == cdev){
		channel = &card->read;
		QETH_DBF_TEXT(trace,5,"read");
	} else if (card->write.ccwdev == cdev) {
		channel = &card->write;
		QETH_DBF_TEXT(trace,5,"write");
	} else {
		channel = &card->data;
		QETH_DBF_TEXT(trace,5,"data");
	}
	atomic_set(&channel->irq_pending, 0);

	if (irb->scsw.fctl & (SCSW_FCTL_CLEAR_FUNC))
		channel->state = CH_STATE_STOPPED;

	if (irb->scsw.fctl & (SCSW_FCTL_HALT_FUNC))
		channel->state = CH_STATE_HALTED;

	/*let's wake up immediately on data channel*/
	if ((channel == &card->data) && (intparm != 0))
		goto out;

	if (intparm == QETH_CLEAR_CHANNEL_PARM) {
		QETH_DBF_TEXT(trace, 6, "clrchpar");
		/* we don't have to handle this further */
		intparm = 0;
	}
	if (intparm == QETH_HALT_CHANNEL_PARM) {
		QETH_DBF_TEXT(trace, 6, "hltchpar");
		/* we don't have to handle this further */
		intparm = 0;
	}
	if ((dstat & DEV_STAT_UNIT_EXCEP) ||
	    (dstat & DEV_STAT_UNIT_CHECK) ||
	    (cstat)) {
		if (irb->esw.esw0.erw.cons) {
			/* TODO: we should make this s390dbf */
			PRINT_WARN("sense data available on channel %s.\n",
				   CHANNEL_ID(channel));
			PRINT_WARN(" cstat 0x%X\n dstat 0x%X\n", cstat, dstat);
			HEXDUMP16(WARN,"irb: ",irb);
			HEXDUMP16(WARN,"sense data: ",irb->ecw);
		}
		rc = qeth_get_problem(cdev,irb);
		if (rc) {
			qeth_schedule_recovery(card);
			goto out;
		}
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

	tasklet_schedule(&channel->irq_tasklet);
	return;
out:
	wake_up(&card->wait_q);
}

/**
 * tasklet function scheduled from irq handler
 */
static void
qeth_irq_tasklet(unsigned long data)
{
	struct qeth_card *card;
	struct qeth_channel *channel;
	struct qeth_cmd_buffer *iob;
	__u8 index;

	QETH_DBF_TEXT(trace,5,"irqtlet");
	channel = (struct qeth_channel *) data;
	iob = channel->iob;
	index = channel->buf_no;
	card = CARD_FROM_CDEV(channel->ccwdev);
	while (iob[index].state == BUF_STATE_PROCESSED) {
		if (iob[index].callback !=NULL) {
			iob[index].callback(channel,iob + index);
		}
		index = (index + 1) % QETH_CMD_BUFFER_NO;
	}
	channel->buf_no = index;
	wake_up(&card->wait_q);
}

static int qeth_stop_card(struct qeth_card *, int);

static int
__qeth_set_offline(struct ccwgroup_device *cgdev, int recovery_mode)
{
	struct qeth_card *card = (struct qeth_card *) cgdev->dev.driver_data;
	int rc = 0, rc2 = 0, rc3 = 0;
	enum qeth_card_states recover_flag;

	QETH_DBF_TEXT(setup, 3, "setoffl");
	QETH_DBF_HEX(setup, 3, &card, sizeof(void *));
	
	netif_carrier_off(card->dev);
	recover_flag = card->state;
	if (qeth_stop_card(card, recovery_mode) == -ERESTARTSYS){
		PRINT_WARN("Stopping card %s interrupted by user!\n",
			   CARD_BUS_ID(card));
		return -ERESTARTSYS;
	}
	rc  = ccw_device_set_offline(CARD_DDEV(card));
	rc2 = ccw_device_set_offline(CARD_WDEV(card));
	rc3 = ccw_device_set_offline(CARD_RDEV(card));
	if (!rc)
		rc = (rc2) ? rc2 : rc3;
	if (rc)
		QETH_DBF_TEXT_(setup, 2, "1err%d", rc);
	if (recover_flag == CARD_STATE_UP)
		card->state = CARD_STATE_RECOVER;
	qeth_notify_processes();
	return 0;
}

static int
qeth_set_offline(struct ccwgroup_device *cgdev)
{
	return  __qeth_set_offline(cgdev, 0);
}

static int
qeth_wait_for_threads(struct qeth_card *card, unsigned long threads);


static void
qeth_remove_device(struct ccwgroup_device *cgdev)
{
	struct qeth_card *card = (struct qeth_card *) cgdev->dev.driver_data;
	unsigned long flags;

	QETH_DBF_TEXT(setup, 3, "rmdev");
	QETH_DBF_HEX(setup, 3, &card, sizeof(void *));

	if (!card)
		return;

	if (qeth_wait_for_threads(card, 0xffffffff))
		return;

	if (cgdev->state == CCWGROUP_ONLINE){
		card->use_hard_stop = 1;
		qeth_set_offline(cgdev);
	}
	/* remove form our internal list */
	write_lock_irqsave(&qeth_card_list.rwlock, flags);
	list_del(&card->list);
	write_unlock_irqrestore(&qeth_card_list.rwlock, flags);
	if (card->dev)
		unregister_netdev(card->dev);
	qeth_remove_device_attributes(&cgdev->dev);
	qeth_free_card(card);
	cgdev->dev.driver_data = NULL;
	put_device(&cgdev->dev);
}

static int
qeth_register_addr_entry(struct qeth_card *, struct qeth_ipaddr *);
static int
qeth_deregister_addr_entry(struct qeth_card *, struct qeth_ipaddr *);

/**
 * Add/remove address to/from card's ip list, i.e. try to add or remove
 * reference to/from an IP address that is already registered on the card.
 * Returns:
 * 	0  address was on card and its reference count has been adjusted,
 * 	   but is still > 0, so nothing has to be done
 * 	   also returns 0 if card was not on card and the todo was to delete
 * 	   the address -> there is also nothing to be done
 * 	1  address was not on card and the todo is to add it to the card's ip
 * 	   list
 * 	-1 address was on card and its reference count has been decremented
 * 	   to <= 0 by the todo -> address must be removed from card
 */
static int
__qeth_ref_ip_on_card(struct qeth_card *card, struct qeth_ipaddr *todo,
		      struct qeth_ipaddr **__addr)
{
	struct qeth_ipaddr *addr;
	int found = 0;

	list_for_each_entry(addr, &card->ip_list, entry) {
		if (card->options.layer2) {
			if ((addr->type == todo->type) &&
			    (memcmp(&addr->mac, &todo->mac, 
				    OSA_ADDR_LEN) == 0)) {
				found = 1;
				break;
			}
			continue;
		} 
		if ((addr->proto     == QETH_PROT_IPV4)  &&
		    (todo->proto     == QETH_PROT_IPV4)  &&
		    (addr->type      == todo->type)      &&
		    (addr->u.a4.addr == todo->u.a4.addr) &&
		    (addr->u.a4.mask == todo->u.a4.mask)) {
			found = 1;
			break;
		}
		if ((addr->proto       == QETH_PROT_IPV6)     &&
		    (todo->proto       == QETH_PROT_IPV6)     &&
		    (addr->type        == todo->type)         &&
		    (addr->u.a6.pfxlen == todo->u.a6.pfxlen)  &&
		    (memcmp(&addr->u.a6.addr, &todo->u.a6.addr,
			    sizeof(struct in6_addr)) == 0)) {
			found = 1;
			break;
		}
	}
	if (found) {
		addr->users += todo->users;
		if (addr->users <= 0){
			*__addr = addr;
			return -1;
		} else {
			/* for VIPA and RXIP limit refcount to 1 */
			if (addr->type != QETH_IP_TYPE_NORMAL)
				addr->users = 1;
			return 0;
		}
	}
	if (todo->users > 0) {
		/* for VIPA and RXIP limit refcount to 1 */
		if (todo->type != QETH_IP_TYPE_NORMAL)
			todo->users = 1;
		return 1;
	} else
		return 0;
}

static inline int
__qeth_address_exists_in_list(struct list_head *list, struct qeth_ipaddr *addr,
		              int same_type)
{
	struct qeth_ipaddr *tmp;

	list_for_each_entry(tmp, list, entry) {
		if ((tmp->proto     == QETH_PROT_IPV4)            &&
		    (addr->proto    == QETH_PROT_IPV4)            &&
		    ((same_type && (tmp->type == addr->type)) ||
		     (!same_type && (tmp->type != addr->type))  ) &&
		    (tmp->u.a4.addr == addr->u.a4.addr)             ){
			return 1;
		}
		if ((tmp->proto  == QETH_PROT_IPV6)               &&
		    (addr->proto == QETH_PROT_IPV6)               &&
		    ((same_type && (tmp->type == addr->type)) ||
		     (!same_type && (tmp->type != addr->type))  ) &&
		    (memcmp(&tmp->u.a6.addr, &addr->u.a6.addr,
			    sizeof(struct in6_addr)) == 0)          ) {
			return 1;
		}
	}
	return 0;
}

/*
 * Add IP to be added to todo list. If there is already an "add todo"
 * in this list we just incremenent the reference count.
 * Returns 0 if we  just incremented reference count.
 */
static int
__qeth_insert_ip_todo(struct qeth_card *card, struct qeth_ipaddr *addr, int add)
{
	struct qeth_ipaddr *tmp, *t;
	int found = 0;

	list_for_each_entry_safe(tmp, t, card->ip_tbd_list, entry) {
		if ((addr->type == QETH_IP_TYPE_DEL_ALL_MC) &&
		    (tmp->type == QETH_IP_TYPE_DEL_ALL_MC))
			return 0;
		if (card->options.layer2) {
			if ((tmp->type	== addr->type)	&&
			    (tmp->is_multicast == addr->is_multicast) &&
			    (memcmp(&tmp->mac, &addr->mac, 
				    OSA_ADDR_LEN) == 0)) {
				found = 1;
				break;
			}
			continue;
		} 	 
		if ((tmp->proto        == QETH_PROT_IPV4)     &&
		    (addr->proto       == QETH_PROT_IPV4)     &&
		    (tmp->type         == addr->type)         &&
		    (tmp->is_multicast == addr->is_multicast) &&
		    (tmp->u.a4.addr    == addr->u.a4.addr)    &&
		    (tmp->u.a4.mask    == addr->u.a4.mask)) {
			found = 1;
			break;
		}
		if ((tmp->proto        == QETH_PROT_IPV6)      &&
		    (addr->proto       == QETH_PROT_IPV6)      &&
		    (tmp->type         == addr->type)          &&
		    (tmp->is_multicast == addr->is_multicast)  &&
		    (tmp->u.a6.pfxlen  == addr->u.a6.pfxlen)   &&
		    (memcmp(&tmp->u.a6.addr, &addr->u.a6.addr,
			    sizeof(struct in6_addr)) == 0)) {
			found = 1;
			break;
		}
	}
	if (found){
		if (addr->users != 0)
			tmp->users += addr->users;
		else
			tmp->users += add? 1:-1;
		if (tmp->users == 0) {
			list_del(&tmp->entry);
			kfree(tmp);
		}
		return 0;
	} else {
		if (addr->type == QETH_IP_TYPE_DEL_ALL_MC)
			list_add(&addr->entry, card->ip_tbd_list);
		else {
			if (addr->users == 0)
				addr->users += add? 1:-1;
			if (add && (addr->type == QETH_IP_TYPE_NORMAL) &&
			    qeth_is_addr_covered_by_ipato(card, addr)){
				QETH_DBF_TEXT(trace, 2, "tkovaddr");
				addr->set_flags |= QETH_IPA_SETIP_TAKEOVER_FLAG;
			}
			list_add_tail(&addr->entry, card->ip_tbd_list);
		}
		return 1;
	}
}

/**
 * Remove IP address from list
 */
static int
qeth_delete_ip(struct qeth_card *card, struct qeth_ipaddr *addr)
{
	unsigned long flags;
	int rc = 0;

	QETH_DBF_TEXT(trace, 4, "delip");

	if (card->options.layer2)
		QETH_DBF_HEX(trace, 4, &addr->mac, 6);
	else if (addr->proto == QETH_PROT_IPV4)
		QETH_DBF_HEX(trace, 4, &addr->u.a4.addr, 4);
	else {
		QETH_DBF_HEX(trace, 4, &addr->u.a6.addr, 8);
		QETH_DBF_HEX(trace, 4, ((char *)&addr->u.a6.addr) + 8, 8);
	}
	spin_lock_irqsave(&card->ip_lock, flags);
	rc = __qeth_insert_ip_todo(card, addr, 0);
	spin_unlock_irqrestore(&card->ip_lock, flags);
	return rc;
}

static int
qeth_add_ip(struct qeth_card *card, struct qeth_ipaddr *addr)
{
	unsigned long flags;
	int rc = 0;

	QETH_DBF_TEXT(trace, 4, "addip");
	if (card->options.layer2)
		QETH_DBF_HEX(trace, 4, &addr->mac, 6);
	else if (addr->proto == QETH_PROT_IPV4)
		QETH_DBF_HEX(trace, 4, &addr->u.a4.addr, 4);
	else {
		QETH_DBF_HEX(trace, 4, &addr->u.a6.addr, 8);
		QETH_DBF_HEX(trace, 4, ((char *)&addr->u.a6.addr) + 8, 8);
	}
	spin_lock_irqsave(&card->ip_lock, flags);
	rc = __qeth_insert_ip_todo(card, addr, 1);
	spin_unlock_irqrestore(&card->ip_lock, flags);
	return rc;
}

static inline void
__qeth_delete_all_mc(struct qeth_card *card, unsigned long *flags)
{
	struct qeth_ipaddr *addr, *tmp;
	int rc;
again:
	list_for_each_entry_safe(addr, tmp, &card->ip_list, entry) {
		if (addr->is_multicast) {
			spin_unlock_irqrestore(&card->ip_lock, *flags);
			rc = qeth_deregister_addr_entry(card, addr);
			spin_lock_irqsave(&card->ip_lock, *flags);
			if (!rc) {
				list_del(&addr->entry);
				kfree(addr);
				goto again;
			}
		}
	}
}

static void
qeth_set_ip_addr_list(struct qeth_card *card)
{
	struct list_head *tbd_list;
	struct qeth_ipaddr *todo, *addr;
	unsigned long flags;
	int rc;

	QETH_DBF_TEXT(trace, 2, "sdiplist");
	QETH_DBF_HEX(trace, 2, &card, sizeof(void *));

	spin_lock_irqsave(&card->ip_lock, flags);
	tbd_list = card->ip_tbd_list;
	card->ip_tbd_list = kmalloc(sizeof(struct list_head), GFP_ATOMIC);
	if (!card->ip_tbd_list) {
		QETH_DBF_TEXT(trace, 0, "silnomem");
		card->ip_tbd_list = tbd_list;
		spin_unlock_irqrestore(&card->ip_lock, flags);
		return;
	} else
		INIT_LIST_HEAD(card->ip_tbd_list);

	while (!list_empty(tbd_list)){
		todo = list_entry(tbd_list->next, struct qeth_ipaddr, entry);
		list_del(&todo->entry);
		if (todo->type == QETH_IP_TYPE_DEL_ALL_MC){
			__qeth_delete_all_mc(card, &flags);
			kfree(todo);
			continue;
		}
		rc = __qeth_ref_ip_on_card(card, todo, &addr);
		if (rc == 0) {
			/* nothing to be done; only adjusted refcount */
			kfree(todo);
		} else if (rc == 1) {
			/* new entry to be added to on-card list */
			spin_unlock_irqrestore(&card->ip_lock, flags);
			rc = qeth_register_addr_entry(card, todo);
			spin_lock_irqsave(&card->ip_lock, flags);
			if (!rc)
				list_add_tail(&todo->entry, &card->ip_list);
			else
				kfree(todo);
		} else if (rc == -1) {
			/* on-card entry to be removed */
			list_del_init(&addr->entry);
			spin_unlock_irqrestore(&card->ip_lock, flags);
			rc = qeth_deregister_addr_entry(card, addr);
			spin_lock_irqsave(&card->ip_lock, flags);
			if (!rc)
				kfree(addr);
			else
				list_add_tail(&addr->entry, &card->ip_list);
			kfree(todo);
		}
	}
	spin_unlock_irqrestore(&card->ip_lock, flags);
	kfree(tbd_list);
}

static void qeth_delete_mc_addresses(struct qeth_card *);
static void qeth_add_multicast_ipv4(struct qeth_card *);
static void qeth_layer2_add_multicast(struct qeth_card *);
#ifdef CONFIG_QETH_IPV6
static void qeth_add_multicast_ipv6(struct qeth_card *);
#endif

static inline int
qeth_set_thread_start_bit(struct qeth_card *card, unsigned long thread)
{
	unsigned long flags;

	spin_lock_irqsave(&card->thread_mask_lock, flags);
	if ( !(card->thread_allowed_mask & thread) ||
	      (card->thread_start_mask & thread) ) {
		spin_unlock_irqrestore(&card->thread_mask_lock, flags);
		return -EPERM;
	}
	card->thread_start_mask |= thread;
	spin_unlock_irqrestore(&card->thread_mask_lock, flags);
	return 0;
}

static void
qeth_clear_thread_start_bit(struct qeth_card *card, unsigned long thread)
{
	unsigned long flags;

	spin_lock_irqsave(&card->thread_mask_lock, flags);
	card->thread_start_mask &= ~thread;
	spin_unlock_irqrestore(&card->thread_mask_lock, flags);
	wake_up(&card->wait_q);
}

static void
qeth_clear_thread_running_bit(struct qeth_card *card, unsigned long thread)
{
	unsigned long flags;

	spin_lock_irqsave(&card->thread_mask_lock, flags);
	card->thread_running_mask &= ~thread;
	spin_unlock_irqrestore(&card->thread_mask_lock, flags);
	wake_up(&card->wait_q);
}

static inline int
__qeth_do_run_thread(struct qeth_card *card, unsigned long thread)
{
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&card->thread_mask_lock, flags);
	if (card->thread_start_mask & thread){
		if ((card->thread_allowed_mask & thread) &&
		    !(card->thread_running_mask & thread)){
			rc = 1;
			card->thread_start_mask &= ~thread;
			card->thread_running_mask |= thread;
		} else
			rc = -EPERM;
	}
	spin_unlock_irqrestore(&card->thread_mask_lock, flags);
	return rc;
}

static int
qeth_do_run_thread(struct qeth_card *card, unsigned long thread)
{
	int rc = 0;

	wait_event(card->wait_q,
		   (rc = __qeth_do_run_thread(card, thread)) >= 0);
	return rc;
}

static int
qeth_register_ip_addresses(void *ptr)
{
	struct qeth_card *card;

	card = (struct qeth_card *) ptr;
	daemonize("qeth_reg_ip");
	QETH_DBF_TEXT(trace,4,"regipth1");
	if (!qeth_do_run_thread(card, QETH_SET_IP_THREAD))
		return 0;
	QETH_DBF_TEXT(trace,4,"regipth2");
	qeth_set_ip_addr_list(card);
	qeth_clear_thread_running_bit(card, QETH_SET_IP_THREAD);
	return 0;
}

/*
 * Drive the SET_PROMISC_MODE thread
 */
static int
qeth_set_promisc_mode(void *ptr)
{
	struct qeth_card *card = (struct qeth_card *) ptr;

	daemonize("qeth_setprm");
	QETH_DBF_TEXT(trace,4,"setprm1");
	if (!qeth_do_run_thread(card, QETH_SET_PROMISC_MODE_THREAD))
		return 0;
	QETH_DBF_TEXT(trace,4,"setprm2");
	qeth_setadp_promisc_mode(card);
	qeth_clear_thread_running_bit(card, QETH_SET_PROMISC_MODE_THREAD);
	return 0;
}

static int
qeth_recover(void *ptr)
{
	struct qeth_card *card;
	int rc = 0;

	card = (struct qeth_card *) ptr;
	daemonize("qeth_recover");
	QETH_DBF_TEXT(trace,2,"recover1");
	QETH_DBF_HEX(trace, 2, &card, sizeof(void *));
	if (!qeth_do_run_thread(card, QETH_RECOVER_THREAD))
		return 0;
	QETH_DBF_TEXT(trace,2,"recover2");
	PRINT_WARN("Recovery of device %s started ...\n",
		   CARD_BUS_ID(card));
	card->use_hard_stop = 1;
	__qeth_set_offline(card->gdev,1);
	rc = __qeth_set_online(card->gdev,1);
	if (!rc)
		PRINT_INFO("Device %s successfully recovered!\n",
			   CARD_BUS_ID(card));
	else
		PRINT_INFO("Device %s could not be recovered!\n",
			   CARD_BUS_ID(card));
	/* don't run another scheduled recovery */
	qeth_clear_thread_start_bit(card, QETH_RECOVER_THREAD);
	qeth_clear_thread_running_bit(card, QETH_RECOVER_THREAD);
	return 0;
}

void
qeth_schedule_recovery(struct qeth_card *card)
{
	QETH_DBF_TEXT(trace,2,"startrec");
	if (qeth_set_thread_start_bit(card, QETH_RECOVER_THREAD) == 0)
		schedule_work(&card->kernel_thread_starter);
}

static int
qeth_do_start_thread(struct qeth_card *card, unsigned long thread)
{
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&card->thread_mask_lock, flags);
	QETH_DBF_TEXT_(trace, 4, "  %02x%02x%02x",
			(u8) card->thread_start_mask,
			(u8) card->thread_allowed_mask,
			(u8) card->thread_running_mask);
	rc = (card->thread_start_mask & thread);
	spin_unlock_irqrestore(&card->thread_mask_lock, flags);
	return rc;
}

static void
qeth_start_kernel_thread(struct qeth_card *card)
{
	QETH_DBF_TEXT(trace , 2, "strthrd");

	if (card->read.state != CH_STATE_UP &&
	    card->write.state != CH_STATE_UP)
		return;

	if (qeth_do_start_thread(card, QETH_SET_IP_THREAD))
		kernel_thread(qeth_register_ip_addresses, (void *)card,SIGCHLD);
	if (qeth_do_start_thread(card, QETH_SET_PROMISC_MODE_THREAD))
		kernel_thread(qeth_set_promisc_mode, (void *)card, SIGCHLD);
	if (qeth_do_start_thread(card, QETH_RECOVER_THREAD))
		kernel_thread(qeth_recover, (void *) card, SIGCHLD);
}


static void
qeth_set_intial_options(struct qeth_card *card)
{
	card->options.route4.type = NO_ROUTER;
#ifdef CONFIG_QETH_IPV6
	card->options.route6.type = NO_ROUTER;
#endif /* QETH_IPV6 */
	card->options.checksum_type = QETH_CHECKSUM_DEFAULT;
	card->options.broadcast_mode = QETH_TR_BROADCAST_ALLRINGS;
	card->options.macaddr_mode = QETH_TR_MACADDR_NONCANONICAL;
	card->options.fake_broadcast = 0;
	card->options.add_hhlen = DEFAULT_ADD_HHLEN;
	card->options.fake_ll = 0;
	if (card->info.type == QETH_CARD_TYPE_OSN)
		card->options.layer2 = 1;
	else
		card->options.layer2 = 0;
}

/**
 * initialize channels ,card and all state machines
 */
static int
qeth_setup_card(struct qeth_card *card)
{

	QETH_DBF_TEXT(setup, 2, "setupcrd");
	QETH_DBF_HEX(setup, 2, &card, sizeof(void *));

	card->read.state  = CH_STATE_DOWN;
	card->write.state = CH_STATE_DOWN;
	card->data.state  = CH_STATE_DOWN;
	card->state = CARD_STATE_DOWN;
	card->lan_online = 0;
	card->use_hard_stop = 0;
	card->dev = NULL;
#ifdef CONFIG_QETH_VLAN
	spin_lock_init(&card->vlanlock);
	card->vlangrp = NULL;
#endif
	spin_lock_init(&card->lock);
	spin_lock_init(&card->ip_lock);
	spin_lock_init(&card->thread_mask_lock);
	card->thread_start_mask = 0;
	card->thread_allowed_mask = 0;
	card->thread_running_mask = 0;
	INIT_WORK(&card->kernel_thread_starter,
		  (void *)qeth_start_kernel_thread,card);
	INIT_LIST_HEAD(&card->ip_list);
	card->ip_tbd_list = kmalloc(sizeof(struct list_head), GFP_KERNEL);
	if (!card->ip_tbd_list) {
		QETH_DBF_TEXT(setup, 0, "iptbdnom");
		return -ENOMEM;
	}
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

static int
is_1920_device (struct qeth_card *card)
{
	int single_queue = 0;
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

	QETH_DBF_TEXT(setup, 2, "chk_1920");

	ccwdev = card->data.ccwdev;
	chp_dsc = (struct channelPath_dsc *)ccw_device_get_chp_desc(ccwdev, 0);
	if (chp_dsc != NULL) {
		/* CHPP field bit 6 == 1 -> single queue */
		single_queue = ((chp_dsc->chpp & 0x02) == 0x02);
		kfree(chp_dsc);
	}
	QETH_DBF_TEXT_(setup, 2, "rc:%x", single_queue);
	return single_queue;
}

static int
qeth_determine_card_type(struct qeth_card *card)
{
	int i = 0;

	QETH_DBF_TEXT(setup, 2, "detcdtyp");

	card->qdio.do_prio_queueing = QETH_PRIOQ_DEFAULT;
	card->qdio.default_out_queue = QETH_DEFAULT_QUEUE;
	while (known_devices[i][4]) {
		if ((CARD_RDEV(card)->id.dev_type == known_devices[i][2]) &&
		    (CARD_RDEV(card)->id.dev_model == known_devices[i][3])) {
			card->info.type = known_devices[i][4];
			card->qdio.no_out_queues = known_devices[i][8];
			card->info.is_multicast_different = known_devices[i][9];
			if (is_1920_device(card)) {
				PRINT_INFO("Priority Queueing not able "
					   "due to hardware limitations!\n");
				card->qdio.no_out_queues = 1;
				card->qdio.default_out_queue = 0;
			} 
			return 0;
		}
		i++;
	}
	card->info.type = QETH_CARD_TYPE_UNKNOWN;
	PRINT_ERR("unknown card type on device %s\n", CARD_BUS_ID(card));
	return -ENOENT;
}

static int
qeth_probe_device(struct ccwgroup_device *gdev)
{
	struct qeth_card *card;
	struct device *dev;
	unsigned long flags;
	int rc;

	QETH_DBF_TEXT(setup, 2, "probedev");

	dev = &gdev->dev;
	if (!get_device(dev))
		return -ENODEV;

	QETH_DBF_TEXT_(setup, 2, "%s", gdev->dev.bus_id);
	
	card = qeth_alloc_card();
	if (!card) {
		put_device(dev);
		QETH_DBF_TEXT_(setup, 2, "1err%d", -ENOMEM);
		return -ENOMEM;
	}
	card->read.ccwdev  = gdev->cdev[0];
	card->write.ccwdev = gdev->cdev[1];
	card->data.ccwdev  = gdev->cdev[2];
	gdev->dev.driver_data = card;
	card->gdev = gdev;
	gdev->cdev[0]->handler = qeth_irq;
	gdev->cdev[1]->handler = qeth_irq;
	gdev->cdev[2]->handler = qeth_irq;

	if ((rc = qeth_determine_card_type(card))){
		PRINT_WARN("%s: not a valid card type\n", __func__);
		QETH_DBF_TEXT_(setup, 2, "3err%d", rc);
		put_device(dev);
		qeth_free_card(card);
		return rc;
	}			    
	if ((rc = qeth_setup_card(card))){
		QETH_DBF_TEXT_(setup, 2, "2err%d", rc);
		put_device(dev);
		qeth_free_card(card);
		return rc;
	}
	rc = qeth_create_device_attributes(dev);
	if (rc) {
		put_device(dev);
		qeth_free_card(card);
		return rc;
	}
	/* insert into our internal list */
	write_lock_irqsave(&qeth_card_list.rwlock, flags);
	list_add_tail(&card->list, &qeth_card_list.list);
	write_unlock_irqrestore(&qeth_card_list.rwlock, flags);
	return rc;
}


static int
qeth_get_unitaddr(struct qeth_card *card)
{
 	int length;
	char *prcd;
	int rc;

	QETH_DBF_TEXT(setup, 2, "getunit");
	rc = read_conf_data(CARD_DDEV(card), (void **) &prcd, &length);
	if (rc) {
		PRINT_ERR("read_conf_data for device %s returned %i\n",
			  CARD_DDEV_ID(card), rc);
		return rc;
	}
	card->info.chpid = prcd[30];
	card->info.unit_addr2 = prcd[31];
	card->info.cula = prcd[63];
	card->info.guestlan = ((prcd[0x10] == _ascebc['V']) &&
			       (prcd[0x11] == _ascebc['M']));
	return 0;
}

static void
qeth_init_tokens(struct qeth_card *card)
{
	card->token.issuer_rm_w = 0x00010103UL;
	card->token.cm_filter_w = 0x00010108UL;
	card->token.cm_connection_w = 0x0001010aUL;
	card->token.ulp_filter_w = 0x0001010bUL;
	card->token.ulp_connection_w = 0x0001010dUL;
}

static inline __u16
raw_devno_from_bus_id(char *id)
{
        id += (strlen(id) - 4);
        return (__u16) simple_strtoul(id, &id, 16);
}
/**
 * setup channel
 */
static void
qeth_setup_ccw(struct qeth_channel *channel,unsigned char *iob, __u32 len)
{
	struct qeth_card *card;

	QETH_DBF_TEXT(trace, 4, "setupccw");
	card = CARD_FROM_CDEV(channel->ccwdev);
	if (channel == &card->read)
		memcpy(&channel->ccw, READ_CCW, sizeof(struct ccw1));
	else
		memcpy(&channel->ccw, WRITE_CCW, sizeof(struct ccw1));
	channel->ccw.count = len;
	channel->ccw.cda = (__u32) __pa(iob);
}

/**
 * get free buffer for ccws (IDX activation, lancmds,ipassists...)
 */
static struct qeth_cmd_buffer *
__qeth_get_buffer(struct qeth_channel *channel)
{
	__u8 index;

	QETH_DBF_TEXT(trace, 6, "getbuff");
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
	} while(index != channel->io_buf_no);

	return NULL;
}

/**
 * release command buffer
 */
static void
qeth_release_buffer(struct qeth_channel *channel, struct qeth_cmd_buffer *iob)
{
	unsigned long flags;

	QETH_DBF_TEXT(trace, 6, "relbuff");
	spin_lock_irqsave(&channel->iob_lock, flags);
	memset(iob->data, 0, QETH_BUFSIZE);
	iob->state = BUF_STATE_FREE;
	iob->callback = qeth_send_control_data_cb;
	iob->rc = 0;
	spin_unlock_irqrestore(&channel->iob_lock, flags);
}

static struct qeth_cmd_buffer *
qeth_get_buffer(struct qeth_channel *channel)
{
	struct qeth_cmd_buffer *buffer = NULL;
	unsigned long flags;

	spin_lock_irqsave(&channel->iob_lock, flags);
	buffer = __qeth_get_buffer(channel);
	spin_unlock_irqrestore(&channel->iob_lock, flags);
	return buffer;
}

static struct qeth_cmd_buffer *
qeth_wait_for_buffer(struct qeth_channel *channel)
{
	struct qeth_cmd_buffer *buffer;
	wait_event(channel->wait_q,
		   ((buffer = qeth_get_buffer(channel)) != NULL));
	return buffer;
}

static void
qeth_clear_cmd_buffers(struct qeth_channel *channel)
{
	int cnt = 0;

	for (cnt=0; cnt < QETH_CMD_BUFFER_NO; cnt++)
		qeth_release_buffer(channel,&channel->iob[cnt]);
	channel->buf_no = 0;
	channel->io_buf_no = 0;
}

/**
 * start IDX for read and write channel
 */
static int
qeth_idx_activate_get_answer(struct qeth_channel *channel,
			      void (*idx_reply_cb)(struct qeth_channel *,
						   struct qeth_cmd_buffer *))
{
	struct qeth_cmd_buffer *iob;
	unsigned long flags;
	int rc;
	struct qeth_card *card;

	QETH_DBF_TEXT(setup, 2, "idxanswr");
	card = CARD_FROM_CDEV(channel->ccwdev);
	iob = qeth_get_buffer(channel);
	iob->callback = idx_reply_cb;
	memcpy(&channel->ccw, READ_CCW, sizeof(struct ccw1));
	channel->ccw.count = QETH_BUFSIZE;
	channel->ccw.cda = (__u32) __pa(iob->data);

	wait_event(card->wait_q,
		   atomic_cmpxchg(&channel->irq_pending, 0, 1) == 0);
	QETH_DBF_TEXT(setup, 6, "noirqpnd");
	spin_lock_irqsave(get_ccwdev_lock(channel->ccwdev), flags);
	rc = ccw_device_start(channel->ccwdev,
			      &channel->ccw,(addr_t) iob, 0, 0);
	spin_unlock_irqrestore(get_ccwdev_lock(channel->ccwdev), flags);

	if (rc) {
		PRINT_ERR("qeth: Error2 in activating channel rc=%d\n",rc);
		QETH_DBF_TEXT_(setup, 2, "2err%d", rc);
		atomic_set(&channel->irq_pending, 0);
		wake_up(&card->wait_q);
		return rc;
	}
	rc = wait_event_interruptible_timeout(card->wait_q,
			 channel->state == CH_STATE_UP, QETH_TIMEOUT);
	if (rc == -ERESTARTSYS)
		return rc;
	if (channel->state != CH_STATE_UP){
		rc = -ETIME;
		QETH_DBF_TEXT_(setup, 2, "3err%d", rc);
		qeth_clear_cmd_buffers(channel);
	} else
		rc = 0;
	return rc;
}

static int
qeth_idx_activate_channel(struct qeth_channel *channel,
			   void (*idx_reply_cb)(struct qeth_channel *,
						struct qeth_cmd_buffer *))
{
	struct qeth_card *card;
	struct qeth_cmd_buffer *iob;
	unsigned long flags;
	__u16 temp;
	int rc;

	card = CARD_FROM_CDEV(channel->ccwdev);

	QETH_DBF_TEXT(setup, 2, "idxactch");

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
	memcpy(QETH_IDX_ACT_ISSUER_RM_TOKEN(iob->data),
	       &card->token.issuer_rm_w,QETH_MPC_TOKEN_LENGTH);
	memcpy(QETH_IDX_ACT_FUNC_LEVEL(iob->data),
	       &card->info.func_level,sizeof(__u16));
	temp = raw_devno_from_bus_id(CARD_DDEV_ID(card));
	memcpy(QETH_IDX_ACT_QDIO_DEV_CUA(iob->data), &temp, 2);
	temp = (card->info.cula << 8) + card->info.unit_addr2;
	memcpy(QETH_IDX_ACT_QDIO_DEV_REALADDR(iob->data), &temp, 2);

	wait_event(card->wait_q,
		   atomic_cmpxchg(&channel->irq_pending, 0, 1) == 0);
	QETH_DBF_TEXT(setup, 6, "noirqpnd");
	spin_lock_irqsave(get_ccwdev_lock(channel->ccwdev), flags);
	rc = ccw_device_start(channel->ccwdev,
			      &channel->ccw,(addr_t) iob, 0, 0);
	spin_unlock_irqrestore(get_ccwdev_lock(channel->ccwdev), flags);

	if (rc) {
		PRINT_ERR("qeth: Error1 in activating channel. rc=%d\n",rc);
		QETH_DBF_TEXT_(setup, 2, "1err%d", rc);
		atomic_set(&channel->irq_pending, 0);
		wake_up(&card->wait_q);
		return rc;
	}
	rc = wait_event_interruptible_timeout(card->wait_q,
			channel->state == CH_STATE_ACTIVATING, QETH_TIMEOUT);
	if (rc == -ERESTARTSYS)
		return rc;
	if (channel->state != CH_STATE_ACTIVATING) {
		PRINT_WARN("qeth: IDX activate timed out!\n");
		QETH_DBF_TEXT_(setup, 2, "2err%d", -ETIME);
		qeth_clear_cmd_buffers(channel);
		return -ETIME;
	}
	return qeth_idx_activate_get_answer(channel,idx_reply_cb);
}

static int
qeth_peer_func_level(int level)
{
	if ((level & 0xff) == 8)
		return (level & 0xff) + 0x400;
	if (((level >> 8) & 3) == 1)
		return (level & 0xff) + 0x200;
	return level;
}

static void
qeth_idx_write_cb(struct qeth_channel *channel, struct qeth_cmd_buffer *iob)
{
	struct qeth_card *card;
	__u16 temp;

	QETH_DBF_TEXT(setup ,2, "idxwrcb");

	if (channel->state == CH_STATE_DOWN) {
		channel->state = CH_STATE_ACTIVATING;
		goto out;
	}
	card = CARD_FROM_CDEV(channel->ccwdev);

	if (!(QETH_IS_IDX_ACT_POS_REPLY(iob->data))) {
		PRINT_ERR("IDX_ACTIVATE on write channel device %s: negative "
			  "reply\n", CARD_WDEV_ID(card));
		goto out;
	}
	memcpy(&temp, QETH_IDX_ACT_FUNC_LEVEL(iob->data), 2);
	if ((temp & ~0x0100) != qeth_peer_func_level(card->info.func_level)) {
		PRINT_WARN("IDX_ACTIVATE on write channel device %s: "
			   "function level mismatch "
			   "(sent: 0x%x, received: 0x%x)\n",
			   CARD_WDEV_ID(card), card->info.func_level, temp);
		goto out;
	}
	channel->state = CH_STATE_UP;
out:
	qeth_release_buffer(channel, iob);
}

static int
qeth_check_idx_response(unsigned char *buffer)
{
	if (!buffer)
		return 0;

	QETH_DBF_HEX(control, 2, buffer, QETH_DBF_CONTROL_LEN);
	if ((buffer[2] & 0xc0) == 0xc0) {
		PRINT_WARN("received an IDX TERMINATE "
			   "with cause code 0x%02x%s\n",
			   buffer[4],
			   ((buffer[4] == 0x22) ?
			    " -- try another portname" : ""));
		QETH_DBF_TEXT(trace, 2, "ckidxres");
		QETH_DBF_TEXT(trace, 2, " idxterm");
		QETH_DBF_TEXT_(trace, 2, "  rc%d", -EIO);
		return -EIO;
	}
	return 0;
}

static void
qeth_idx_read_cb(struct qeth_channel *channel, struct qeth_cmd_buffer *iob)
{
	struct qeth_card *card;
	__u16 temp;

	QETH_DBF_TEXT(setup , 2, "idxrdcb");
	if (channel->state == CH_STATE_DOWN) {
		channel->state = CH_STATE_ACTIVATING;
		goto out;
	}

	card = CARD_FROM_CDEV(channel->ccwdev);
	if (qeth_check_idx_response(iob->data)) {
			goto out;
	}
	if (!(QETH_IS_IDX_ACT_POS_REPLY(iob->data))) {
		PRINT_ERR("IDX_ACTIVATE on read channel device %s: negative "
			  "reply\n", CARD_RDEV_ID(card));
		goto out;
	}

/**
 * temporary fix for microcode bug
 * to revert it,replace OR by AND
 */
	if ( (!QETH_IDX_NO_PORTNAME_REQUIRED(iob->data)) ||
	     (card->info.type == QETH_CARD_TYPE_OSAE) )
		card->info.portname_required = 1;

	memcpy(&temp, QETH_IDX_ACT_FUNC_LEVEL(iob->data), 2);
	if (temp != qeth_peer_func_level(card->info.func_level)) {
		PRINT_WARN("IDX_ACTIVATE on read channel device %s: function "
			   "level mismatch (sent: 0x%x, received: 0x%x)\n",
			   CARD_RDEV_ID(card), card->info.func_level, temp);
		goto out;
	}
	memcpy(&card->token.issuer_rm_r,
	       QETH_IDX_ACT_ISSUER_RM_TOKEN(iob->data),
	       QETH_MPC_TOKEN_LENGTH);
	memcpy(&card->info.mcl_level[0],
	       QETH_IDX_REPLY_LEVEL(iob->data), QETH_MCL_LENGTH);
	channel->state = CH_STATE_UP;
out:
	qeth_release_buffer(channel,iob);
}

static int
qeth_issue_next_read(struct qeth_card *card)
{
	int rc;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(trace,5,"issnxrd");
	if (card->read.state != CH_STATE_UP)
		return -EIO;
	iob = qeth_get_buffer(&card->read);
	if (!iob) {
		PRINT_WARN("issue_next_read failed: no iob available!\n");
		return -ENOMEM;
	}
	qeth_setup_ccw(&card->read, iob->data, QETH_BUFSIZE);
	wait_event(card->wait_q,
		   atomic_cmpxchg(&card->read.irq_pending, 0, 1) == 0);
	QETH_DBF_TEXT(trace, 6, "noirqpnd");
	rc = ccw_device_start(card->read.ccwdev, &card->read.ccw,
			      (addr_t) iob, 0, 0);
	if (rc) {
		PRINT_ERR("Error in starting next read ccw! rc=%i\n", rc);
		atomic_set(&card->read.irq_pending, 0);
		qeth_schedule_recovery(card);
		wake_up(&card->wait_q);
	}
	return rc;
}

static struct qeth_reply *
qeth_alloc_reply(struct qeth_card *card)
{
	struct qeth_reply *reply;

	reply = kmalloc(sizeof(struct qeth_reply), GFP_ATOMIC);
	if (reply){
		memset(reply, 0, sizeof(struct qeth_reply));
		atomic_set(&reply->refcnt, 1);
		reply->card = card;
	};
	return reply;
}

static void
qeth_get_reply(struct qeth_reply *reply)
{
	WARN_ON(atomic_read(&reply->refcnt) <= 0);
	atomic_inc(&reply->refcnt);
}

static void
qeth_put_reply(struct qeth_reply *reply)
{
	WARN_ON(atomic_read(&reply->refcnt) <= 0);
	if (atomic_dec_and_test(&reply->refcnt))
		kfree(reply);
}

static void
qeth_cmd_timeout(unsigned long data)
{
	struct qeth_reply *reply, *list_reply, *r;
	unsigned long flags;

	reply = (struct qeth_reply *) data;
	spin_lock_irqsave(&reply->card->lock, flags);
	list_for_each_entry_safe(list_reply, r,
				 &reply->card->cmd_waiter_list, list) {
		if (reply == list_reply){
			qeth_get_reply(reply);
			list_del_init(&reply->list);
			spin_unlock_irqrestore(&reply->card->lock, flags);
			reply->rc = -ETIME;
			reply->received = 1;
			wake_up(&reply->wait_q);
			qeth_put_reply(reply);
			return;
		}
	}
	spin_unlock_irqrestore(&reply->card->lock, flags);
}

static struct qeth_ipa_cmd *
qeth_check_ipa_data(struct qeth_card *card, struct qeth_cmd_buffer *iob)
{
	struct qeth_ipa_cmd *cmd = NULL;

	QETH_DBF_TEXT(trace,5,"chkipad");
	if (IS_IPA(iob->data)){
		cmd = (struct qeth_ipa_cmd *) PDU_ENCAPSULATION(iob->data);
		if (IS_IPA_REPLY(cmd))
			return cmd;
		else {
			switch (cmd->hdr.command) {
			case IPA_CMD_STOPLAN:
				PRINT_WARN("Link failure on %s (CHPID 0x%X) - "
					   "there is a network problem or "
					   "someone pulled the cable or "
					   "disabled the port.\n",
					   QETH_CARD_IFNAME(card),
					   card->info.chpid);
				card->lan_online = 0;
				netif_carrier_off(card->dev);
				return NULL;
			case IPA_CMD_STARTLAN:
				PRINT_INFO("Link reestablished on %s "
					   "(CHPID 0x%X). Scheduling "
					   "IP address reset.\n",
					   QETH_CARD_IFNAME(card),
					   card->info.chpid);
				qeth_schedule_recovery(card);
				return NULL;
			case IPA_CMD_MODCCID:
				return cmd;
			case IPA_CMD_REGISTER_LOCAL_ADDR:
				QETH_DBF_TEXT(trace,3, "irla");
				break;
			case IPA_CMD_UNREGISTER_LOCAL_ADDR:
				QETH_DBF_TEXT(trace,3, "urla");
				break;
			default:
				PRINT_WARN("Received data is IPA "
					   "but not a reply!\n");
				break;
			}
		}
	}
	return cmd;
}

/**
 * wake all waiting ipa commands
 */
static void
qeth_clear_ipacmd_list(struct qeth_card *card)
{
	struct qeth_reply *reply, *r;
	unsigned long flags;

	QETH_DBF_TEXT(trace, 4, "clipalst");

	spin_lock_irqsave(&card->lock, flags);
	list_for_each_entry_safe(reply, r, &card->cmd_waiter_list, list) {
		qeth_get_reply(reply);
		reply->rc = -EIO;
		reply->received = 1;
		list_del_init(&reply->list);
		wake_up(&reply->wait_q);
		qeth_put_reply(reply);
	}
	spin_unlock_irqrestore(&card->lock, flags);
}

static void
qeth_send_control_data_cb(struct qeth_channel *channel,
			  struct qeth_cmd_buffer *iob)
{
	struct qeth_card *card;
	struct qeth_reply *reply, *r;
	struct qeth_ipa_cmd *cmd;
	unsigned long flags;
	int keep_reply;

	QETH_DBF_TEXT(trace,4,"sndctlcb");

	card = CARD_FROM_CDEV(channel->ccwdev);
	if (qeth_check_idx_response(iob->data)) {
		qeth_clear_ipacmd_list(card);
		qeth_schedule_recovery(card);
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
					reply->offset = (__u16)((char*)cmd -
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
				reply->received = 1;
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
	qeth_release_buffer(channel,iob);
}

static inline void
qeth_prepare_control_data(struct qeth_card *card, int len,
struct qeth_cmd_buffer *iob)
{
	qeth_setup_ccw(&card->write,iob->data,len);
	iob->callback = qeth_release_buffer;

	memcpy(QETH_TRANSPORT_HEADER_SEQ_NO(iob->data),
	       &card->seqno.trans_hdr, QETH_SEQ_NO_LENGTH);
	card->seqno.trans_hdr++;
	memcpy(QETH_PDU_HEADER_SEQ_NO(iob->data),
	       &card->seqno.pdu_hdr, QETH_SEQ_NO_LENGTH);
	card->seqno.pdu_hdr++;
	memcpy(QETH_PDU_HEADER_ACK_SEQ_NO(iob->data),
	       &card->seqno.pdu_hdr_ack, QETH_SEQ_NO_LENGTH);
	QETH_DBF_HEX(control, 2, iob->data, QETH_DBF_CONTROL_LEN);
}
						    
static int
qeth_send_control_data(struct qeth_card *card, int len,
		       struct qeth_cmd_buffer *iob,
		       int (*reply_cb)
		       (struct qeth_card *, struct qeth_reply*, unsigned long),
		       void *reply_param)

{
	int rc;
	unsigned long flags;
	struct qeth_reply *reply = NULL;
	struct timer_list timer;

	QETH_DBF_TEXT(trace, 2, "sendctl");

	reply = qeth_alloc_reply(card);
	if (!reply) {
		PRINT_WARN("Could no alloc qeth_reply!\n");
		return -ENOMEM;
	}
	reply->callback = reply_cb;
	reply->param = reply_param;
	if (card->state == CARD_STATE_DOWN)
		reply->seqno = QETH_IDX_COMMAND_SEQNO;
	else
		reply->seqno = card->seqno.ipa++;
	init_timer(&timer);
	timer.function = qeth_cmd_timeout;
	timer.data = (unsigned long) reply;
	init_waitqueue_head(&reply->wait_q);
	spin_lock_irqsave(&card->lock, flags);
	list_add_tail(&reply->list, &card->cmd_waiter_list);
	spin_unlock_irqrestore(&card->lock, flags);
	QETH_DBF_HEX(control, 2, iob->data, QETH_DBF_CONTROL_LEN);
	wait_event(card->wait_q,
		   atomic_cmpxchg(&card->write.irq_pending, 0, 1) == 0);
	qeth_prepare_control_data(card, len, iob);
	if (IS_IPA(iob->data))
		timer.expires = jiffies + QETH_IPA_TIMEOUT;
	else
		timer.expires = jiffies + QETH_TIMEOUT;
	QETH_DBF_TEXT(trace, 6, "noirqpnd");
	spin_lock_irqsave(get_ccwdev_lock(card->write.ccwdev), flags);
	rc = ccw_device_start(card->write.ccwdev, &card->write.ccw,
			      (addr_t) iob, 0, 0);
	spin_unlock_irqrestore(get_ccwdev_lock(card->write.ccwdev), flags);
	if (rc){
		PRINT_WARN("qeth_send_control_data: "
			   "ccw_device_start rc = %i\n", rc);
		QETH_DBF_TEXT_(trace, 2, " err%d", rc);
		spin_lock_irqsave(&card->lock, flags);
		list_del_init(&reply->list);
		qeth_put_reply(reply);
		spin_unlock_irqrestore(&card->lock, flags);
		qeth_release_buffer(iob->channel, iob);
		atomic_set(&card->write.irq_pending, 0);
		wake_up(&card->wait_q);
		return rc;
	}
	add_timer(&timer);
	wait_event(reply->wait_q, reply->received);
	del_timer_sync(&timer);
	rc = reply->rc;
	qeth_put_reply(reply);
	return rc;
}

static int
qeth_osn_send_control_data(struct qeth_card *card, int len,
			   struct qeth_cmd_buffer *iob)
{
	unsigned long flags;
	int rc = 0;

	QETH_DBF_TEXT(trace, 5, "osndctrd");

	wait_event(card->wait_q,
		   atomic_cmpxchg(&card->write.irq_pending, 0, 1) == 0);
	qeth_prepare_control_data(card, len, iob);
	QETH_DBF_TEXT(trace, 6, "osnoirqp");
	spin_lock_irqsave(get_ccwdev_lock(card->write.ccwdev), flags);
	rc = ccw_device_start(card->write.ccwdev, &card->write.ccw,
			      (addr_t) iob, 0, 0);
	spin_unlock_irqrestore(get_ccwdev_lock(card->write.ccwdev), flags);
	if (rc){
		PRINT_WARN("qeth_osn_send_control_data: "
			   "ccw_device_start rc = %i\n", rc);
		QETH_DBF_TEXT_(trace, 2, " err%d", rc);
		qeth_release_buffer(iob->channel, iob);
		atomic_set(&card->write.irq_pending, 0);
		wake_up(&card->wait_q);
	}
	return rc;
}					

static inline void
qeth_prepare_ipa_cmd(struct qeth_card *card, struct qeth_cmd_buffer *iob,
		     char prot_type)
{
	memcpy(iob->data, IPA_PDU_HEADER, IPA_PDU_HEADER_SIZE);
	memcpy(QETH_IPA_CMD_PROT_TYPE(iob->data),&prot_type,1);
	memcpy(QETH_IPA_CMD_DEST_ADDR(iob->data),
	       &card->token.ulp_connection_r, QETH_MPC_TOKEN_LENGTH);
}

static int
qeth_osn_send_ipa_cmd(struct qeth_card *card, struct qeth_cmd_buffer *iob,
		      int data_len)
{
	u16 s1, s2;

	QETH_DBF_TEXT(trace,4,"osndipa");

	qeth_prepare_ipa_cmd(card, iob, QETH_PROT_OSN2);
	s1 = (u16)(IPA_PDU_HEADER_SIZE + data_len);
	s2 = (u16)data_len;
	memcpy(QETH_IPA_PDU_LEN_TOTAL(iob->data), &s1, 2);
	memcpy(QETH_IPA_PDU_LEN_PDU1(iob->data), &s2, 2);
	memcpy(QETH_IPA_PDU_LEN_PDU2(iob->data), &s2, 2);
	memcpy(QETH_IPA_PDU_LEN_PDU3(iob->data), &s2, 2);
	return qeth_osn_send_control_data(card, s1, iob);
}
							    
static int
qeth_send_ipa_cmd(struct qeth_card *card, struct qeth_cmd_buffer *iob,
		  int (*reply_cb)
		  (struct qeth_card *,struct qeth_reply*, unsigned long),
		  void *reply_param)
{
	int rc;
	char prot_type;

	QETH_DBF_TEXT(trace,4,"sendipa");

	if (card->options.layer2)
		if (card->info.type == QETH_CARD_TYPE_OSN)
			prot_type = QETH_PROT_OSN2;
		else
			prot_type = QETH_PROT_LAYER2;
	else
		prot_type = QETH_PROT_TCPIP;
	qeth_prepare_ipa_cmd(card,iob,prot_type);
	rc = qeth_send_control_data(card, IPA_CMD_LENGTH, iob,
				    reply_cb, reply_param);
	return rc;
}


static int
qeth_cm_enable_cb(struct qeth_card *card, struct qeth_reply *reply,
		  unsigned long data)
{
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(setup, 2, "cmenblcb");

	iob = (struct qeth_cmd_buffer *) data;
	memcpy(&card->token.cm_filter_r,
	       QETH_CM_ENABLE_RESP_FILTER_TOKEN(iob->data),
	       QETH_MPC_TOKEN_LENGTH);
	QETH_DBF_TEXT_(setup, 2, "  rc%d", iob->rc);
	return 0;
}

static int
qeth_cm_enable(struct qeth_card *card)
{
	int rc;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(setup,2,"cmenable");

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

static int
qeth_cm_setup_cb(struct qeth_card *card, struct qeth_reply *reply,
		 unsigned long data)
{

	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(setup, 2, "cmsetpcb");

	iob = (struct qeth_cmd_buffer *) data;
	memcpy(&card->token.cm_connection_r,
	       QETH_CM_SETUP_RESP_DEST_ADDR(iob->data),
	       QETH_MPC_TOKEN_LENGTH);
	QETH_DBF_TEXT_(setup, 2, "  rc%d", iob->rc);
	return 0;
}

static int
qeth_cm_setup(struct qeth_card *card)
{
	int rc;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(setup,2,"cmsetup");

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

static int
qeth_ulp_enable_cb(struct qeth_card *card, struct qeth_reply *reply,
		   unsigned long data)
{

	__u16 mtu, framesize;
	__u16 len;
	__u8 link_type;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(setup, 2, "ulpenacb");

	iob = (struct qeth_cmd_buffer *) data;
	memcpy(&card->token.ulp_filter_r,
	       QETH_ULP_ENABLE_RESP_FILTER_TOKEN(iob->data),
	       QETH_MPC_TOKEN_LENGTH);
	if (qeth_get_mtu_out_of_mpc(card->info.type)) {
		memcpy(&framesize, QETH_ULP_ENABLE_RESP_MAX_MTU(iob->data), 2);
		mtu = qeth_get_mtu_outof_framesize(framesize);
		if (!mtu) {
			iob->rc = -EINVAL;
			QETH_DBF_TEXT_(setup, 2, "  rc%d", iob->rc);
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
	QETH_DBF_TEXT_(setup, 2, "  rc%d", iob->rc);
	return 0;
}

static int
qeth_ulp_enable(struct qeth_card *card)
{
	int rc;
	char prot_type;
	struct qeth_cmd_buffer *iob;

	/*FIXME: trace view callbacks*/
	QETH_DBF_TEXT(setup,2,"ulpenabl");

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

	memcpy(QETH_ULP_ENABLE_PROT_TYPE(iob->data),&prot_type,1);
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

static inline __u16
__raw_devno_from_bus_id(char *id)
{
	id += (strlen(id) - 4);
	return (__u16) simple_strtoul(id, &id, 16);
}

static int
qeth_ulp_setup_cb(struct qeth_card *card, struct qeth_reply *reply,
		  unsigned long data)
{
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(setup, 2, "ulpstpcb");

	iob = (struct qeth_cmd_buffer *) data;
	memcpy(&card->token.ulp_connection_r,
	       QETH_ULP_SETUP_RESP_CONNECTION_TOKEN(iob->data),
	       QETH_MPC_TOKEN_LENGTH);
	QETH_DBF_TEXT_(setup, 2, "  rc%d", iob->rc);
	return 0;
}

static int
qeth_ulp_setup(struct qeth_card *card)
{
	int rc;
	__u16 temp;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(setup,2,"ulpsetup");

	iob = qeth_wait_for_buffer(&card->write);
	memcpy(iob->data, ULP_SETUP, ULP_SETUP_SIZE);

	memcpy(QETH_ULP_SETUP_DEST_ADDR(iob->data),
	       &card->token.cm_connection_r, QETH_MPC_TOKEN_LENGTH);
	memcpy(QETH_ULP_SETUP_CONNECTION_TOKEN(iob->data),
	       &card->token.ulp_connection_w, QETH_MPC_TOKEN_LENGTH);
	memcpy(QETH_ULP_SETUP_FILTER_TOKEN(iob->data),
	       &card->token.ulp_filter_r, QETH_MPC_TOKEN_LENGTH);

	temp = __raw_devno_from_bus_id(CARD_DDEV_ID(card));
	memcpy(QETH_ULP_SETUP_CUA(iob->data), &temp, 2);
	temp = (card->info.cula << 8) + card->info.unit_addr2;
	memcpy(QETH_ULP_SETUP_REAL_DEVADDR(iob->data), &temp, 2);
	rc = qeth_send_control_data(card, ULP_SETUP_SIZE, iob,
				    qeth_ulp_setup_cb, NULL);
	return rc;
}

static inline int
qeth_check_qdio_errors(struct qdio_buffer *buf, unsigned int qdio_error,
		       unsigned int siga_error, const char *dbftext)
{
	if (qdio_error || siga_error) {
		QETH_DBF_TEXT(trace, 2, dbftext);
		QETH_DBF_TEXT(qerr, 2, dbftext);
		QETH_DBF_TEXT_(qerr, 2, " F15=%02X",
			       buf->element[15].flags & 0xff);
		QETH_DBF_TEXT_(qerr, 2, " F14=%02X",
			       buf->element[14].flags & 0xff);
		QETH_DBF_TEXT_(qerr, 2, " qerr=%X", qdio_error);
		QETH_DBF_TEXT_(qerr, 2, " serr=%X", siga_error);
		return 1;
	}
	return 0;
}

static inline struct sk_buff *
qeth_get_skb(unsigned int length, struct qeth_hdr *hdr)
{
	struct sk_buff* skb;
	int add_len;

	add_len = 0;
	if (hdr->hdr.osn.id == QETH_HEADER_TYPE_OSN)
		add_len = sizeof(struct qeth_hdr);
#ifdef CONFIG_QETH_VLAN
	else
		add_len = VLAN_HLEN;
#endif
	skb = dev_alloc_skb(length + add_len);
	if (skb && add_len)
		skb_reserve(skb, add_len);
	return skb;
}

static inline struct sk_buff *
qeth_get_next_skb(struct qeth_card *card, struct qdio_buffer *buffer,
		  struct qdio_buffer_element **__element, int *__offset,
		  struct qeth_hdr **hdr)
{
	struct qdio_buffer_element *element = *__element;
	int offset = *__offset;
	struct sk_buff *skb = NULL;
	int skb_len;
	void *data_ptr;
	int data_len;

	QETH_DBF_TEXT(trace,6,"nextskb");
	/* qeth_hdr must not cross element boundaries */
	if (element->length < offset + sizeof(struct qeth_hdr)){
		if (qeth_is_last_sbale(element))
			return NULL;
		element++;
		offset = 0;
		if (element->length < sizeof(struct qeth_hdr))
			return NULL;
	}
	*hdr = element->addr + offset;

	offset += sizeof(struct qeth_hdr);
	if (card->options.layer2)
		if (card->info.type == QETH_CARD_TYPE_OSN)
			skb_len = (*hdr)->hdr.osn.pdu_length;
		else
			skb_len = (*hdr)->hdr.l2.pkt_length;
	else
		skb_len = (*hdr)->hdr.l3.length;

	if (!skb_len)
		return NULL;
	if (card->options.fake_ll){
		if(card->dev->type == ARPHRD_IEEE802_TR){
			if (!(skb = qeth_get_skb(skb_len+QETH_FAKE_LL_LEN_TR, *hdr)))
				goto no_mem;
			skb_reserve(skb,QETH_FAKE_LL_LEN_TR);
		} else {
			if (!(skb = qeth_get_skb(skb_len+QETH_FAKE_LL_LEN_ETH, *hdr)))
				goto no_mem;
			skb_reserve(skb,QETH_FAKE_LL_LEN_ETH);
		}
	} else if (!(skb = qeth_get_skb(skb_len, *hdr)))
		goto no_mem;
	data_ptr = element->addr + offset;
	while (skb_len) {
		data_len = min(skb_len, (int)(element->length - offset));
		if (data_len)
			memcpy(skb_put(skb, data_len), data_ptr, data_len);
		skb_len -= data_len;
		if (skb_len){
			if (qeth_is_last_sbale(element)){
				QETH_DBF_TEXT(trace,4,"unexeob");
				QETH_DBF_TEXT_(trace,4,"%s",CARD_BUS_ID(card));
				QETH_DBF_TEXT(qerr,2,"unexeob");
				QETH_DBF_TEXT_(qerr,2,"%s",CARD_BUS_ID(card));
				QETH_DBF_HEX(misc,4,buffer,sizeof(*buffer));
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
	return skb;
no_mem:
	if (net_ratelimit()){
		PRINT_WARN("No memory for packet received on %s.\n",
			   QETH_CARD_IFNAME(card));
		QETH_DBF_TEXT(trace,2,"noskbmem");
		QETH_DBF_TEXT_(trace,2,"%s",CARD_BUS_ID(card));
	}
	card->stats.rx_dropped++;
	return NULL;
}

static inline __be16
qeth_type_trans(struct sk_buff *skb, struct net_device *dev)
{
	struct qeth_card *card;
	struct ethhdr *eth;

	QETH_DBF_TEXT(trace,6,"typtrans");

	card = (struct qeth_card *)dev->priv;
#ifdef CONFIG_TR
	if ((card->info.link_type == QETH_LINK_TYPE_HSTR) ||
	    (card->info.link_type == QETH_LINK_TYPE_LANE_TR))
	 	return tr_type_trans(skb,dev);
#endif /* CONFIG_TR */
	skb->mac.raw = skb->data;
	skb_pull(skb, ETH_HLEN );
	eth = eth_hdr(skb);

	if (*eth->h_dest & 1) {
		if (memcmp(eth->h_dest, dev->broadcast, ETH_ALEN) == 0)
			skb->pkt_type = PACKET_BROADCAST;
		else
			skb->pkt_type = PACKET_MULTICAST;
	} else if (memcmp(eth->h_dest, dev->dev_addr, ETH_ALEN))
		skb->pkt_type = PACKET_OTHERHOST;

	if (ntohs(eth->h_proto) >= 1536)
		return eth->h_proto;
	if (*(unsigned short *) (skb->data) == 0xFFFF)
		return htons(ETH_P_802_3);
	return htons(ETH_P_802_2);
}

static inline void
qeth_rebuild_skb_fake_ll_tr(struct qeth_card *card, struct sk_buff *skb,
			 struct qeth_hdr *hdr)
{
	struct trh_hdr *fake_hdr;
	struct trllc *fake_llc;
	struct iphdr *ip_hdr;

	QETH_DBF_TEXT(trace,5,"skbfktr");
	skb->mac.raw = skb->data - QETH_FAKE_LL_LEN_TR;
	/* this is a fake ethernet header */
	fake_hdr = (struct trh_hdr *) skb->mac.raw;

	/* the destination MAC address */
	switch (skb->pkt_type){
	case PACKET_MULTICAST:
		switch (skb->protocol){
#ifdef CONFIG_QETH_IPV6
		case __constant_htons(ETH_P_IPV6):
			ndisc_mc_map((struct in6_addr *)
				     skb->data + QETH_FAKE_LL_V6_ADDR_POS,
				     fake_hdr->daddr, card->dev, 0);
			break;
#endif /* CONFIG_QETH_IPV6 */
		case __constant_htons(ETH_P_IP):
			ip_hdr = (struct iphdr *)skb->data;
			ip_tr_mc_map(ip_hdr->daddr, fake_hdr->daddr);
			break;
		default:
			memcpy(fake_hdr->daddr, card->dev->dev_addr, TR_ALEN);
		}
		break;
	case PACKET_BROADCAST:
		memset(fake_hdr->daddr, 0xff, TR_ALEN);
		break;
	default:
		memcpy(fake_hdr->daddr, card->dev->dev_addr, TR_ALEN);
	}
	/* the source MAC address */
	if (hdr->hdr.l3.ext_flags & QETH_HDR_EXT_SRC_MAC_ADDR)
		memcpy(fake_hdr->saddr, &hdr->hdr.l3.dest_addr[2], TR_ALEN);
	else
		memset(fake_hdr->saddr, 0, TR_ALEN);
	fake_hdr->rcf=0;
	fake_llc = (struct trllc*)&(fake_hdr->rcf);
	fake_llc->dsap = EXTENDED_SAP;
	fake_llc->ssap = EXTENDED_SAP;
	fake_llc->llc  = UI_CMD;
	fake_llc->protid[0] = 0;
	fake_llc->protid[1] = 0;
	fake_llc->protid[2] = 0;
	fake_llc->ethertype = ETH_P_IP;
}

static inline void
qeth_rebuild_skb_fake_ll_eth(struct qeth_card *card, struct sk_buff *skb,
			 struct qeth_hdr *hdr)
{
	struct ethhdr *fake_hdr;
	struct iphdr *ip_hdr;

	QETH_DBF_TEXT(trace,5,"skbfketh");
	skb->mac.raw = skb->data - QETH_FAKE_LL_LEN_ETH;
	/* this is a fake ethernet header */
	fake_hdr = (struct ethhdr *) skb->mac.raw;

	/* the destination MAC address */
	switch (skb->pkt_type){
	case PACKET_MULTICAST:
		switch (skb->protocol){
#ifdef CONFIG_QETH_IPV6
		case __constant_htons(ETH_P_IPV6):
			ndisc_mc_map((struct in6_addr *)
				     skb->data + QETH_FAKE_LL_V6_ADDR_POS,
				     fake_hdr->h_dest, card->dev, 0);
			break;
#endif /* CONFIG_QETH_IPV6 */
		case __constant_htons(ETH_P_IP):
			ip_hdr = (struct iphdr *)skb->data;
			ip_eth_mc_map(ip_hdr->daddr, fake_hdr->h_dest);
			break;
		default:
			memcpy(fake_hdr->h_dest, card->dev->dev_addr, ETH_ALEN);
		}
		break;
	case PACKET_BROADCAST:
		memset(fake_hdr->h_dest, 0xff, ETH_ALEN);
		break;
	default:
		memcpy(fake_hdr->h_dest, card->dev->dev_addr, ETH_ALEN);
	}
	/* the source MAC address */
	if (hdr->hdr.l3.ext_flags & QETH_HDR_EXT_SRC_MAC_ADDR)
		memcpy(fake_hdr->h_source, &hdr->hdr.l3.dest_addr[2], ETH_ALEN);
	else
		memset(fake_hdr->h_source, 0, ETH_ALEN);
	/* the protocol */
	fake_hdr->h_proto = skb->protocol;
}

static inline void
qeth_rebuild_skb_fake_ll(struct qeth_card *card, struct sk_buff *skb,
			struct qeth_hdr *hdr)
{
	if (card->dev->type == ARPHRD_IEEE802_TR)
		qeth_rebuild_skb_fake_ll_tr(card, skb, hdr);
	else
		qeth_rebuild_skb_fake_ll_eth(card, skb, hdr);
}

static inline void
qeth_rebuild_skb_vlan(struct qeth_card *card, struct sk_buff *skb,
		      struct qeth_hdr *hdr)
{
#ifdef CONFIG_QETH_VLAN
	u16 *vlan_tag;

	if (hdr->hdr.l3.ext_flags &
	    (QETH_HDR_EXT_VLAN_FRAME | QETH_HDR_EXT_INCLUDE_VLAN_TAG)) {
		vlan_tag = (u16 *) skb_push(skb, VLAN_HLEN);
		*vlan_tag = (hdr->hdr.l3.ext_flags & QETH_HDR_EXT_VLAN_FRAME)?
			hdr->hdr.l3.vlan_id : *((u16 *)&hdr->hdr.l3.dest_addr[12]);
		*(vlan_tag + 1) = skb->protocol;
		skb->protocol = __constant_htons(ETH_P_8021Q);
	}
#endif /* CONFIG_QETH_VLAN */
}

static inline __u16
qeth_layer2_rebuild_skb(struct qeth_card *card, struct sk_buff *skb,
			struct qeth_hdr *hdr)
{
	unsigned short vlan_id = 0;
#ifdef CONFIG_QETH_VLAN
	struct vlan_hdr *vhdr;
#endif

	skb->pkt_type = PACKET_HOST;
	skb->protocol = qeth_type_trans(skb, skb->dev);
	if (card->options.checksum_type == NO_CHECKSUMMING)
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	else
		skb->ip_summed = CHECKSUM_NONE;
#ifdef CONFIG_QETH_VLAN
	if (hdr->hdr.l2.flags[2] & (QETH_LAYER2_FLAG_VLAN)) {
		vhdr = (struct vlan_hdr *) skb->data;
		skb->protocol =
			__constant_htons(vhdr->h_vlan_encapsulated_proto);
		vlan_id = hdr->hdr.l2.vlan_id;
		skb_pull(skb, VLAN_HLEN);
	}
#endif
	*((__u32 *)skb->cb) = ++card->seqno.pkt_seqno;
	return vlan_id;
}

static inline void
qeth_rebuild_skb(struct qeth_card *card, struct sk_buff *skb,
		 struct qeth_hdr *hdr)
{
#ifdef CONFIG_QETH_IPV6
	if (hdr->hdr.l3.flags & QETH_HDR_PASSTHRU) {
		skb->pkt_type = PACKET_HOST;
		skb->protocol = qeth_type_trans(skb, card->dev);
		return;
	}
#endif /* CONFIG_QETH_IPV6 */
	skb->protocol = htons((hdr->hdr.l3.flags & QETH_HDR_IPV6)? ETH_P_IPV6 :
			      ETH_P_IP);
	switch (hdr->hdr.l3.flags & QETH_HDR_CAST_MASK){
	case QETH_CAST_UNICAST:
		skb->pkt_type = PACKET_HOST;
		break;
	case QETH_CAST_MULTICAST:
		skb->pkt_type = PACKET_MULTICAST;
		card->stats.multicast++;
		break;
	case QETH_CAST_BROADCAST:
		skb->pkt_type = PACKET_BROADCAST;
		card->stats.multicast++;
		break;
	case QETH_CAST_ANYCAST:
	case QETH_CAST_NOCAST:
	default:
		skb->pkt_type = PACKET_HOST;
	}
	qeth_rebuild_skb_vlan(card, skb, hdr);
	if (card->options.fake_ll)
		qeth_rebuild_skb_fake_ll(card, skb, hdr);
	else
		skb->mac.raw = skb->data;
	skb->ip_summed = card->options.checksum_type;
	if (card->options.checksum_type == HW_CHECKSUMMING){
		if ( (hdr->hdr.l3.ext_flags &
		      (QETH_HDR_EXT_CSUM_HDR_REQ |
		       QETH_HDR_EXT_CSUM_TRANSP_REQ)) ==
		     (QETH_HDR_EXT_CSUM_HDR_REQ |
		      QETH_HDR_EXT_CSUM_TRANSP_REQ) )
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb->ip_summed = SW_CHECKSUMMING;
	}
}

static inline void
qeth_process_inbound_buffer(struct qeth_card *card,
			    struct qeth_qdio_buffer *buf, int index)
{
	struct qdio_buffer_element *element;
	struct sk_buff *skb;
	struct qeth_hdr *hdr;
	int offset;
	int rxrc;
	__u16 vlan_tag = 0;

	/* get first element of current buffer */
	element = (struct qdio_buffer_element *)&buf->buffer->element[0];
	offset = 0;
#ifdef CONFIG_QETH_PERF_STATS
	card->perf_stats.bufs_rec++;
#endif
	while((skb = qeth_get_next_skb(card, buf->buffer, &element,
				       &offset, &hdr))) {
		skb->dev = card->dev;
		if (hdr->hdr.l2.id == QETH_HEADER_TYPE_LAYER2)
			vlan_tag = qeth_layer2_rebuild_skb(card, skb, hdr);
		else if (hdr->hdr.l3.id == QETH_HEADER_TYPE_LAYER3)     
			qeth_rebuild_skb(card, skb, hdr);
		else { /*in case of OSN*/
			skb_push(skb, sizeof(struct qeth_hdr));
			memcpy(skb->data, hdr, sizeof(struct qeth_hdr));
		}
		/* is device UP ? */
		if (!(card->dev->flags & IFF_UP)){
			dev_kfree_skb_any(skb);
			continue;
		}
#ifdef CONFIG_QETH_VLAN
		if (vlan_tag)
			vlan_hwaccel_rx(skb, card->vlangrp, vlan_tag);
		else
#endif
		if (card->info.type == QETH_CARD_TYPE_OSN)
			rxrc = card->osn_info.data_cb(skb);
		else
			rxrc = netif_rx(skb);
		card->dev->last_rx = jiffies;
		card->stats.rx_packets++;
		card->stats.rx_bytes += skb->len;
	}
}

static inline struct qeth_buffer_pool_entry *
qeth_get_buffer_pool_entry(struct qeth_card *card)
{
	struct qeth_buffer_pool_entry *entry;

	QETH_DBF_TEXT(trace, 6, "gtbfplen");
	if (!list_empty(&card->qdio.in_buf_pool.entry_list)) {
		entry = list_entry(card->qdio.in_buf_pool.entry_list.next,
				struct qeth_buffer_pool_entry, list);
		list_del_init(&entry->list);
		return entry;
	}
	return NULL;
}

static inline void
qeth_init_input_buffer(struct qeth_card *card, struct qeth_qdio_buffer *buf)
{
	struct qeth_buffer_pool_entry *pool_entry;
	int i;

	pool_entry = qeth_get_buffer_pool_entry(card);
	/*
	 * since the buffer is accessed only from the input_tasklet
	 * there shouldn't be a need to synchronize; also, since we use
	 * the QETH_IN_BUF_REQUEUE_THRESHOLD we should never run  out off
	 * buffers
	 */
	BUG_ON(!pool_entry);

	buf->pool_entry = pool_entry;
	for(i = 0; i < QETH_MAX_BUFFER_ELEMENTS(card); ++i){
		buf->buffer->element[i].length = PAGE_SIZE;
		buf->buffer->element[i].addr =  pool_entry->elements[i];
		if (i == QETH_MAX_BUFFER_ELEMENTS(card) - 1)
			buf->buffer->element[i].flags = SBAL_FLAGS_LAST_ENTRY;
		else
			buf->buffer->element[i].flags = 0;
	}
	buf->state = QETH_QDIO_BUF_EMPTY;
}

static inline void
qeth_clear_output_buffer(struct qeth_qdio_out_q *queue,
			 struct qeth_qdio_out_buffer *buf)
{
	int i;
	struct sk_buff *skb;

	/* is PCI flag set on buffer? */
	if (buf->buffer->element[0].flags & 0x40)
		atomic_dec(&queue->set_pci_flags_count);

	while ((skb = skb_dequeue(&buf->skb_list))){
		atomic_dec(&skb->users);
		dev_kfree_skb_any(skb);
	}
	qeth_eddp_buf_release_contexts(buf);
	for(i = 0; i < QETH_MAX_BUFFER_ELEMENTS(queue->card); ++i){
		buf->buffer->element[i].length = 0;
		buf->buffer->element[i].addr = NULL;
		buf->buffer->element[i].flags = 0;
	}
	buf->next_element_to_fill = 0;
	atomic_set(&buf->state, QETH_QDIO_BUF_EMPTY);
}

static inline void
qeth_queue_input_buffer(struct qeth_card *card, int index)
{
	struct qeth_qdio_q *queue = card->qdio.in_q;
	int count;
	int i;
	int rc;

	QETH_DBF_TEXT(trace,6,"queinbuf");
	count = (index < queue->next_buf_to_init)?
		card->qdio.in_buf_pool.buf_count -
		(queue->next_buf_to_init - index) :
		card->qdio.in_buf_pool.buf_count -
		(queue->next_buf_to_init + QDIO_MAX_BUFFERS_PER_Q - index);
	/* only requeue at a certain threshold to avoid SIGAs */
	if (count >= QETH_IN_BUF_REQUEUE_THRESHOLD(card)){
		for (i = queue->next_buf_to_init;
		     i < queue->next_buf_to_init + count; ++i)
			qeth_init_input_buffer(card,
				&queue->bufs[i % QDIO_MAX_BUFFERS_PER_Q]);
		/*
		 * according to old code it should be avoided to requeue all
		 * 128 buffers in order to benefit from PCI avoidance.
		 * this function keeps at least one buffer (the buffer at
		 * 'index') un-requeued -> this buffer is the first buffer that
		 * will be requeued the next time
		 */
#ifdef CONFIG_QETH_PERF_STATS
		card->perf_stats.inbound_do_qdio_cnt++;
		card->perf_stats.inbound_do_qdio_start_time = qeth_get_micros();
#endif
		rc = do_QDIO(CARD_DDEV(card),
			     QDIO_FLAG_SYNC_INPUT | QDIO_FLAG_UNDER_INTERRUPT,
			     0, queue->next_buf_to_init, count, NULL);
#ifdef CONFIG_QETH_PERF_STATS
		card->perf_stats.inbound_do_qdio_time += qeth_get_micros() -
			card->perf_stats.inbound_do_qdio_start_time;
#endif
		if (rc){
			PRINT_WARN("qeth_queue_input_buffer's do_QDIO "
				   "return %i (device %s).\n",
				   rc, CARD_DDEV_ID(card));
			QETH_DBF_TEXT(trace,2,"qinberr");
			QETH_DBF_TEXT_(trace,2,"%s",CARD_BUS_ID(card));
		}
		queue->next_buf_to_init = (queue->next_buf_to_init + count) %
					  QDIO_MAX_BUFFERS_PER_Q;
	}
}

static inline void
qeth_put_buffer_pool_entry(struct qeth_card *card,
			   struct qeth_buffer_pool_entry *entry)
{
	QETH_DBF_TEXT(trace, 6, "ptbfplen");
	list_add_tail(&entry->list, &card->qdio.in_buf_pool.entry_list);
}

static void
qeth_qdio_input_handler(struct ccw_device * ccwdev, unsigned int status,
		        unsigned int qdio_err, unsigned int siga_err,
			unsigned int queue, int first_element, int count,
			unsigned long card_ptr)
{
	struct net_device *net_dev;
	struct qeth_card *card;
	struct qeth_qdio_buffer *buffer;
	int index;
	int i;

	QETH_DBF_TEXT(trace, 6, "qdinput");
	card = (struct qeth_card *) card_ptr;
	net_dev = card->dev;
#ifdef CONFIG_QETH_PERF_STATS
	card->perf_stats.inbound_cnt++;
	card->perf_stats.inbound_start_time = qeth_get_micros();
#endif
	if (status & QDIO_STATUS_LOOK_FOR_ERROR) {
		if (status & QDIO_STATUS_ACTIVATE_CHECK_CONDITION){
			QETH_DBF_TEXT(trace, 1,"qdinchk");
			QETH_DBF_TEXT_(trace,1,"%s",CARD_BUS_ID(card));
			QETH_DBF_TEXT_(trace,1,"%04X%04X",first_element,count);
			QETH_DBF_TEXT_(trace,1,"%04X%04X", queue, status);
			qeth_schedule_recovery(card);
			return;
		}
	}
	for (i = first_element; i < (first_element + count); ++i) {
		index = i % QDIO_MAX_BUFFERS_PER_Q;
		buffer = &card->qdio.in_q->bufs[index];
		if (!((status & QDIO_STATUS_LOOK_FOR_ERROR) &&
		      qeth_check_qdio_errors(buffer->buffer, 
					     qdio_err, siga_err,"qinerr")))
			qeth_process_inbound_buffer(card, buffer, index);
		/* clear buffer and give back to hardware */
		qeth_put_buffer_pool_entry(card, buffer->pool_entry);
		qeth_queue_input_buffer(card, index);
	}
#ifdef CONFIG_QETH_PERF_STATS
	card->perf_stats.inbound_time += qeth_get_micros() -
		card->perf_stats.inbound_start_time;
#endif
}

static inline int
qeth_handle_send_error(struct qeth_card *card,
		       struct qeth_qdio_out_buffer *buffer,
		       unsigned int qdio_err, unsigned int siga_err)
{
	int sbalf15 = buffer->buffer->element[15].flags & 0xff;
	int cc = siga_err & 3;

	QETH_DBF_TEXT(trace, 6, "hdsnderr");
	qeth_check_qdio_errors(buffer->buffer, qdio_err, siga_err, "qouterr");
	switch (cc) {
	case 0:
		if (qdio_err){
			QETH_DBF_TEXT(trace, 1,"lnkfail");
			QETH_DBF_TEXT_(trace,1,"%s",CARD_BUS_ID(card));
			QETH_DBF_TEXT_(trace,1,"%04x %02x",
				       (u16)qdio_err, (u8)sbalf15);
			return QETH_SEND_ERROR_LINK_FAILURE;
		}
		return QETH_SEND_ERROR_NONE;
	case 2:
		if (siga_err & QDIO_SIGA_ERROR_B_BIT_SET) {
			QETH_DBF_TEXT(trace, 1, "SIGAcc2B");
			QETH_DBF_TEXT_(trace,1,"%s",CARD_BUS_ID(card));
			return QETH_SEND_ERROR_KICK_IT;
		}
		if ((sbalf15 >= 15) && (sbalf15 <= 31))
			return QETH_SEND_ERROR_RETRY;
		return QETH_SEND_ERROR_LINK_FAILURE;
		/* look at qdio_error and sbalf 15 */
	case 1:
		QETH_DBF_TEXT(trace, 1, "SIGAcc1");
		QETH_DBF_TEXT_(trace,1,"%s",CARD_BUS_ID(card));
		return QETH_SEND_ERROR_LINK_FAILURE;
	case 3:
		QETH_DBF_TEXT(trace, 1, "SIGAcc3");
		QETH_DBF_TEXT_(trace,1,"%s",CARD_BUS_ID(card));
		return QETH_SEND_ERROR_KICK_IT;
	}
	return QETH_SEND_ERROR_LINK_FAILURE;
}

void
qeth_flush_buffers(struct qeth_qdio_out_q *queue, int under_int,
		   int index, int count)
{
	struct qeth_qdio_out_buffer *buf;
	int rc;
	int i;

	QETH_DBF_TEXT(trace, 6, "flushbuf");

	for (i = index; i < index + count; ++i) {
		buf = &queue->bufs[i % QDIO_MAX_BUFFERS_PER_Q];
		buf->buffer->element[buf->next_element_to_fill - 1].flags |=
				SBAL_FLAGS_LAST_ENTRY;

		if (queue->card->info.type == QETH_CARD_TYPE_IQD)
			continue;

		if (!queue->do_pack){
			if ((atomic_read(&queue->used_buffers) >=
		    		(QETH_HIGH_WATERMARK_PACK -
				 QETH_WATERMARK_PACK_FUZZ)) &&
		    	    !atomic_read(&queue->set_pci_flags_count)){
				/* it's likely that we'll go to packing
				 * mode soon */
				atomic_inc(&queue->set_pci_flags_count);
				buf->buffer->element[0].flags |= 0x40;
			}
		} else {
			if (!atomic_read(&queue->set_pci_flags_count)){
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

	queue->card->dev->trans_start = jiffies;
#ifdef CONFIG_QETH_PERF_STATS
	queue->card->perf_stats.outbound_do_qdio_cnt++;
	queue->card->perf_stats.outbound_do_qdio_start_time = qeth_get_micros();
#endif
	if (under_int)
		rc = do_QDIO(CARD_DDEV(queue->card),
			     QDIO_FLAG_SYNC_OUTPUT | QDIO_FLAG_UNDER_INTERRUPT,
			     queue->queue_no, index, count, NULL);
	else
		rc = do_QDIO(CARD_DDEV(queue->card), QDIO_FLAG_SYNC_OUTPUT,
			     queue->queue_no, index, count, NULL);
#ifdef CONFIG_QETH_PERF_STATS
	queue->card->perf_stats.outbound_do_qdio_time += qeth_get_micros() -
		queue->card->perf_stats.outbound_do_qdio_start_time;
#endif
	if (rc){
		QETH_DBF_TEXT(trace, 2, "flushbuf");
		QETH_DBF_TEXT_(trace, 2, " err%d", rc);
		QETH_DBF_TEXT_(trace, 2, "%s", CARD_DDEV_ID(queue->card));
		queue->card->stats.tx_errors += count;
		/* this must not happen under normal circumstances. if it
		 * happens something is really wrong -> recover */
		qeth_schedule_recovery(queue->card);
		return;
	}
	atomic_add(count, &queue->used_buffers);
#ifdef CONFIG_QETH_PERF_STATS
	queue->card->perf_stats.bufs_sent += count;
#endif
}

/*
 * Switched to packing state if the number of used buffers on a queue
 * reaches a certain limit.
 */
static inline void
qeth_switch_to_packing_if_needed(struct qeth_qdio_out_q *queue)
{
	if (!queue->do_pack) {
		if (atomic_read(&queue->used_buffers)
		    >= QETH_HIGH_WATERMARK_PACK){
			/* switch non-PACKING -> PACKING */
			QETH_DBF_TEXT(trace, 6, "np->pack");
#ifdef CONFIG_QETH_PERF_STATS
			queue->card->perf_stats.sc_dp_p++;
#endif
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
static inline int
qeth_switch_to_nonpacking_if_needed(struct qeth_qdio_out_q *queue)
{
	struct qeth_qdio_out_buffer *buffer;
	int flush_count = 0;

	if (queue->do_pack) {
		if (atomic_read(&queue->used_buffers)
		    <= QETH_LOW_WATERMARK_PACK) {
			/* switch PACKING -> non-PACKING */
			QETH_DBF_TEXT(trace, 6, "pack->np");
#ifdef CONFIG_QETH_PERF_STATS
			queue->card->perf_stats.sc_p_dp++;
#endif
			queue->do_pack = 0;
			/* flush packing buffers */
			buffer = &queue->bufs[queue->next_buf_to_fill];
			if ((atomic_read(&buffer->state) ==
						QETH_QDIO_BUF_EMPTY) &&
			    (buffer->next_element_to_fill > 0)) {
				atomic_set(&buffer->state,QETH_QDIO_BUF_PRIMED);
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
static inline int
qeth_flush_buffers_on_no_pci(struct qeth_qdio_out_q *queue)
{
	struct qeth_qdio_out_buffer *buffer;

	buffer = &queue->bufs[queue->next_buf_to_fill];
	if((atomic_read(&buffer->state) == QETH_QDIO_BUF_EMPTY) &&
	   (buffer->next_element_to_fill > 0)){
		/* it's a packing buffer */
		atomic_set(&buffer->state, QETH_QDIO_BUF_PRIMED);
		queue->next_buf_to_fill =
			(queue->next_buf_to_fill + 1) % QDIO_MAX_BUFFERS_PER_Q;
		return 1;
	}
	return 0;
}

static inline void
qeth_check_outbound_queue(struct qeth_qdio_out_q *queue)
{
	int index;
	int flush_cnt = 0;
	int q_was_packing = 0;

	/*
	 * check if weed have to switch to non-packing mode or if
	 * we have to get a pci flag out on the queue
	 */
	if ((atomic_read(&queue->used_buffers) <= QETH_LOW_WATERMARK_PACK) ||
	    !atomic_read(&queue->set_pci_flags_count)){
		if (atomic_swap(&queue->state, QETH_OUT_Q_LOCKED_FLUSH) ==
				QETH_OUT_Q_UNLOCKED) {
			/*
			 * If we get in here, there was no action in
			 * do_send_packet. So, we check if there is a
			 * packing buffer to be flushed here.
			 */
			netif_stop_queue(queue->card->dev);
			index = queue->next_buf_to_fill;
			q_was_packing = queue->do_pack;
			flush_cnt += qeth_switch_to_nonpacking_if_needed(queue);
			if (!flush_cnt &&
			    !atomic_read(&queue->set_pci_flags_count))
				flush_cnt +=
					qeth_flush_buffers_on_no_pci(queue);
#ifdef CONFIG_QETH_PERF_STATS
			if (q_was_packing)
				queue->card->perf_stats.bufs_sent_pack +=
					flush_cnt;
#endif
			if (flush_cnt)
				qeth_flush_buffers(queue, 1, index, flush_cnt);
			atomic_set(&queue->state, QETH_OUT_Q_UNLOCKED);
		}
	}
}

static void
qeth_qdio_output_handler(struct ccw_device * ccwdev, unsigned int status,
		        unsigned int qdio_error, unsigned int siga_error,
			unsigned int __queue, int first_element, int count,
			unsigned long card_ptr)
{
	struct qeth_card *card        = (struct qeth_card *) card_ptr;
	struct qeth_qdio_out_q *queue = card->qdio.out_qs[__queue];
	struct qeth_qdio_out_buffer *buffer;
	int i;

	QETH_DBF_TEXT(trace, 6, "qdouhdl");
	if (status & QDIO_STATUS_LOOK_FOR_ERROR) {
		if (status & QDIO_STATUS_ACTIVATE_CHECK_CONDITION){
			QETH_DBF_TEXT(trace, 2, "achkcond");
			QETH_DBF_TEXT_(trace, 2, "%s", CARD_BUS_ID(card));
			QETH_DBF_TEXT_(trace, 2, "%08x", status);
			netif_stop_queue(card->dev);
			qeth_schedule_recovery(card);
			return;
		}
	}
#ifdef CONFIG_QETH_PERF_STATS
	card->perf_stats.outbound_handler_cnt++;
	card->perf_stats.outbound_handler_start_time = qeth_get_micros();
#endif
	for(i = first_element; i < (first_element + count); ++i){
		buffer = &queue->bufs[i % QDIO_MAX_BUFFERS_PER_Q];
		/*we only handle the KICK_IT error by doing a recovery */
		if (qeth_handle_send_error(card, buffer,
					   qdio_error, siga_error)
				== QETH_SEND_ERROR_KICK_IT){
			netif_stop_queue(card->dev);
			qeth_schedule_recovery(card);
			return;
		}
		qeth_clear_output_buffer(queue, buffer);
	}
	atomic_sub(count, &queue->used_buffers);
	/* check if we need to do something on this outbound queue */
	if (card->info.type != QETH_CARD_TYPE_IQD)
		qeth_check_outbound_queue(queue);

	netif_wake_queue(queue->card->dev);
#ifdef CONFIG_QETH_PERF_STATS
	card->perf_stats.outbound_handler_time += qeth_get_micros() -
		card->perf_stats.outbound_handler_start_time;
#endif
}

static void
qeth_create_qib_param_field(struct qeth_card *card, char *param_field)
{

	param_field[0] = _ascebc['P'];
	param_field[1] = _ascebc['C'];
	param_field[2] = _ascebc['I'];
	param_field[3] = _ascebc['T'];
	*((unsigned int *) (&param_field[4])) = QETH_PCI_THRESHOLD_A(card);
	*((unsigned int *) (&param_field[8])) = QETH_PCI_THRESHOLD_B(card);
	*((unsigned int *) (&param_field[12])) = QETH_PCI_TIMER_VALUE(card);
}

static void
qeth_create_qib_param_field_blkt(struct qeth_card *card, char *param_field)
{
	param_field[16] = _ascebc['B'];
        param_field[17] = _ascebc['L'];
        param_field[18] = _ascebc['K'];
        param_field[19] = _ascebc['T'];
        *((unsigned int *) (&param_field[20])) = card->info.blkt.time_total;
        *((unsigned int *) (&param_field[24])) = card->info.blkt.inter_packet;
        *((unsigned int *) (&param_field[28])) = card->info.blkt.inter_packet_jumbo;
}

static void
qeth_initialize_working_pool_list(struct qeth_card *card)
{
	struct qeth_buffer_pool_entry *entry;

	QETH_DBF_TEXT(trace,5,"inwrklst");

	list_for_each_entry(entry,
			    &card->qdio.init_pool.entry_list, init_list) {
		qeth_put_buffer_pool_entry(card,entry);
	}
}

static void
qeth_clear_working_pool_list(struct qeth_card *card)
{
	struct qeth_buffer_pool_entry *pool_entry, *tmp;

	QETH_DBF_TEXT(trace,5,"clwrklst");
	list_for_each_entry_safe(pool_entry, tmp,
			    &card->qdio.in_buf_pool.entry_list, list){
			list_del(&pool_entry->list);
	}
}

static void
qeth_free_buffer_pool(struct qeth_card *card)
{
	struct qeth_buffer_pool_entry *pool_entry, *tmp;
	int i=0;
	QETH_DBF_TEXT(trace,5,"freepool");
	list_for_each_entry_safe(pool_entry, tmp,
				 &card->qdio.init_pool.entry_list, init_list){
		for (i = 0; i < QETH_MAX_BUFFER_ELEMENTS(card); ++i)
			free_page((unsigned long)pool_entry->elements[i]);
		list_del(&pool_entry->init_list);
		kfree(pool_entry);
	}
}

static int
qeth_alloc_buffer_pool(struct qeth_card *card)
{
	struct qeth_buffer_pool_entry *pool_entry;
	void *ptr;
	int i, j;

	QETH_DBF_TEXT(trace,5,"alocpool");
	for (i = 0; i < card->qdio.init_pool.buf_count; ++i){
	 	pool_entry = kmalloc(sizeof(*pool_entry), GFP_KERNEL);
		if (!pool_entry){
			qeth_free_buffer_pool(card);
			return -ENOMEM;
		}
		for(j = 0; j < QETH_MAX_BUFFER_ELEMENTS(card); ++j){
			ptr = (void *) __get_free_page(GFP_KERNEL|GFP_DMA);
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

int
qeth_realloc_buffer_pool(struct qeth_card *card, int bufcnt)
{
	QETH_DBF_TEXT(trace, 2, "realcbp");

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

static int
qeth_alloc_qdio_buffers(struct qeth_card *card)
{
	int i, j;

	QETH_DBF_TEXT(setup, 2, "allcqdbf");

	if (card->qdio.state == QETH_QDIO_ALLOCATED)
		return 0;

	card->qdio.in_q = kmalloc(sizeof(struct qeth_qdio_q), 
				  GFP_KERNEL|GFP_DMA);
	if (!card->qdio.in_q)
		return - ENOMEM;
	QETH_DBF_TEXT(setup, 2, "inq");
	QETH_DBF_HEX(setup, 2, &card->qdio.in_q, sizeof(void *));
	memset(card->qdio.in_q, 0, sizeof(struct qeth_qdio_q));
	/* give inbound qeth_qdio_buffers their qdio_buffers */
	for (i = 0; i < QDIO_MAX_BUFFERS_PER_Q; ++i)
		card->qdio.in_q->bufs[i].buffer =
			&card->qdio.in_q->qdio_bufs[i];
	/* inbound buffer pool */
	if (qeth_alloc_buffer_pool(card)){
		kfree(card->qdio.in_q);
		return -ENOMEM;
	}
	/* outbound */
	card->qdio.out_qs =
		kmalloc(card->qdio.no_out_queues *
			sizeof(struct qeth_qdio_out_q *), GFP_KERNEL);
	if (!card->qdio.out_qs){
		qeth_free_buffer_pool(card);
		return -ENOMEM;
	}
	for (i = 0; i < card->qdio.no_out_queues; ++i){
		card->qdio.out_qs[i] = kmalloc(sizeof(struct qeth_qdio_out_q),
					       GFP_KERNEL|GFP_DMA);
		if (!card->qdio.out_qs[i]){
			while (i > 0)
				kfree(card->qdio.out_qs[--i]);
			kfree(card->qdio.out_qs);
			return -ENOMEM;
		}
		QETH_DBF_TEXT_(setup, 2, "outq %i", i);
		QETH_DBF_HEX(setup, 2, &card->qdio.out_qs[i], sizeof(void *));
		memset(card->qdio.out_qs[i], 0, sizeof(struct qeth_qdio_out_q));
		card->qdio.out_qs[i]->queue_no = i;
		/* give outbound qeth_qdio_buffers their qdio_buffers */
		for (j = 0; j < QDIO_MAX_BUFFERS_PER_Q; ++j){
			card->qdio.out_qs[i]->bufs[j].buffer =
				&card->qdio.out_qs[i]->qdio_bufs[j];
			skb_queue_head_init(&card->qdio.out_qs[i]->bufs[j].
					    skb_list);
			INIT_LIST_HEAD(&card->qdio.out_qs[i]->bufs[j].ctx_list);
		}
	}
	card->qdio.state = QETH_QDIO_ALLOCATED;
	return 0;
}

static void
qeth_free_qdio_buffers(struct qeth_card *card)
{
	int i, j;

	QETH_DBF_TEXT(trace, 2, "freeqdbf");
	if (card->qdio.state == QETH_QDIO_UNINITIALIZED)
		return;
	kfree(card->qdio.in_q);
	/* inbound buffer pool */
	qeth_free_buffer_pool(card);
	/* free outbound qdio_qs */
	for (i = 0; i < card->qdio.no_out_queues; ++i){
		for (j = 0; j < QDIO_MAX_BUFFERS_PER_Q; ++j)
			qeth_clear_output_buffer(card->qdio.out_qs[i],
					&card->qdio.out_qs[i]->bufs[j]);
		kfree(card->qdio.out_qs[i]);
	}
	kfree(card->qdio.out_qs);
	card->qdio.state = QETH_QDIO_UNINITIALIZED;
}

static void
qeth_clear_qdio_buffers(struct qeth_card *card)
{
	int i, j;

	QETH_DBF_TEXT(trace, 2, "clearqdbf");
	/* clear outbound buffers to free skbs */
	for (i = 0; i < card->qdio.no_out_queues; ++i)
		if (card->qdio.out_qs[i]){
			for (j = 0; j < QDIO_MAX_BUFFERS_PER_Q; ++j)
				qeth_clear_output_buffer(card->qdio.out_qs[i],
						&card->qdio.out_qs[i]->bufs[j]);
		}
}

static void
qeth_init_qdio_info(struct qeth_card *card)
{
	QETH_DBF_TEXT(setup, 4, "intqdinf");
	card->qdio.state = QETH_QDIO_UNINITIALIZED;
	/* inbound */
	card->qdio.in_buf_size = QETH_IN_BUF_SIZE_DEFAULT;
	card->qdio.init_pool.buf_count = QETH_IN_BUF_COUNT_DEFAULT;
	card->qdio.in_buf_pool.buf_count = card->qdio.init_pool.buf_count;
	INIT_LIST_HEAD(&card->qdio.in_buf_pool.entry_list);
	INIT_LIST_HEAD(&card->qdio.init_pool.entry_list);
}

static int
qeth_init_qdio_queues(struct qeth_card *card)
{
	int i, j;
	int rc;

	QETH_DBF_TEXT(setup, 2, "initqdqs");

	/* inbound queue */
	memset(card->qdio.in_q->qdio_bufs, 0,
	       QDIO_MAX_BUFFERS_PER_Q * sizeof(struct qdio_buffer));
	qeth_initialize_working_pool_list(card);
	/*give only as many buffers to hardware as we have buffer pool entries*/
	for (i = 0; i < card->qdio.in_buf_pool.buf_count - 1; ++i)
		qeth_init_input_buffer(card, &card->qdio.in_q->bufs[i]);
	card->qdio.in_q->next_buf_to_init = card->qdio.in_buf_pool.buf_count - 1;
	rc = do_QDIO(CARD_DDEV(card), QDIO_FLAG_SYNC_INPUT, 0, 0,
		     card->qdio.in_buf_pool.buf_count - 1, NULL);
	if (rc) {
		QETH_DBF_TEXT_(setup, 2, "1err%d", rc);
		return rc;
	}
	rc = qdio_synchronize(CARD_DDEV(card), QDIO_FLAG_SYNC_INPUT, 0);
	if (rc) {
		QETH_DBF_TEXT_(setup, 2, "2err%d", rc);
		return rc;
	}
	/* outbound queue */
	for (i = 0; i < card->qdio.no_out_queues; ++i){
		memset(card->qdio.out_qs[i]->qdio_bufs, 0,
		       QDIO_MAX_BUFFERS_PER_Q * sizeof(struct qdio_buffer));
		for (j = 0; j < QDIO_MAX_BUFFERS_PER_Q; ++j){
			qeth_clear_output_buffer(card->qdio.out_qs[i],
					&card->qdio.out_qs[i]->bufs[j]);
		}
		card->qdio.out_qs[i]->card = card;
		card->qdio.out_qs[i]->next_buf_to_fill = 0;
		card->qdio.out_qs[i]->do_pack = 0;
		atomic_set(&card->qdio.out_qs[i]->used_buffers,0);
		atomic_set(&card->qdio.out_qs[i]->set_pci_flags_count, 0);
		atomic_set(&card->qdio.out_qs[i]->state,
			   QETH_OUT_Q_UNLOCKED);
	}
	return 0;
}

static int
qeth_qdio_establish(struct qeth_card *card)
{
	struct qdio_initialize init_data;
	char *qib_param_field;
	struct qdio_buffer **in_sbal_ptrs;
	struct qdio_buffer **out_sbal_ptrs;
	int i, j, k;
	int rc;

	QETH_DBF_TEXT(setup, 2, "qdioest");

	qib_param_field = kmalloc(QDIO_MAX_BUFFERS_PER_Q * sizeof(char),
			      GFP_KERNEL);
 	if (!qib_param_field)
		return -ENOMEM;

 	memset(qib_param_field, 0, QDIO_MAX_BUFFERS_PER_Q * sizeof(char));

	qeth_create_qib_param_field(card, qib_param_field);
	qeth_create_qib_param_field_blkt(card, qib_param_field);

	in_sbal_ptrs = kmalloc(QDIO_MAX_BUFFERS_PER_Q * sizeof(void *),
			       GFP_KERNEL);
	if (!in_sbal_ptrs) {
		kfree(qib_param_field);
		return -ENOMEM;
	}
	for(i = 0; i < QDIO_MAX_BUFFERS_PER_Q; ++i)
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
	for(i = 0, k = 0; i < card->qdio.no_out_queues; ++i)
		for(j = 0; j < QDIO_MAX_BUFFERS_PER_Q; ++j, ++k){
			out_sbal_ptrs[k] = (struct qdio_buffer *)
				virt_to_phys(card->qdio.out_qs[i]->
					     bufs[j].buffer);
		}

	memset(&init_data, 0, sizeof(struct qdio_initialize));
	init_data.cdev                   = CARD_DDEV(card);
	init_data.q_format               = qeth_get_qdio_q_format(card);
	init_data.qib_param_field_format = 0;
	init_data.qib_param_field        = qib_param_field;
	init_data.min_input_threshold    = QETH_MIN_INPUT_THRESHOLD;
	init_data.max_input_threshold    = QETH_MAX_INPUT_THRESHOLD;
	init_data.min_output_threshold   = QETH_MIN_OUTPUT_THRESHOLD;
	init_data.max_output_threshold   = QETH_MAX_OUTPUT_THRESHOLD;
	init_data.no_input_qs            = 1;
	init_data.no_output_qs           = card->qdio.no_out_queues;
	init_data.input_handler          = (qdio_handler_t *)
					   qeth_qdio_input_handler;
	init_data.output_handler         = (qdio_handler_t *)
					   qeth_qdio_output_handler;
	init_data.int_parm               = (unsigned long) card;
	init_data.flags                  = QDIO_INBOUND_0COPY_SBALS |
					   QDIO_OUTBOUND_0COPY_SBALS |
					   QDIO_USE_OUTBOUND_PCIS;
	init_data.input_sbal_addr_array  = (void **) in_sbal_ptrs;
	init_data.output_sbal_addr_array = (void **) out_sbal_ptrs;

	if (!(rc = qdio_initialize(&init_data)))
		card->qdio.state = QETH_QDIO_ESTABLISHED;

	kfree(out_sbal_ptrs);
	kfree(in_sbal_ptrs);
	kfree(qib_param_field);
	return rc;
}

static int
qeth_qdio_activate(struct qeth_card *card)
{
	QETH_DBF_TEXT(setup,3,"qdioact");
	return qdio_activate(CARD_DDEV(card), 0);
}

static int
qeth_clear_channel(struct qeth_channel *channel)
{
	unsigned long flags;
	struct qeth_card *card;
	int rc;

	QETH_DBF_TEXT(trace,3,"clearch");
	card = CARD_FROM_CDEV(channel->ccwdev);
	spin_lock_irqsave(get_ccwdev_lock(channel->ccwdev), flags);
	rc = ccw_device_clear(channel->ccwdev, QETH_CLEAR_CHANNEL_PARM);
	spin_unlock_irqrestore(get_ccwdev_lock(channel->ccwdev), flags);

	if (rc)
		return rc;
	rc = wait_event_interruptible_timeout(card->wait_q,
			channel->state==CH_STATE_STOPPED, QETH_TIMEOUT);
	if (rc == -ERESTARTSYS)
		return rc;
	if (channel->state != CH_STATE_STOPPED)
		return -ETIME;
	channel->state = CH_STATE_DOWN;
	return 0;
}

static int
qeth_halt_channel(struct qeth_channel *channel)
{
	unsigned long flags;
	struct qeth_card *card;
	int rc;

	QETH_DBF_TEXT(trace,3,"haltch");
	card = CARD_FROM_CDEV(channel->ccwdev);
	spin_lock_irqsave(get_ccwdev_lock(channel->ccwdev), flags);
	rc = ccw_device_halt(channel->ccwdev, QETH_HALT_CHANNEL_PARM);
	spin_unlock_irqrestore(get_ccwdev_lock(channel->ccwdev), flags);

	if (rc)
		return rc;
	rc = wait_event_interruptible_timeout(card->wait_q,
			channel->state==CH_STATE_HALTED, QETH_TIMEOUT);
	if (rc == -ERESTARTSYS)
		return rc;
	if (channel->state != CH_STATE_HALTED)
		return -ETIME;
	return 0;
}

static int
qeth_halt_channels(struct qeth_card *card)
{
	int rc1 = 0, rc2=0, rc3 = 0;

	QETH_DBF_TEXT(trace,3,"haltchs");
	rc1 = qeth_halt_channel(&card->read);
	rc2 = qeth_halt_channel(&card->write);
	rc3 = qeth_halt_channel(&card->data);
	if (rc1)
		return rc1;
	if (rc2) 
		return rc2;
	return rc3;
}
static int
qeth_clear_channels(struct qeth_card *card)
{
	int rc1 = 0, rc2=0, rc3 = 0;

	QETH_DBF_TEXT(trace,3,"clearchs");
	rc1 = qeth_clear_channel(&card->read);
	rc2 = qeth_clear_channel(&card->write);
	rc3 = qeth_clear_channel(&card->data);
	if (rc1)
		return rc1;
	if (rc2) 
		return rc2;
	return rc3;
}

static int
qeth_clear_halt_card(struct qeth_card *card, int halt)
{
	int rc = 0;

	QETH_DBF_TEXT(trace,3,"clhacrd");
	QETH_DBF_HEX(trace, 3, &card, sizeof(void *));

	if (halt)
		rc = qeth_halt_channels(card);
	if (rc)
		return rc;
	return qeth_clear_channels(card);
}

static int
qeth_qdio_clear_card(struct qeth_card *card, int use_halt)
{
	int rc = 0;

	QETH_DBF_TEXT(trace,3,"qdioclr");
	if (card->qdio.state == QETH_QDIO_ESTABLISHED){
		if ((rc = qdio_cleanup(CARD_DDEV(card),
			     (card->info.type == QETH_CARD_TYPE_IQD) ?
			     QDIO_FLAG_CLEANUP_USING_HALT :
			     QDIO_FLAG_CLEANUP_USING_CLEAR)))
			QETH_DBF_TEXT_(trace, 3, "1err%d", rc);
		card->qdio.state = QETH_QDIO_ALLOCATED;
	}
	if ((rc = qeth_clear_halt_card(card, use_halt)))
		QETH_DBF_TEXT_(trace, 3, "2err%d", rc);
	card->state = CARD_STATE_DOWN;
	return rc;
}

static int
qeth_dm_act(struct qeth_card *card)
{
	int rc;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(setup,2,"dmact");

	iob = qeth_wait_for_buffer(&card->write);
	memcpy(iob->data, DM_ACT, DM_ACT_SIZE);

	memcpy(QETH_DM_ACT_DEST_ADDR(iob->data),
	       &card->token.cm_connection_r, QETH_MPC_TOKEN_LENGTH);
	memcpy(QETH_DM_ACT_CONNECTION_TOKEN(iob->data),
	       &card->token.ulp_connection_r, QETH_MPC_TOKEN_LENGTH);
	rc = qeth_send_control_data(card, DM_ACT_SIZE, iob, NULL, NULL);
	return rc;
}

static int
qeth_mpc_initialize(struct qeth_card *card)
{
	int rc;

	QETH_DBF_TEXT(setup,2,"mpcinit");

	if ((rc = qeth_issue_next_read(card))){
		QETH_DBF_TEXT_(setup, 2, "1err%d", rc);
		return rc;
	}
	if ((rc = qeth_cm_enable(card))){
		QETH_DBF_TEXT_(setup, 2, "2err%d", rc);
		goto out_qdio;
	}
	if ((rc = qeth_cm_setup(card))){
		QETH_DBF_TEXT_(setup, 2, "3err%d", rc);
		goto out_qdio;
	}
	if ((rc = qeth_ulp_enable(card))){
		QETH_DBF_TEXT_(setup, 2, "4err%d", rc);
		goto out_qdio;
	}
	if ((rc = qeth_ulp_setup(card))){
		QETH_DBF_TEXT_(setup, 2, "5err%d", rc);
		goto out_qdio;
	}
	if ((rc = qeth_alloc_qdio_buffers(card))){
		QETH_DBF_TEXT_(setup, 2, "5err%d", rc);
		goto out_qdio;
	}
	if ((rc = qeth_qdio_establish(card))){
		QETH_DBF_TEXT_(setup, 2, "6err%d", rc);
		qeth_free_qdio_buffers(card);
		goto out_qdio;
	}
 	if ((rc = qeth_qdio_activate(card))){
		QETH_DBF_TEXT_(setup, 2, "7err%d", rc);
		goto out_qdio;
	}
	if ((rc = qeth_dm_act(card))){
		QETH_DBF_TEXT_(setup, 2, "8err%d", rc);
		goto out_qdio;
	}

	return 0;
out_qdio:
	qeth_qdio_clear_card(card, card->info.type!=QETH_CARD_TYPE_IQD);
	return rc;
}

static struct net_device *
qeth_get_netdevice(enum qeth_card_types type, enum qeth_link_types linktype)
{
	struct net_device *dev = NULL;

	switch (type) {
	case QETH_CARD_TYPE_OSAE:
		switch (linktype) {
		case QETH_LINK_TYPE_LANE_TR:
		case QETH_LINK_TYPE_HSTR:
#ifdef CONFIG_TR
			dev = alloc_trdev(0);
#endif /* CONFIG_TR */
			break;
		default:
			dev = alloc_etherdev(0);
		}
		break;
	case QETH_CARD_TYPE_IQD:
		dev = alloc_netdev(0, "hsi%d", ether_setup);
		break;
	case QETH_CARD_TYPE_OSN:
		dev = alloc_netdev(0, "osn%d", ether_setup);
		break;
	default:
		dev = alloc_etherdev(0);
	}
	return dev;
}

/*hard_header fake function; used in case fake_ll is set */
static int
qeth_fake_header(struct sk_buff *skb, struct net_device *dev,
		     unsigned short type, void *daddr, void *saddr,
		     unsigned len)
{
	if(dev->type == ARPHRD_IEEE802_TR){
		struct trh_hdr *hdr;
        	hdr = (struct trh_hdr *)skb_push(skb, QETH_FAKE_LL_LEN_TR);
		memcpy(hdr->saddr, dev->dev_addr, TR_ALEN);
        	memcpy(hdr->daddr, "FAKELL", TR_ALEN);
		return QETH_FAKE_LL_LEN_TR;

	} else {
		struct ethhdr *hdr;
        	hdr = (struct ethhdr *)skb_push(skb, QETH_FAKE_LL_LEN_ETH);
		memcpy(hdr->h_source, dev->dev_addr, ETH_ALEN);
        	memcpy(hdr->h_dest, "FAKELL", ETH_ALEN);
        	if (type != ETH_P_802_3)
                	hdr->h_proto = htons(type);
        	else
                	hdr->h_proto = htons(len);
		return QETH_FAKE_LL_LEN_ETH;

	}
}

static inline int
qeth_send_packet(struct qeth_card *, struct sk_buff *);

static int
qeth_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	int rc;
	struct qeth_card *card;

	QETH_DBF_TEXT(trace, 6, "hrdstxmi");
	card = (struct qeth_card *)dev->priv;
	if (skb==NULL) {
		card->stats.tx_dropped++;
		card->stats.tx_errors++;
		/* return OK; otherwise ksoftirqd goes to 100% */
		return NETDEV_TX_OK;
	}
	if ((card->state != CARD_STATE_UP) || !card->lan_online) {
		card->stats.tx_dropped++;
		card->stats.tx_errors++;
		card->stats.tx_carrier_errors++;
		dev_kfree_skb_any(skb);
		/* return OK; otherwise ksoftirqd goes to 100% */
		return NETDEV_TX_OK;
	}
#ifdef CONFIG_QETH_PERF_STATS
	card->perf_stats.outbound_cnt++;
	card->perf_stats.outbound_start_time = qeth_get_micros();
#endif
	netif_stop_queue(dev);
	if ((rc = qeth_send_packet(card, skb))) {
		if (rc == -EBUSY) {
			return NETDEV_TX_BUSY;
		} else {
			card->stats.tx_errors++;
			card->stats.tx_dropped++;
			dev_kfree_skb_any(skb);
			/*set to OK; otherwise ksoftirqd goes to 100% */
			rc = NETDEV_TX_OK;
		}
	}
	netif_wake_queue(dev);
#ifdef CONFIG_QETH_PERF_STATS
	card->perf_stats.outbound_time += qeth_get_micros() -
		card->perf_stats.outbound_start_time;
#endif
	return rc;
}

static int
qeth_verify_vlan_dev(struct net_device *dev, struct qeth_card *card)
{
	int rc = 0;
#ifdef CONFIG_QETH_VLAN
	struct vlan_group *vg;
	int i;

	if (!(vg = card->vlangrp))
		return rc;

	for (i = 0; i < VLAN_GROUP_ARRAY_LEN; i++){
		if (vg->vlan_devices[i] == dev){
			rc = QETH_VLAN_CARD;
			break;
		}
	}
	if (rc && !(VLAN_DEV_INFO(dev)->real_dev->priv == (void *)card))
		return 0;

#endif
	return rc;
}

static int
qeth_verify_dev(struct net_device *dev)
{
	struct qeth_card *card;
	unsigned long flags;
	int rc = 0;

	read_lock_irqsave(&qeth_card_list.rwlock, flags);
	list_for_each_entry(card, &qeth_card_list.list, list){
		if (card->dev == dev){
			rc = QETH_REAL_CARD;
			break;
		}
		rc = qeth_verify_vlan_dev(dev, card);
		if (rc)
			break;
	}
	read_unlock_irqrestore(&qeth_card_list.rwlock, flags);

	return rc;
}

static struct qeth_card *
qeth_get_card_from_dev(struct net_device *dev)
{
	struct qeth_card *card = NULL;
	int rc;

	rc = qeth_verify_dev(dev);
	if (rc == QETH_REAL_CARD)
		card = (struct qeth_card *)dev->priv;
	else if (rc == QETH_VLAN_CARD)
		card = (struct qeth_card *)
			VLAN_DEV_INFO(dev)->real_dev->priv;

	QETH_DBF_TEXT_(trace, 4, "%d", rc);
	return card ;
}

static void
qeth_tx_timeout(struct net_device *dev)
{
	struct qeth_card *card;

	card = (struct qeth_card *) dev->priv;
	card->stats.tx_errors++;
	qeth_schedule_recovery(card);
}

static int
qeth_open(struct net_device *dev)
{
	struct qeth_card *card;

	QETH_DBF_TEXT(trace, 4, "qethopen");

	card = (struct qeth_card *) dev->priv;

	if (card->state != CARD_STATE_SOFTSETUP)
		return -ENODEV;

	if ( (card->info.type != QETH_CARD_TYPE_OSN) &&
	     (card->options.layer2) &&
	     (!(card->info.mac_bits & QETH_LAYER2_MAC_REGISTERED))) {
		QETH_DBF_TEXT(trace,4,"nomacadr");
		return -EPERM;
	}
	card->dev->flags |= IFF_UP;
	netif_start_queue(dev);
	card->data.state = CH_STATE_UP;
	card->state = CARD_STATE_UP;

	if (!card->lan_online && netif_carrier_ok(dev))
		netif_carrier_off(dev);
	return 0;
}

static int
qeth_stop(struct net_device *dev)
{
	struct qeth_card *card;

	QETH_DBF_TEXT(trace, 4, "qethstop");

	card = (struct qeth_card *) dev->priv;

	netif_stop_queue(dev);
	card->dev->flags &= ~IFF_UP;
	if (card->state == CARD_STATE_UP)
		card->state = CARD_STATE_SOFTSETUP;
	return 0;
}

static inline int
qeth_get_cast_type(struct qeth_card *card, struct sk_buff *skb)
{
	int cast_type = RTN_UNSPEC;

	if (card->info.type == QETH_CARD_TYPE_OSN)
		return cast_type;

	if (skb->dst && skb->dst->neighbour){
		cast_type = skb->dst->neighbour->type;
		if ((cast_type == RTN_BROADCAST) ||
		    (cast_type == RTN_MULTICAST) ||
		    (cast_type == RTN_ANYCAST))
			return cast_type;
		else
			return RTN_UNSPEC;
	}
	/* try something else */
	if (skb->protocol == ETH_P_IPV6)
		return (skb->nh.raw[24] == 0xff) ? RTN_MULTICAST : 0;
	else if (skb->protocol == ETH_P_IP)
		return ((skb->nh.raw[16] & 0xf0) == 0xe0) ? RTN_MULTICAST : 0;
	/* ... */
	if (!memcmp(skb->data, skb->dev->broadcast, 6))
		return RTN_BROADCAST;
	else {
		u16 hdr_mac;

	        hdr_mac = *((u16 *)skb->data);
	        /* tr multicast? */
	        switch (card->info.link_type) {
	        case QETH_LINK_TYPE_HSTR:
	        case QETH_LINK_TYPE_LANE_TR:
	        	if ((hdr_mac == QETH_TR_MAC_NC) ||
			    (hdr_mac == QETH_TR_MAC_C))
				return RTN_MULTICAST;
	        /* eth or so multicast? */
                default:
                      	if ((hdr_mac == QETH_ETH_MAC_V4) ||
			    (hdr_mac == QETH_ETH_MAC_V6))
			        return RTN_MULTICAST;
	        }
        }
	return cast_type;
}

static inline int
qeth_get_priority_queue(struct qeth_card *card, struct sk_buff *skb,
		        int ipv, int cast_type)
{
	if (!ipv && (card->info.type == QETH_CARD_TYPE_OSAE))
		return card->qdio.default_out_queue;
	switch (card->qdio.no_out_queues) {
	case 4:
		if (cast_type && card->info.is_multicast_different)
			return card->info.is_multicast_different &
				(card->qdio.no_out_queues - 1);
		if (card->qdio.do_prio_queueing && (ipv == 4)) {
			if (card->qdio.do_prio_queueing==QETH_PRIO_Q_ING_TOS){
				if (skb->nh.iph->tos & IP_TOS_NOTIMPORTANT)
					return 3;
				if (skb->nh.iph->tos & IP_TOS_HIGHRELIABILITY)
					return 2;
				if (skb->nh.iph->tos & IP_TOS_HIGHTHROUGHPUT)
					return 1;
				if (skb->nh.iph->tos & IP_TOS_LOWDELAY)
					return 0;
			}
			if (card->qdio.do_prio_queueing==QETH_PRIO_Q_ING_PREC)
				return 3 - (skb->nh.iph->tos >> 6);
		} else if (card->qdio.do_prio_queueing && (ipv == 6)) {
			/* TODO: IPv6!!! */
		}
		return card->qdio.default_out_queue;
	case 1: /* fallthrough for single-out-queue 1920-device */
	default:
		return card->qdio.default_out_queue;
	}
}

static inline int
qeth_get_ip_version(struct sk_buff *skb)
{
	switch (skb->protocol) {
	case ETH_P_IPV6:
		return 6;
	case ETH_P_IP:
		return 4;
	default:
		return 0;
	}
}

static inline int
qeth_prepare_skb(struct qeth_card *card, struct sk_buff **skb,
		 struct qeth_hdr **hdr, int ipv)
{
	int rc = 0;
#ifdef CONFIG_QETH_VLAN
	u16 *tag;
#endif

	QETH_DBF_TEXT(trace, 6, "prepskb");
	if (card->info.type == QETH_CARD_TYPE_OSN) {
		*hdr = (struct qeth_hdr *)(*skb)->data;
		return rc;
	}
        rc = qeth_realloc_headroom(card, skb, sizeof(struct qeth_hdr));
        if (rc)
                return rc;
#ifdef CONFIG_QETH_VLAN
	if (card->vlangrp && vlan_tx_tag_present(*skb) &&
	    ((ipv == 6) || card->options.layer2) ) {
		/*
		 * Move the mac addresses (6 bytes src, 6 bytes dest)
		 * to the beginning of the new header.  We are using three
		 * memcpys instead of one memmove to save cycles.
		 */
		skb_push(*skb, VLAN_HLEN);
		memcpy((*skb)->data, (*skb)->data + 4, 4);
		memcpy((*skb)->data + 4, (*skb)->data + 8, 4);
		memcpy((*skb)->data + 8, (*skb)->data + 12, 4);
		tag = (u16 *)((*skb)->data + 12);
		/*
		 * first two bytes  = ETH_P_8021Q (0x8100)
		 * second two bytes = VLANID
		 */
		*tag = __constant_htons(ETH_P_8021Q);
		*(tag + 1) = htons(vlan_tx_tag_get(*skb));
	}
#endif
	*hdr = (struct qeth_hdr *)
		qeth_push_skb(card, skb, sizeof(struct qeth_hdr));
	if (hdr == NULL)
		return -EINVAL;
	return 0;
}

static inline u8
qeth_get_qeth_hdr_flags4(int cast_type)
{
	if (cast_type == RTN_MULTICAST)
		return QETH_CAST_MULTICAST;
	if (cast_type == RTN_BROADCAST)
		return QETH_CAST_BROADCAST;
	return QETH_CAST_UNICAST;
}

static inline u8
qeth_get_qeth_hdr_flags6(int cast_type)
{
	u8 ct = QETH_HDR_PASSTHRU | QETH_HDR_IPV6;
	if (cast_type == RTN_MULTICAST)
		return ct | QETH_CAST_MULTICAST;
	if (cast_type == RTN_ANYCAST)
		return ct | QETH_CAST_ANYCAST;
	if (cast_type == RTN_BROADCAST)
		return ct | QETH_CAST_BROADCAST;
	return ct | QETH_CAST_UNICAST;
}

static inline void
qeth_layer2_get_packet_type(struct qeth_card *card, struct qeth_hdr *hdr,
			    struct sk_buff *skb)
{
	__u16 hdr_mac;

	if (!memcmp(skb->data+QETH_HEADER_SIZE,
		    skb->dev->broadcast,6)) { /* broadcast? */
		*(__u32 *)hdr->hdr.l2.flags |=
			 QETH_LAYER2_FLAG_BROADCAST << 8;
		return;
	}
	hdr_mac=*((__u16*)skb->data);
	/* tr multicast? */
	switch (card->info.link_type) {
	case QETH_LINK_TYPE_HSTR:
	case QETH_LINK_TYPE_LANE_TR:
		if ((hdr_mac == QETH_TR_MAC_NC) ||
		    (hdr_mac == QETH_TR_MAC_C) )
			*(__u32 *)hdr->hdr.l2.flags |=
				QETH_LAYER2_FLAG_MULTICAST << 8;
		else
			*(__u32 *)hdr->hdr.l2.flags |=
				QETH_LAYER2_FLAG_UNICAST << 8;
		break;
		/* eth or so multicast? */
	default:
		if ( (hdr_mac==QETH_ETH_MAC_V4) ||
		     (hdr_mac==QETH_ETH_MAC_V6) )
			*(__u32 *)hdr->hdr.l2.flags |=
				QETH_LAYER2_FLAG_MULTICAST << 8;
		else
			*(__u32 *)hdr->hdr.l2.flags |=
				QETH_LAYER2_FLAG_UNICAST << 8;
	}
}

static inline void
qeth_layer2_fill_header(struct qeth_card *card, struct qeth_hdr *hdr,
			struct sk_buff *skb, int cast_type)
{
	memset(hdr, 0, sizeof(struct qeth_hdr));
	hdr->hdr.l2.id = QETH_HEADER_TYPE_LAYER2;

	/* set byte 0 to "0x02" and byte 3 to casting flags */
	if (cast_type==RTN_MULTICAST)
		*(__u32 *)hdr->hdr.l2.flags |= QETH_LAYER2_FLAG_MULTICAST << 8;
	else if (cast_type==RTN_BROADCAST)
		*(__u32 *)hdr->hdr.l2.flags |= QETH_LAYER2_FLAG_BROADCAST << 8;
	 else
		qeth_layer2_get_packet_type(card, hdr, skb);

	hdr->hdr.l2.pkt_length = skb->len-QETH_HEADER_SIZE;
#ifdef CONFIG_QETH_VLAN
	/* VSWITCH relies on the VLAN
	 * information to be present in
	 * the QDIO header */
	if ((card->vlangrp != NULL) &&
	    vlan_tx_tag_present(skb)) {
		*(__u32 *)hdr->hdr.l2.flags |= QETH_LAYER2_FLAG_VLAN << 8;
		hdr->hdr.l2.vlan_id = vlan_tx_tag_get(skb);
	}
#endif
}

void
qeth_fill_header(struct qeth_card *card, struct qeth_hdr *hdr,
		struct sk_buff *skb, int ipv, int cast_type)
{
	QETH_DBF_TEXT(trace, 6, "fillhdr");

	memset(hdr, 0, sizeof(struct qeth_hdr));
	if (card->options.layer2) {
		qeth_layer2_fill_header(card, hdr, skb, cast_type);
		return;
	}
	hdr->hdr.l3.id = QETH_HEADER_TYPE_LAYER3;
	hdr->hdr.l3.ext_flags = 0;
#ifdef CONFIG_QETH_VLAN
	/*
	 * before we're going to overwrite this location with next hop ip.
	 * v6 uses passthrough, v4 sets the tag in the QDIO header.
	 */
	if (card->vlangrp && vlan_tx_tag_present(skb)) {
		hdr->hdr.l3.ext_flags = (ipv == 4) ?
			QETH_HDR_EXT_VLAN_FRAME :
			QETH_HDR_EXT_INCLUDE_VLAN_TAG;
		hdr->hdr.l3.vlan_id = vlan_tx_tag_get(skb);
	}
#endif /* CONFIG_QETH_VLAN */
	hdr->hdr.l3.length = skb->len - sizeof(struct qeth_hdr);
	if (ipv == 4) {	 /* IPv4 */
		hdr->hdr.l3.flags = qeth_get_qeth_hdr_flags4(cast_type);
		memset(hdr->hdr.l3.dest_addr, 0, 12);
		if ((skb->dst) && (skb->dst->neighbour)) {
			*((u32 *) (&hdr->hdr.l3.dest_addr[12])) =
			    *((u32 *) skb->dst->neighbour->primary_key);
		} else {
			/* fill in destination address used in ip header */
			*((u32 *) (&hdr->hdr.l3.dest_addr[12])) = skb->nh.iph->daddr;
		}
	} else if (ipv == 6) { /* IPv6 or passthru */
		hdr->hdr.l3.flags = qeth_get_qeth_hdr_flags6(cast_type);
		if ((skb->dst) && (skb->dst->neighbour)) {
			memcpy(hdr->hdr.l3.dest_addr,
			       skb->dst->neighbour->primary_key, 16);
		} else {
			/* fill in destination address used in ip header */
			memcpy(hdr->hdr.l3.dest_addr, &skb->nh.ipv6h->daddr, 16);
		}
	} else { /* passthrough */
                if((skb->dev->type == ARPHRD_IEEE802_TR) &&
		    !memcmp(skb->data + sizeof(struct qeth_hdr) + 
		    sizeof(__u16), skb->dev->broadcast, 6)) {
			hdr->hdr.l3.flags = QETH_CAST_BROADCAST |
						QETH_HDR_PASSTHRU;
		} else if (!memcmp(skb->data + sizeof(struct qeth_hdr),
			    skb->dev->broadcast, 6)) {   /* broadcast? */
			hdr->hdr.l3.flags = QETH_CAST_BROADCAST |
						QETH_HDR_PASSTHRU;
		} else {
 			hdr->hdr.l3.flags = (cast_type == RTN_MULTICAST) ?
 				QETH_CAST_MULTICAST | QETH_HDR_PASSTHRU :
 				QETH_CAST_UNICAST | QETH_HDR_PASSTHRU;
		}
	}
}

static inline void
__qeth_fill_buffer(struct sk_buff *skb, struct qdio_buffer *buffer,
		   int is_tso, int *next_element_to_fill)
{
	int length = skb->len;
	int length_here;
	int element;
	char *data;
	int first_lap ;

	element = *next_element_to_fill;
	data = skb->data;
	first_lap = (is_tso == 0 ? 1 : 0);

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

static inline int
qeth_fill_buffer(struct qeth_qdio_out_q *queue,
		 struct qeth_qdio_out_buffer *buf,
		 struct sk_buff *skb)
{
	struct qdio_buffer *buffer;
	struct qeth_hdr_tso *hdr;
	int flush_cnt = 0, hdr_len, large_send = 0;

	QETH_DBF_TEXT(trace, 6, "qdfillbf");

	buffer = buf->buffer;
	atomic_inc(&skb->users);
	skb_queue_tail(&buf->skb_list, skb);

	hdr  = (struct qeth_hdr_tso *) skb->data;
	/*check first on TSO ....*/
	if (hdr->hdr.hdr.l3.id == QETH_HEADER_TYPE_TSO) {
		int element = buf->next_element_to_fill;

		hdr_len = sizeof(struct qeth_hdr_tso) + hdr->ext.dg_hdr_len;
		/*fill first buffer entry only with header information */
		buffer->element[element].addr = skb->data;
		buffer->element[element].length = hdr_len;
		buffer->element[element].flags = SBAL_FLAGS_FIRST_FRAG;
		buf->next_element_to_fill++;
		skb->data += hdr_len;
		skb->len  -= hdr_len;
		large_send = 1;
	}
	if (skb_shinfo(skb)->nr_frags == 0)
		__qeth_fill_buffer(skb, buffer, large_send,
				   (int *)&buf->next_element_to_fill);
	else
		__qeth_fill_buffer_frag(skb, buffer, large_send,
					(int *)&buf->next_element_to_fill);

	if (!queue->do_pack) {
		QETH_DBF_TEXT(trace, 6, "fillbfnp");
		/* set state to PRIMED -> will be flushed */
		atomic_set(&buf->state, QETH_QDIO_BUF_PRIMED);
		flush_cnt = 1;
	} else {
		QETH_DBF_TEXT(trace, 6, "fillbfpa");
#ifdef CONFIG_QETH_PERF_STATS
		queue->card->perf_stats.skbs_sent_pack++;
#endif
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

static inline int
qeth_do_send_packet_fast(struct qeth_card *card, struct qeth_qdio_out_q *queue,
			 struct sk_buff *skb, struct qeth_hdr *hdr,
			 int elements_needed,
			 struct qeth_eddp_context *ctx)
{
	struct qeth_qdio_out_buffer *buffer;
	int buffers_needed = 0;
	int flush_cnt = 0;
	int index;

	QETH_DBF_TEXT(trace, 6, "dosndpfa");

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
	if (atomic_read(&buffer->state) != QETH_QDIO_BUF_EMPTY) {
		card->stats.tx_dropped++;
		atomic_set(&queue->state, QETH_OUT_Q_UNLOCKED);
		return -EBUSY;
	}
	if (ctx == NULL)
		queue->next_buf_to_fill = (queue->next_buf_to_fill + 1) %
					  QDIO_MAX_BUFFERS_PER_Q;
	else {
		buffers_needed = qeth_eddp_check_buffers_for_context(queue,ctx);
		if (buffers_needed < 0) {
			card->stats.tx_dropped++;
			atomic_set(&queue->state, QETH_OUT_Q_UNLOCKED);
			return -EBUSY;
		}
		queue->next_buf_to_fill =
			(queue->next_buf_to_fill + buffers_needed) %
			QDIO_MAX_BUFFERS_PER_Q;
	}
	atomic_set(&queue->state, QETH_OUT_Q_UNLOCKED);
	if (ctx == NULL) {
		qeth_fill_buffer(queue, buffer, skb);
		qeth_flush_buffers(queue, 0, index, 1);
	} else {
		flush_cnt = qeth_eddp_fill_buffer(queue, ctx, index);
		WARN_ON(buffers_needed != flush_cnt);
		qeth_flush_buffers(queue, 0, index, flush_cnt);
	}
	return 0;
}

static inline int
qeth_do_send_packet(struct qeth_card *card, struct qeth_qdio_out_q *queue,
		    struct sk_buff *skb, struct qeth_hdr *hdr,
		    int elements_needed, struct qeth_eddp_context *ctx)
{
	struct qeth_qdio_out_buffer *buffer;
	int start_index;
	int flush_count = 0;
	int do_pack = 0;
	int tmp;
	int rc = 0;

	QETH_DBF_TEXT(trace, 6, "dosndpkt");

	/* spin until we get the queue ... */
	while (atomic_cmpxchg(&queue->state, QETH_OUT_Q_UNLOCKED,
			      QETH_OUT_Q_LOCKED) != QETH_OUT_Q_UNLOCKED);
	start_index = queue->next_buf_to_fill;
	buffer = &queue->bufs[queue->next_buf_to_fill];
	/*
	 * check if buffer is empty to make sure that we do not 'overtake'
	 * ourselves and try to fill a buffer that is already primed
	 */
	if (atomic_read(&buffer->state) != QETH_QDIO_BUF_EMPTY){
		card->stats.tx_dropped++;
		atomic_set(&queue->state, QETH_OUT_Q_UNLOCKED);
		return -EBUSY;
	}
	/* check if we need to switch packing state of this queue */
	qeth_switch_to_packing_if_needed(queue);
	if (queue->do_pack){
		do_pack = 1;
		if (ctx == NULL) {
			/* does packet fit in current buffer? */
			if((QETH_MAX_BUFFER_ELEMENTS(card) -
			    buffer->next_element_to_fill) < elements_needed){
				/* ... no -> set state PRIMED */
				atomic_set(&buffer->state,QETH_QDIO_BUF_PRIMED);
				flush_count++;
				queue->next_buf_to_fill =
					(queue->next_buf_to_fill + 1) %
					QDIO_MAX_BUFFERS_PER_Q;
				buffer = &queue->bufs[queue->next_buf_to_fill];
				/* we did a step forward, so check buffer state
				 * again */
				if (atomic_read(&buffer->state) !=
						QETH_QDIO_BUF_EMPTY){
					card->stats.tx_dropped++;
					qeth_flush_buffers(queue, 0, start_index, flush_count);
					atomic_set(&queue->state, QETH_OUT_Q_UNLOCKED);
					return -EBUSY;
				}
			}
		} else {
			/* check if we have enough elements (including following
			 * free buffers) to handle eddp context */
			if (qeth_eddp_check_buffers_for_context(queue,ctx) < 0){
				printk("eddp tx_dropped 1\n");
				card->stats.tx_dropped++;
				rc = -EBUSY;
				goto out;
			}
		}
	}
	if (ctx == NULL)
		tmp = qeth_fill_buffer(queue, buffer, skb);
	else {
		tmp = qeth_eddp_fill_buffer(queue,ctx,queue->next_buf_to_fill);
		if (tmp < 0) {
			printk("eddp tx_dropped 2\n");
			card->stats.tx_dropped++;
			rc = - EBUSY;
			goto out;
		}
	}
	queue->next_buf_to_fill = (queue->next_buf_to_fill + tmp) %
				  QDIO_MAX_BUFFERS_PER_Q;
	flush_count += tmp;
out:
	if (flush_count)
		qeth_flush_buffers(queue, 0, start_index, flush_count);
	else if (!atomic_read(&queue->set_pci_flags_count))
		atomic_swap(&queue->state, QETH_OUT_Q_LOCKED_FLUSH);
	/*
	 * queue->state will go from LOCKED -> UNLOCKED or from
	 * LOCKED_FLUSH -> LOCKED if output_handler wanted to 'notify' us
	 * (switch packing state or flush buffer to get another pci flag out).
	 * In that case we will enter this loop
	 */
	while (atomic_dec_return(&queue->state)){
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
			qeth_flush_buffers(queue, 0, start_index, flush_count);
	}
	/* at this point the queue is UNLOCKED again */
#ifdef CONFIG_QETH_PERF_STATS
	if (do_pack)
		queue->card->perf_stats.bufs_sent_pack += flush_count;
#endif /* CONFIG_QETH_PERF_STATS */

	return rc;
}

static inline int
qeth_get_elements_no(struct qeth_card *card, void *hdr, 
		     struct sk_buff *skb, int elems)
{
	int elements_needed = 0;

        if (skb_shinfo(skb)->nr_frags > 0) {
                elements_needed = (skb_shinfo(skb)->nr_frags + 1);
	}
        if (elements_needed == 0 )
                elements_needed = 1 + (((((unsigned long) hdr) % PAGE_SIZE)
                                        + skb->len) >> PAGE_SHIFT);
	if ((elements_needed + elems) > QETH_MAX_BUFFER_ELEMENTS(card)){
                PRINT_ERR("qeth_do_send_packet: invalid size of "
                          "IP packet (Number=%d / Length=%d). Discarded.\n",
                          (elements_needed+elems), skb->len);
                return 0;
        }
        return elements_needed;
}

static inline int
qeth_send_packet(struct qeth_card *card, struct sk_buff *skb)
{
	int ipv = 0;
	int cast_type;
	struct qeth_qdio_out_q *queue;
	struct qeth_hdr *hdr = NULL;
	int elements_needed = 0;
	enum qeth_large_send_types large_send = QETH_LARGE_SEND_NO;
	struct qeth_eddp_context *ctx = NULL;
	int rc;

	QETH_DBF_TEXT(trace, 6, "sendpkt");

	if (!card->options.layer2) {
		ipv = qeth_get_ip_version(skb);
		if ((card->dev->hard_header == qeth_fake_header) && ipv) {
               		if ((skb = qeth_pskb_unshare(skb,GFP_ATOMIC)) == NULL) {
                        	card->stats.tx_dropped++;
                        	dev_kfree_skb_irq(skb);
                        	return 0;
                	}
			if(card->dev->type == ARPHRD_IEEE802_TR){
				skb_pull(skb, QETH_FAKE_LL_LEN_TR);
			} else {
                		skb_pull(skb, QETH_FAKE_LL_LEN_ETH);
			}
		}
	}
	if ((card->info.type == QETH_CARD_TYPE_OSN) &&
		(skb->protocol == htons(ETH_P_IPV6))) {
		dev_kfree_skb_any(skb);
		return 0;
	}
	cast_type = qeth_get_cast_type(card, skb);
	if ((cast_type == RTN_BROADCAST) && 
	    (card->info.broadcast_capable == 0)){
		card->stats.tx_dropped++;
		card->stats.tx_errors++;
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}
	queue = card->qdio.out_qs
		[qeth_get_priority_queue(card, skb, ipv, cast_type)];

	if (skb_shinfo(skb)->tso_size)
		large_send = card->options.large_send;

	/*are we able to do TSO ? If so ,prepare and send it from here */
	if ((large_send == QETH_LARGE_SEND_TSO) &&
	    (cast_type == RTN_UNSPEC)) {
		rc = qeth_tso_prepare_packet(card, skb, ipv, cast_type);
		if (rc) {
			card->stats.tx_dropped++;
			card->stats.tx_errors++;
			dev_kfree_skb_any(skb);
			return NETDEV_TX_OK;
		} 
		elements_needed++;
	} else {
		if ((rc = qeth_prepare_skb(card, &skb, &hdr, ipv))) {
			QETH_DBF_TEXT_(trace, 4, "pskbe%d", rc);
			return rc;
		}
		if (card->info.type != QETH_CARD_TYPE_OSN)
			qeth_fill_header(card, hdr, skb, ipv, cast_type);
	}

	if (large_send == QETH_LARGE_SEND_EDDP) {
		ctx = qeth_eddp_create_context(card, skb, hdr);
		if (ctx == NULL) {
			PRINT_WARN("could not create eddp context\n");
			return -EINVAL;
		}
	} else {
		int elems = qeth_get_elements_no(card,(void*) hdr, skb,
						 elements_needed);
		if (!elems)
			return -EINVAL;
		elements_needed += elems;
	}

	if (card->info.type != QETH_CARD_TYPE_IQD)
		rc = qeth_do_send_packet(card, queue, skb, hdr,
					 elements_needed, ctx);
	else
		rc = qeth_do_send_packet_fast(card, queue, skb, hdr,
					      elements_needed, ctx);
	if (!rc){
		card->stats.tx_packets++;
		card->stats.tx_bytes += skb->len;
#ifdef CONFIG_QETH_PERF_STATS
		if (skb_shinfo(skb)->tso_size &&
		   !(large_send == QETH_LARGE_SEND_NO)) {
			card->perf_stats.large_send_bytes += skb->len;
			card->perf_stats.large_send_cnt++;
		}
 		if (skb_shinfo(skb)->nr_frags > 0){
			card->perf_stats.sg_skbs_sent++;
			/* nr_frags + skb->data */
			card->perf_stats.sg_frags_sent +=
				skb_shinfo(skb)->nr_frags + 1;
		}
#endif /* CONFIG_QETH_PERF_STATS */
	}
	if (ctx != NULL) {
		/* drop creator's reference */
		qeth_eddp_put_context(ctx);
		/* free skb; it's not referenced by a buffer */
		if (rc == 0)
			dev_kfree_skb_any(skb);

	}
	return rc;
}

static int
qeth_mdio_read(struct net_device *dev, int phy_id, int regnum)
{
	struct qeth_card *card = (struct qeth_card *) dev->priv;
	int rc = 0;

	switch(regnum){
	case MII_BMCR: /* Basic mode control register */
		rc = BMCR_FULLDPLX;
		if ((card->info.link_type != QETH_LINK_TYPE_GBIT_ETH)&&
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
		rc = 0;
		break;
	}
	return rc;
}

static void
qeth_mdio_write(struct net_device *dev, int phy_id, int regnum, int value)
{
	switch(regnum){
	case MII_BMCR: /* Basic mode control register */
	case MII_BMSR: /* Basic mode status register */
	case MII_PHYSID1: /* PHYS ID 1 */
	case MII_PHYSID2: /* PHYS ID 2 */
	case MII_ADVERTISE: /* Advertisement control reg */
	case MII_LPA: /* Link partner ability reg */
	case MII_EXPANSION: /* Expansion register */
	case MII_DCOUNTER: /* disconnect counter */
	case MII_FCSCOUNTER: /* false carrier counter */
	case MII_NWAYTEST: /* N-way auto-neg test register */
	case MII_RERRCOUNTER: /* rx error counter */
	case MII_SREVISION: /* silicon revision */
	case MII_RESV1: /* reserved 1 */
	case MII_LBRERROR: /* loopback, rx, bypass error */
	case MII_PHYADDR: /* physical address */
	case MII_RESV2: /* reserved 2 */
	case MII_TPISTATUS: /* TPI status for 10mbps */
	case MII_NCONFIG: /* network interface config */
	default:
		break;
	}
}

static inline const char *
qeth_arp_get_error_cause(int *rc)
{
	switch (*rc) {
	case QETH_IPA_ARP_RC_FAILED:
		*rc = -EIO;
		return "operation failed";
	case QETH_IPA_ARP_RC_NOTSUPP:
		*rc = -EOPNOTSUPP;
		return "operation not supported";
	case QETH_IPA_ARP_RC_OUT_OF_RANGE:
		*rc = -EINVAL;
		return "argument out of range";
	case QETH_IPA_ARP_RC_Q_NOTSUPP:
		*rc = -EOPNOTSUPP;
		return "query operation not supported";
	case QETH_IPA_ARP_RC_Q_NO_DATA:
		*rc = -ENOENT;
		return "no query data available";
	default:
		return "unknown error";
	}
}

static int
qeth_send_simple_setassparms(struct qeth_card *, enum qeth_ipa_funcs,
			     __u16, long);

static int
qeth_arp_set_no_entries(struct qeth_card *card, int no_entries)
{
	int tmp;
	int rc;

	QETH_DBF_TEXT(trace,3,"arpstnoe");

	/*
	 * currently GuestLAN only supports the ARP assist function
	 * IPA_CMD_ASS_ARP_QUERY_INFO, but not IPA_CMD_ASS_ARP_SET_NO_ENTRIES;
	 * thus we say EOPNOTSUPP for this ARP function
	 */
	if (card->info.guestlan)
		return -EOPNOTSUPP;
	if (!qeth_is_supported(card,IPA_ARP_PROCESSING)) {
		PRINT_WARN("ARP processing not supported "
			   "on %s!\n", QETH_CARD_IFNAME(card));
		return -EOPNOTSUPP;
	}
	rc = qeth_send_simple_setassparms(card, IPA_ARP_PROCESSING,
					  IPA_CMD_ASS_ARP_SET_NO_ENTRIES,
					  no_entries);
	if (rc) {
		tmp = rc;
		PRINT_WARN("Could not set number of ARP entries on %s: "
			   "%s (0x%x/%d)\n",
			   QETH_CARD_IFNAME(card), qeth_arp_get_error_cause(&rc),
			   tmp, tmp);
	}
	return rc;
}

static inline void
qeth_copy_arp_entries_stripped(struct qeth_arp_query_info *qinfo,
		               struct qeth_arp_query_data *qdata,
			       int entry_size, int uentry_size)
{
	char *entry_ptr;
	char *uentry_ptr;
	int i;

	entry_ptr = (char *)&qdata->data;
	uentry_ptr = (char *)(qinfo->udata + qinfo->udata_offset);
	for (i = 0; i < qdata->no_entries; ++i){
		/* strip off 32 bytes "media specific information" */
		memcpy(uentry_ptr, (entry_ptr + 32), entry_size - 32);
		entry_ptr += entry_size;
		uentry_ptr += uentry_size;
	}
}

static int
qeth_arp_query_cb(struct qeth_card *card, struct qeth_reply *reply,
		  unsigned long data)
{
	struct qeth_ipa_cmd *cmd;
	struct qeth_arp_query_data *qdata;
	struct qeth_arp_query_info *qinfo;
	int entry_size;
	int uentry_size;
	int i;

	QETH_DBF_TEXT(trace,4,"arpquecb");

	qinfo = (struct qeth_arp_query_info *) reply->param;
	cmd = (struct qeth_ipa_cmd *) data;
	if (cmd->hdr.return_code) {
		QETH_DBF_TEXT_(trace,4,"qaer1%i", cmd->hdr.return_code);
		return 0;
	}
	if (cmd->data.setassparms.hdr.return_code) {
		cmd->hdr.return_code = cmd->data.setassparms.hdr.return_code;
		QETH_DBF_TEXT_(trace,4,"qaer2%i", cmd->hdr.return_code);
		return 0;
	}
	qdata = &cmd->data.setassparms.data.query_arp;
	switch(qdata->reply_bits){
	case 5:
		uentry_size = entry_size = sizeof(struct qeth_arp_qi_entry5);
		if (qinfo->mask_bits & QETH_QARP_STRIP_ENTRIES)
			uentry_size = sizeof(struct qeth_arp_qi_entry5_short);
		break;
	case 7:
		/* fall through to default */
	default:
		/* tr is the same as eth -> entry7 */
		uentry_size = entry_size = sizeof(struct qeth_arp_qi_entry7);
		if (qinfo->mask_bits & QETH_QARP_STRIP_ENTRIES)
			uentry_size = sizeof(struct qeth_arp_qi_entry7_short);
		break;
	}
	/* check if there is enough room in userspace */
	if ((qinfo->udata_len - qinfo->udata_offset) <
			qdata->no_entries * uentry_size){
		QETH_DBF_TEXT_(trace, 4, "qaer3%i", -ENOMEM);
		cmd->hdr.return_code = -ENOMEM;
		PRINT_WARN("query ARP user space buffer is too small for "
			   "the returned number of ARP entries. "
			   "Aborting query!\n");
		goto out_error;
	}
	QETH_DBF_TEXT_(trace, 4, "anore%i",
		       cmd->data.setassparms.hdr.number_of_replies);
	QETH_DBF_TEXT_(trace, 4, "aseqn%i", cmd->data.setassparms.hdr.seq_no);
	QETH_DBF_TEXT_(trace, 4, "anoen%i", qdata->no_entries);

	if (qinfo->mask_bits & QETH_QARP_STRIP_ENTRIES) {
		/* strip off "media specific information" */
		qeth_copy_arp_entries_stripped(qinfo, qdata, entry_size,
					       uentry_size);
	} else
		/*copy entries to user buffer*/
		memcpy(qinfo->udata + qinfo->udata_offset,
		       (char *)&qdata->data, qdata->no_entries*uentry_size);

	qinfo->no_entries += qdata->no_entries;
	qinfo->udata_offset += (qdata->no_entries*uentry_size);
	/* check if all replies received ... */
	if (cmd->data.setassparms.hdr.seq_no <
	    cmd->data.setassparms.hdr.number_of_replies)
		return 1;
	memcpy(qinfo->udata, &qinfo->no_entries, 4);
	/* keep STRIP_ENTRIES flag so the user program can distinguish
	 * stripped entries from normal ones */
	if (qinfo->mask_bits & QETH_QARP_STRIP_ENTRIES)
		qdata->reply_bits |= QETH_QARP_STRIP_ENTRIES;
	memcpy(qinfo->udata + QETH_QARP_MASK_OFFSET,&qdata->reply_bits,2);
	return 0;
out_error:
	i = 0;
	memcpy(qinfo->udata, &i, 4);
	return 0;
}

static int
qeth_send_ipa_arp_cmd(struct qeth_card *card, struct qeth_cmd_buffer *iob,
		      int len, int (*reply_cb)(struct qeth_card *,
					       struct qeth_reply *,
					       unsigned long),
		      void *reply_param)
{
	QETH_DBF_TEXT(trace,4,"sendarp");

	memcpy(iob->data, IPA_PDU_HEADER, IPA_PDU_HEADER_SIZE);
	memcpy(QETH_IPA_CMD_DEST_ADDR(iob->data),
	       &card->token.ulp_connection_r, QETH_MPC_TOKEN_LENGTH);
	return qeth_send_control_data(card, IPA_PDU_HEADER_SIZE + len, iob,
				      reply_cb, reply_param);
}

static int
qeth_send_ipa_snmp_cmd(struct qeth_card *card, struct qeth_cmd_buffer *iob,
		      int len, int (*reply_cb)(struct qeth_card *,
					       struct qeth_reply *,
					       unsigned long),
		      void *reply_param)
{
	u16 s1, s2;

	QETH_DBF_TEXT(trace,4,"sendsnmp");

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

static struct qeth_cmd_buffer *
qeth_get_setassparms_cmd(struct qeth_card *, enum qeth_ipa_funcs,
			 __u16, __u16, enum qeth_prot_versions);
static int
qeth_arp_query(struct qeth_card *card, char *udata)
{
	struct qeth_cmd_buffer *iob;
	struct qeth_arp_query_info qinfo = {0, };
	int tmp;
	int rc;

	QETH_DBF_TEXT(trace,3,"arpquery");

	if (!qeth_is_supported(card,/*IPA_QUERY_ARP_ADDR_INFO*/
			       IPA_ARP_PROCESSING)) {
		PRINT_WARN("ARP processing not supported "
			   "on %s!\n", QETH_CARD_IFNAME(card));
		return -EOPNOTSUPP;
	}
	/* get size of userspace buffer and mask_bits -> 6 bytes */
	if (copy_from_user(&qinfo, udata, 6))
		return -EFAULT;
	if (!(qinfo.udata = kmalloc(qinfo.udata_len, GFP_KERNEL)))
		return -ENOMEM;
	memset(qinfo.udata, 0, qinfo.udata_len);
	qinfo.udata_offset = QETH_QARP_ENTRIES_OFFSET;
	iob = qeth_get_setassparms_cmd(card, IPA_ARP_PROCESSING,
				       IPA_CMD_ASS_ARP_QUERY_INFO,
				       sizeof(int),QETH_PROT_IPV4);

	rc = qeth_send_ipa_arp_cmd(card, iob,
				   QETH_SETASS_BASE_LEN+QETH_ARP_CMD_LEN,
				   qeth_arp_query_cb, (void *)&qinfo);
	if (rc) {
		tmp = rc;
		PRINT_WARN("Error while querying ARP cache on %s: %s "
			   "(0x%x/%d)\n",
			   QETH_CARD_IFNAME(card), qeth_arp_get_error_cause(&rc),
			   tmp, tmp);
		copy_to_user(udata, qinfo.udata, 4);
	} else {
		copy_to_user(udata, qinfo.udata, qinfo.udata_len);
	}
	kfree(qinfo.udata);
	return rc;
}

/**
 * SNMP command callback
 */
static int
qeth_snmp_command_cb(struct qeth_card *card, struct qeth_reply *reply,
		     unsigned long sdata)
{
	struct qeth_ipa_cmd *cmd;
	struct qeth_arp_query_info *qinfo;
	struct qeth_snmp_cmd *snmp;
	unsigned char *data;
	__u16 data_len;

	QETH_DBF_TEXT(trace,3,"snpcmdcb");

	cmd = (struct qeth_ipa_cmd *) sdata;
	data = (unsigned char *)((char *)cmd - reply->offset);
	qinfo = (struct qeth_arp_query_info *) reply->param;
	snmp = &cmd->data.setadapterparms.data.snmp;

	if (cmd->hdr.return_code) {
		QETH_DBF_TEXT_(trace,4,"scer1%i", cmd->hdr.return_code);
		return 0;
	}
	if (cmd->data.setadapterparms.hdr.return_code) {
		cmd->hdr.return_code = cmd->data.setadapterparms.hdr.return_code;
		QETH_DBF_TEXT_(trace,4,"scer2%i", cmd->hdr.return_code);
		return 0;
	}
	data_len = *((__u16*)QETH_IPA_PDU_LEN_PDU1(data));
	if (cmd->data.setadapterparms.hdr.seq_no == 1)
		data_len -= (__u16)((char *)&snmp->data - (char *)cmd);
	else
		data_len -= (__u16)((char*)&snmp->request - (char *)cmd);

	/* check if there is enough room in userspace */
	if ((qinfo->udata_len - qinfo->udata_offset) < data_len) {
		QETH_DBF_TEXT_(trace, 4, "scer3%i", -ENOMEM);
		cmd->hdr.return_code = -ENOMEM;
		return 0;
	}
	QETH_DBF_TEXT_(trace, 4, "snore%i",
		       cmd->data.setadapterparms.hdr.used_total);
	QETH_DBF_TEXT_(trace, 4, "sseqn%i", cmd->data.setadapterparms.hdr.seq_no);
	/*copy entries to user buffer*/
	if (cmd->data.setadapterparms.hdr.seq_no == 1) {
		memcpy(qinfo->udata + qinfo->udata_offset,
		       (char *)snmp,
		       data_len + offsetof(struct qeth_snmp_cmd,data));
		qinfo->udata_offset += offsetof(struct qeth_snmp_cmd, data);
	} else {
		memcpy(qinfo->udata + qinfo->udata_offset,
		       (char *)&snmp->request, data_len);
	}
	qinfo->udata_offset += data_len;
	/* check if all replies received ... */
		QETH_DBF_TEXT_(trace, 4, "srtot%i",
			       cmd->data.setadapterparms.hdr.used_total);
		QETH_DBF_TEXT_(trace, 4, "srseq%i",
			       cmd->data.setadapterparms.hdr.seq_no);
	if (cmd->data.setadapterparms.hdr.seq_no <
	    cmd->data.setadapterparms.hdr.used_total)
		return 1;
	return 0;
}

static struct qeth_cmd_buffer *
qeth_get_ipacmd_buffer(struct qeth_card *, enum qeth_ipa_cmds,
		       enum qeth_prot_versions );

static struct qeth_cmd_buffer *
qeth_get_adapter_cmd(struct qeth_card *card, __u32 command, __u32 cmdlen)
{
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;

	iob = qeth_get_ipacmd_buffer(card,IPA_CMD_SETADAPTERPARMS,
				     QETH_PROT_IPV4);
	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
	cmd->data.setadapterparms.hdr.cmdlength = cmdlen;
	cmd->data.setadapterparms.hdr.command_code = command;
	cmd->data.setadapterparms.hdr.used_total = 1;
	cmd->data.setadapterparms.hdr.seq_no = 1;

	return iob;
}

/**
 * function to send SNMP commands to OSA-E card
 */
static int
qeth_snmp_command(struct qeth_card *card, char *udata)
{
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;
	struct qeth_snmp_ureq *ureq;
	int req_len;
	struct qeth_arp_query_info qinfo = {0, };
	int rc = 0;

	QETH_DBF_TEXT(trace,3,"snmpcmd");

	if (card->info.guestlan)
		return -EOPNOTSUPP;

	if ((!qeth_adp_supported(card,IPA_SETADP_SET_SNMP_CONTROL)) &&
	    (!card->options.layer2) ) {
		PRINT_WARN("SNMP Query MIBS not supported "
			   "on %s!\n", QETH_CARD_IFNAME(card));
		return -EOPNOTSUPP;
	}
	/* skip 4 bytes (data_len struct member) to get req_len */
	if (copy_from_user(&req_len, udata + sizeof(int), sizeof(int)))
		return -EFAULT;
	ureq = kmalloc(req_len+sizeof(struct qeth_snmp_ureq_hdr), GFP_KERNEL);
	if (!ureq) {
		QETH_DBF_TEXT(trace, 2, "snmpnome");
		return -ENOMEM;
	}
	if (copy_from_user(ureq, udata,
			req_len+sizeof(struct qeth_snmp_ureq_hdr))){
		kfree(ureq);
		return -EFAULT;
	}
	qinfo.udata_len = ureq->hdr.data_len;
	if (!(qinfo.udata = kmalloc(qinfo.udata_len, GFP_KERNEL))){
		kfree(ureq);
		return -ENOMEM;
	}
	memset(qinfo.udata, 0, qinfo.udata_len);
	qinfo.udata_offset = sizeof(struct qeth_snmp_ureq_hdr);

	iob = qeth_get_adapter_cmd(card, IPA_SETADP_SET_SNMP_CONTROL,
				   QETH_SNMP_SETADP_CMDLENGTH + req_len);
	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
	memcpy(&cmd->data.setadapterparms.data.snmp, &ureq->cmd, req_len);
	rc = qeth_send_ipa_snmp_cmd(card, iob, QETH_SETADP_BASE_LEN + req_len,
				    qeth_snmp_command_cb, (void *)&qinfo);
	if (rc)
		PRINT_WARN("SNMP command failed on %s: (0x%x)\n",
			   QETH_CARD_IFNAME(card), rc);
	 else
		copy_to_user(udata, qinfo.udata, qinfo.udata_len);

	kfree(ureq);
	kfree(qinfo.udata);
	return rc;
}

static int
qeth_default_setassparms_cb(struct qeth_card *, struct qeth_reply *,
			    unsigned long);

static int
qeth_default_setadapterparms_cb(struct qeth_card *card,
                                struct qeth_reply *reply,
                                unsigned long data);
static int
qeth_send_setassparms(struct qeth_card *, struct qeth_cmd_buffer *,
		      __u16, long,
		      int (*reply_cb)
		      (struct qeth_card *, struct qeth_reply *, unsigned long),
		      void *reply_param);

static int
qeth_arp_add_entry(struct qeth_card *card, struct qeth_arp_cache_entry *entry)
{
	struct qeth_cmd_buffer *iob;
	char buf[16];
	int tmp;
	int rc;

	QETH_DBF_TEXT(trace,3,"arpadent");

	/*
	 * currently GuestLAN only supports the ARP assist function
	 * IPA_CMD_ASS_ARP_QUERY_INFO, but not IPA_CMD_ASS_ARP_ADD_ENTRY;
	 * thus we say EOPNOTSUPP for this ARP function
	 */
	if (card->info.guestlan)
		return -EOPNOTSUPP;
	if (!qeth_is_supported(card,IPA_ARP_PROCESSING)) {
		PRINT_WARN("ARP processing not supported "
			   "on %s!\n", QETH_CARD_IFNAME(card));
		return -EOPNOTSUPP;
	}

	iob = qeth_get_setassparms_cmd(card, IPA_ARP_PROCESSING,
				       IPA_CMD_ASS_ARP_ADD_ENTRY,
				       sizeof(struct qeth_arp_cache_entry),
				       QETH_PROT_IPV4);
	rc = qeth_send_setassparms(card, iob,
				   sizeof(struct qeth_arp_cache_entry),
				   (unsigned long) entry,
				   qeth_default_setassparms_cb, NULL);
	if (rc) {
		tmp = rc;
		qeth_ipaddr4_to_string((u8 *)entry->ipaddr, buf);
		PRINT_WARN("Could not add ARP entry for address %s on %s: "
			   "%s (0x%x/%d)\n",
			   buf, QETH_CARD_IFNAME(card),
			   qeth_arp_get_error_cause(&rc), tmp, tmp);
	}
	return rc;
}

static int
qeth_arp_remove_entry(struct qeth_card *card, struct qeth_arp_cache_entry *entry)
{
	struct qeth_cmd_buffer *iob;
	char buf[16] = {0, };
	int tmp;
	int rc;

	QETH_DBF_TEXT(trace,3,"arprment");

	/*
	 * currently GuestLAN only supports the ARP assist function
	 * IPA_CMD_ASS_ARP_QUERY_INFO, but not IPA_CMD_ASS_ARP_REMOVE_ENTRY;
	 * thus we say EOPNOTSUPP for this ARP function
	 */
	if (card->info.guestlan)
		return -EOPNOTSUPP;
	if (!qeth_is_supported(card,IPA_ARP_PROCESSING)) {
		PRINT_WARN("ARP processing not supported "
			   "on %s!\n", QETH_CARD_IFNAME(card));
		return -EOPNOTSUPP;
	}
	memcpy(buf, entry, 12);
	iob = qeth_get_setassparms_cmd(card, IPA_ARP_PROCESSING,
				       IPA_CMD_ASS_ARP_REMOVE_ENTRY,
				       12,
				       QETH_PROT_IPV4);
	rc = qeth_send_setassparms(card, iob,
				   12, (unsigned long)buf,
				   qeth_default_setassparms_cb, NULL);
	if (rc) {
		tmp = rc;
		memset(buf, 0, 16);
		qeth_ipaddr4_to_string((u8 *)entry->ipaddr, buf);
		PRINT_WARN("Could not delete ARP entry for address %s on %s: "
			   "%s (0x%x/%d)\n",
			   buf, QETH_CARD_IFNAME(card),
			   qeth_arp_get_error_cause(&rc), tmp, tmp);
	}
	return rc;
}

static int
qeth_arp_flush_cache(struct qeth_card *card)
{
	int rc;
	int tmp;

	QETH_DBF_TEXT(trace,3,"arpflush");

	/*
	 * currently GuestLAN only supports the ARP assist function
	 * IPA_CMD_ASS_ARP_QUERY_INFO, but not IPA_CMD_ASS_ARP_FLUSH_CACHE;
	 * thus we say EOPNOTSUPP for this ARP function
	*/
	if (card->info.guestlan || (card->info.type == QETH_CARD_TYPE_IQD))
		return -EOPNOTSUPP;
	if (!qeth_is_supported(card,IPA_ARP_PROCESSING)) {
		PRINT_WARN("ARP processing not supported "
			   "on %s!\n", QETH_CARD_IFNAME(card));
		return -EOPNOTSUPP;
	}
	rc = qeth_send_simple_setassparms(card, IPA_ARP_PROCESSING,
					  IPA_CMD_ASS_ARP_FLUSH_CACHE, 0);
	if (rc){
		tmp = rc;
		PRINT_WARN("Could not flush ARP cache on %s: %s (0x%x/%d)\n",
			   QETH_CARD_IFNAME(card), qeth_arp_get_error_cause(&rc),
			   tmp, tmp);
	}
	return rc;
}

static int
qeth_do_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct qeth_card *card = (struct qeth_card *)dev->priv;
	struct qeth_arp_cache_entry arp_entry;
	struct mii_ioctl_data *mii_data;
	int rc = 0;

	if (!card)
		return -ENODEV;

	if ((card->state != CARD_STATE_UP) &&
            (card->state != CARD_STATE_SOFTSETUP))
		return -ENODEV;

	if (card->info.type == QETH_CARD_TYPE_OSN)
		return -EPERM;

	switch (cmd){
	case SIOC_QETH_ARP_SET_NO_ENTRIES:
		if ( !capable(CAP_NET_ADMIN) ||
		     (card->options.layer2) ) {
			rc = -EPERM;
			break;
		}
		rc = qeth_arp_set_no_entries(card, rq->ifr_ifru.ifru_ivalue);
		break;
	case SIOC_QETH_ARP_QUERY_INFO:
		if ( !capable(CAP_NET_ADMIN) ||
		     (card->options.layer2) ) {
			rc = -EPERM;
			break;
		}
		rc = qeth_arp_query(card, rq->ifr_ifru.ifru_data);
		break;
	case SIOC_QETH_ARP_ADD_ENTRY:
		if ( !capable(CAP_NET_ADMIN) ||
		     (card->options.layer2) ) {
			rc = -EPERM;
			break;
		}
		if (copy_from_user(&arp_entry, rq->ifr_ifru.ifru_data,
				   sizeof(struct qeth_arp_cache_entry)))
			rc = -EFAULT;
		else
			rc = qeth_arp_add_entry(card, &arp_entry);
		break;
	case SIOC_QETH_ARP_REMOVE_ENTRY:
		if ( !capable(CAP_NET_ADMIN) ||
		     (card->options.layer2) ) {
			rc = -EPERM;
			break;
		}
		if (copy_from_user(&arp_entry, rq->ifr_ifru.ifru_data,
				   sizeof(struct qeth_arp_cache_entry)))
			rc = -EFAULT;
		else
			rc = qeth_arp_remove_entry(card, &arp_entry);
		break;
	case SIOC_QETH_ARP_FLUSH_CACHE:
		if ( !capable(CAP_NET_ADMIN) ||
		     (card->options.layer2) ) {
			rc = -EPERM;
			break;
		}
		rc = qeth_arp_flush_cache(card);
		break;
	case SIOC_QETH_ADP_SET_SNMP_CONTROL:
		rc = qeth_snmp_command(card, rq->ifr_ifru.ifru_data);
		break;
	case SIOC_QETH_GET_CARD_TYPE:
		if ((card->info.type == QETH_CARD_TYPE_OSAE) &&
		    !card->info.guestlan)
			return 1;
		return 0;
		break;
	case SIOCGMIIPHY:
		mii_data = if_mii(rq);
		mii_data->phy_id = 0;
		break;
	case SIOCGMIIREG:
		mii_data = if_mii(rq);
		if (mii_data->phy_id != 0)
			rc = -EINVAL;
		else
			mii_data->val_out = qeth_mdio_read(dev,mii_data->phy_id,
							   mii_data->reg_num);
		break;
	case SIOCSMIIREG:
		rc = -EOPNOTSUPP;
		break;
		/* TODO: remove return if qeth_mdio_write does something */
		if (!capable(CAP_NET_ADMIN)){
			rc = -EPERM;
			break;
		}
		mii_data = if_mii(rq);
		if (mii_data->phy_id != 0)
			rc = -EINVAL;
		else
			qeth_mdio_write(dev, mii_data->phy_id, mii_data->reg_num,
					mii_data->val_in);
		break;
	default:
		rc = -EOPNOTSUPP;
	}
	if (rc)
		QETH_DBF_TEXT_(trace, 2, "ioce%d", rc);
	return rc;
}

static struct net_device_stats *
qeth_get_stats(struct net_device *dev)
{
	struct qeth_card *card;

	card = (struct qeth_card *) (dev->priv);

	QETH_DBF_TEXT(trace,5,"getstat");

	return &card->stats;
}

static int
qeth_change_mtu(struct net_device *dev, int new_mtu)
{
	struct qeth_card *card;
	char dbf_text[15];

	card = (struct qeth_card *) (dev->priv);

	QETH_DBF_TEXT(trace,4,"chgmtu");
	sprintf(dbf_text, "%8x", new_mtu);
	QETH_DBF_TEXT(trace,4,dbf_text);

	if (new_mtu < 64)
		return -EINVAL;
	if (new_mtu > 65535)
		return -EINVAL;
	if ((!qeth_is_supported(card,IPA_IP_FRAGMENTATION)) &&
	    (!qeth_mtu_is_valid(card, new_mtu)))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

#ifdef CONFIG_QETH_VLAN
static void
qeth_vlan_rx_register(struct net_device *dev, struct vlan_group *grp)
{
	struct qeth_card *card;
	unsigned long flags;

	QETH_DBF_TEXT(trace,4,"vlanreg");

	card = (struct qeth_card *) dev->priv;
	spin_lock_irqsave(&card->vlanlock, flags);
	card->vlangrp = grp;
	spin_unlock_irqrestore(&card->vlanlock, flags);
}

static inline void
qeth_free_vlan_buffer(struct qeth_card *card, struct qeth_qdio_out_buffer *buf,
		      unsigned short vid)
{
	int i;
	struct sk_buff *skb;
	struct sk_buff_head tmp_list;

	skb_queue_head_init(&tmp_list);
	for(i = 0; i < QETH_MAX_BUFFER_ELEMENTS(card); ++i){
		while ((skb = skb_dequeue(&buf->skb_list))){
			if (vlan_tx_tag_present(skb) &&
			    (vlan_tx_tag_get(skb) == vid)) {
				atomic_dec(&skb->users);
				dev_kfree_skb(skb);
			} else
				skb_queue_tail(&tmp_list, skb);
		}
	}
	while ((skb = skb_dequeue(&tmp_list)))
		skb_queue_tail(&buf->skb_list, skb);
}

static void
qeth_free_vlan_skbs(struct qeth_card *card, unsigned short vid)
{
	int i, j;

	QETH_DBF_TEXT(trace, 4, "frvlskbs");
	for (i = 0; i < card->qdio.no_out_queues; ++i){
		for (j = 0; j < QDIO_MAX_BUFFERS_PER_Q; ++j)
			qeth_free_vlan_buffer(card, &card->qdio.
					      out_qs[i]->bufs[j], vid);
	}
}

static void
qeth_free_vlan_addresses4(struct qeth_card *card, unsigned short vid)
{
	struct in_device *in_dev;
	struct in_ifaddr *ifa;
	struct qeth_ipaddr *addr;

	QETH_DBF_TEXT(trace, 4, "frvaddr4");

	rcu_read_lock();
	in_dev = __in_dev_get_rcu(card->vlangrp->vlan_devices[vid]);
	if (!in_dev)
		goto out;
	for (ifa = in_dev->ifa_list; ifa; ifa = ifa->ifa_next) {
		addr = qeth_get_addr_buffer(QETH_PROT_IPV4);
		if (addr){
			addr->u.a4.addr = ifa->ifa_address;
			addr->u.a4.mask = ifa->ifa_mask;
			addr->type = QETH_IP_TYPE_NORMAL;
			if (!qeth_delete_ip(card, addr))
				kfree(addr);
		}
	}
out:
	rcu_read_unlock();
}

static void
qeth_free_vlan_addresses6(struct qeth_card *card, unsigned short vid)
{
#ifdef CONFIG_QETH_IPV6
	struct inet6_dev *in6_dev;
	struct inet6_ifaddr *ifa;
	struct qeth_ipaddr *addr;

	QETH_DBF_TEXT(trace, 4, "frvaddr6");

	in6_dev = in6_dev_get(card->vlangrp->vlan_devices[vid]);
	if (!in6_dev)
		return;
	for (ifa = in6_dev->addr_list; ifa; ifa = ifa->lst_next){
		addr = qeth_get_addr_buffer(QETH_PROT_IPV6);
		if (addr){
			memcpy(&addr->u.a6.addr, &ifa->addr,
			       sizeof(struct in6_addr));
			addr->u.a6.pfxlen = ifa->prefix_len;
			addr->type = QETH_IP_TYPE_NORMAL;
			if (!qeth_delete_ip(card, addr))
				kfree(addr);
		}
	}
	in6_dev_put(in6_dev);
#endif /* CONFIG_QETH_IPV6 */
}

static void
qeth_free_vlan_addresses(struct qeth_card *card, unsigned short vid)
{
	if (card->options.layer2 || !card->vlangrp)
		return;
	qeth_free_vlan_addresses4(card, vid);
	qeth_free_vlan_addresses6(card, vid);
}

static int
qeth_layer2_send_setdelvlan_cb(struct qeth_card *card,
                               struct qeth_reply *reply,
                               unsigned long data)
{
        struct qeth_ipa_cmd *cmd;

        QETH_DBF_TEXT(trace, 2, "L2sdvcb");
        cmd = (struct qeth_ipa_cmd *) data;
        if (cmd->hdr.return_code) {
		PRINT_ERR("Error in processing VLAN %i on %s: 0x%x. "
			  "Continuing\n",cmd->data.setdelvlan.vlan_id, 
			  QETH_CARD_IFNAME(card), cmd->hdr.return_code);
		QETH_DBF_TEXT_(trace, 2, "L2VL%4x", cmd->hdr.command);
		QETH_DBF_TEXT_(trace, 2, "L2%s", CARD_BUS_ID(card));
		QETH_DBF_TEXT_(trace, 2, "err%d", cmd->hdr.return_code);
	}
        return 0;
}

static int
qeth_layer2_send_setdelvlan(struct qeth_card *card, __u16 i,
			    enum qeth_ipa_cmds ipacmd)
{
	struct qeth_ipa_cmd *cmd;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT_(trace, 4, "L2sdv%x",ipacmd);
	iob = qeth_get_ipacmd_buffer(card, ipacmd, QETH_PROT_IPV4);
	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
        cmd->data.setdelvlan.vlan_id = i;
	return qeth_send_ipa_cmd(card, iob, 
				 qeth_layer2_send_setdelvlan_cb, NULL);
}

static void
qeth_layer2_process_vlans(struct qeth_card *card, int clear)
{
        unsigned short  i;

	QETH_DBF_TEXT(trace, 3, "L2prcvln");

	if (!card->vlangrp)
		return;
	for (i = 0; i < VLAN_GROUP_ARRAY_LEN; i++) {
		if (card->vlangrp->vlan_devices[i] == NULL)
			continue;
		if (clear)
			qeth_layer2_send_setdelvlan(card, i, IPA_CMD_DELVLAN);
		else
			qeth_layer2_send_setdelvlan(card, i, IPA_CMD_SETVLAN);
        }
}

/*add_vid is layer 2 used only ....*/
static void
qeth_vlan_rx_add_vid(struct net_device *dev, unsigned short vid)
{
	struct qeth_card *card;

	QETH_DBF_TEXT_(trace, 4, "aid:%d", vid);

	card = (struct qeth_card *) dev->priv;
	if (!card->options.layer2)
		return;
	qeth_layer2_send_setdelvlan(card, vid, IPA_CMD_SETVLAN);
}

/*... kill_vid used for both modes*/
static void
qeth_vlan_rx_kill_vid(struct net_device *dev, unsigned short vid)
{
	struct qeth_card *card;
	unsigned long flags;

	QETH_DBF_TEXT_(trace, 4, "kid:%d", vid);

	card = (struct qeth_card *) dev->priv;
	/* free all skbs for the vlan device */
	qeth_free_vlan_skbs(card, vid);
	spin_lock_irqsave(&card->vlanlock, flags);
	/* unregister IP addresses of vlan device */
	qeth_free_vlan_addresses(card, vid);
	if (card->vlangrp)
		card->vlangrp->vlan_devices[vid] = NULL;
	spin_unlock_irqrestore(&card->vlanlock, flags);
	if (card->options.layer2)
		qeth_layer2_send_setdelvlan(card, vid, IPA_CMD_DELVLAN);
	qeth_set_multicast_list(card->dev);
}
#endif
/**
 * Examine hardware response to SET_PROMISC_MODE
 */
static int
qeth_setadp_promisc_mode_cb(struct qeth_card *card, 
			    struct qeth_reply *reply,
			    unsigned long data)
{
	struct qeth_ipa_cmd *cmd;
	struct qeth_ipacmd_setadpparms *setparms;

	QETH_DBF_TEXT(trace,4,"prmadpcb");

	cmd = (struct qeth_ipa_cmd *) data;
	setparms = &(cmd->data.setadapterparms);
	
        qeth_default_setadapterparms_cb(card, reply, (unsigned long)cmd);
	if (cmd->hdr.return_code) { 
		QETH_DBF_TEXT_(trace,4,"prmrc%2.2x",cmd->hdr.return_code);	
		setparms->data.mode = SET_PROMISC_MODE_OFF;
	}
	card->info.promisc_mode = setparms->data.mode;
	return 0;
}
/*
 * Set promiscuous mode (on or off) (SET_PROMISC_MODE command)
 */
static void
qeth_setadp_promisc_mode(struct qeth_card *card)
{
	enum qeth_ipa_promisc_modes mode;
	struct net_device *dev = card->dev;
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(trace, 4, "setprom");

	if (((dev->flags & IFF_PROMISC) &&
	     (card->info.promisc_mode == SET_PROMISC_MODE_ON)) ||
	    (!(dev->flags & IFF_PROMISC) &&
	     (card->info.promisc_mode == SET_PROMISC_MODE_OFF)))
		return;
	mode = SET_PROMISC_MODE_OFF;
	if (dev->flags & IFF_PROMISC)
		mode = SET_PROMISC_MODE_ON;
	QETH_DBF_TEXT_(trace, 4, "mode:%x", mode);

	iob = qeth_get_adapter_cmd(card, IPA_SETADP_SET_PROMISC_MODE,
			sizeof(struct qeth_ipacmd_setadpparms));
	cmd = (struct qeth_ipa_cmd *)(iob->data + IPA_PDU_HEADER_SIZE);
	cmd->data.setadapterparms.data.mode = mode;
	qeth_send_ipa_cmd(card, iob, qeth_setadp_promisc_mode_cb, NULL);
}

/**
 * set multicast address on card
 */
static void
qeth_set_multicast_list(struct net_device *dev)
{
	struct qeth_card *card = (struct qeth_card *) dev->priv;

	if (card->info.type == QETH_CARD_TYPE_OSN)
		return ;
	 
	QETH_DBF_TEXT(trace,3,"setmulti");
	qeth_delete_mc_addresses(card);
	if (card->options.layer2) {
		qeth_layer2_add_multicast(card);
		goto out;
	}
	qeth_add_multicast_ipv4(card);
#ifdef CONFIG_QETH_IPV6
	qeth_add_multicast_ipv6(card);
#endif
out:
 	if (qeth_set_thread_start_bit(card, QETH_SET_IP_THREAD) == 0)
		schedule_work(&card->kernel_thread_starter);
	if (!qeth_adp_supported(card, IPA_SETADP_SET_PROMISC_MODE))
		return;
	if (qeth_set_thread_start_bit(card, QETH_SET_PROMISC_MODE_THREAD)==0)
		schedule_work(&card->kernel_thread_starter);

}

static int
qeth_neigh_setup(struct net_device *dev, struct neigh_parms *np)
{
	return 0;
}

static void
qeth_get_mac_for_ipm(__u32 ipm, char *mac, struct net_device *dev)
{
	if (dev->type == ARPHRD_IEEE802_TR)
		ip_tr_mc_map(ipm, mac);
	else
		ip_eth_mc_map(ipm, mac);
}

static struct qeth_ipaddr *
qeth_get_addr_buffer(enum qeth_prot_versions prot)
{
	struct qeth_ipaddr *addr;

	addr = kmalloc(sizeof(struct qeth_ipaddr), GFP_ATOMIC);
	if (addr == NULL) {
		PRINT_WARN("Not enough memory to add address\n");
		return NULL;
	}
	memset(addr,0,sizeof(struct qeth_ipaddr));
	addr->type = QETH_IP_TYPE_NORMAL;
	addr->proto = prot;
	return addr;
}

int
qeth_osn_assist(struct net_device *dev,
		void *data,
		int data_len)
{
	struct qeth_cmd_buffer *iob;
	struct qeth_card *card;
	int rc;
	
	QETH_DBF_TEXT(trace, 2, "osnsdmc");
	if (!dev)
		return -ENODEV;
	card = (struct qeth_card *)dev->priv;
	if (!card)
		return -ENODEV;
	if ((card->state != CARD_STATE_UP) &&
	    (card->state != CARD_STATE_SOFTSETUP))
		return -ENODEV;
	iob = qeth_wait_for_buffer(&card->write);
	memcpy(iob->data+IPA_PDU_HEADER_SIZE, data, data_len);
	rc = qeth_osn_send_ipa_cmd(card, iob, data_len);
	return rc;
}

static struct net_device *
qeth_netdev_by_devno(unsigned char *read_dev_no)
{
	struct qeth_card *card;
	struct net_device *ndev;
	unsigned char *readno;
	__u16 temp_dev_no, card_dev_no;
	char *endp;
	unsigned long flags;

	ndev = NULL;
	memcpy(&temp_dev_no, read_dev_no, 2);
	read_lock_irqsave(&qeth_card_list.rwlock, flags);
	list_for_each_entry(card, &qeth_card_list.list, list) {
		readno = CARD_RDEV_ID(card);
		readno += (strlen(readno) - 4);
		card_dev_no = simple_strtoul(readno, &endp, 16);
		if (card_dev_no == temp_dev_no) {
			ndev = card->dev;
			break;
		}
	}
	read_unlock_irqrestore(&qeth_card_list.rwlock, flags);
	return ndev;
}

int
qeth_osn_register(unsigned char *read_dev_no,
		  struct net_device **dev,
		  int (*assist_cb)(struct net_device *, void *),
		  int (*data_cb)(struct sk_buff *))
{
	struct qeth_card * card;

	QETH_DBF_TEXT(trace, 2, "osnreg");
	*dev = qeth_netdev_by_devno(read_dev_no);
	if (*dev == NULL)
		return -ENODEV;
	card = (struct qeth_card *)(*dev)->priv;
	if (!card)
		return -ENODEV;
	if ((assist_cb == NULL) || (data_cb == NULL))
		return -EINVAL;
	card->osn_info.assist_cb = assist_cb;
	card->osn_info.data_cb = data_cb;
	return 0;
}

void
qeth_osn_deregister(struct net_device * dev)
{
	struct qeth_card *card;

	QETH_DBF_TEXT(trace, 2, "osndereg");
	if (!dev)
		return;
	card = (struct qeth_card *)dev->priv;
	if (!card)
		return;
	card->osn_info.assist_cb = NULL;
	card->osn_info.data_cb = NULL;
	return;
}
					   
static void
qeth_delete_mc_addresses(struct qeth_card *card)
{
	struct qeth_ipaddr *iptodo;
	unsigned long flags;

	QETH_DBF_TEXT(trace,4,"delmc");
	iptodo = qeth_get_addr_buffer(QETH_PROT_IPV4);
	if (!iptodo) {
		QETH_DBF_TEXT(trace, 2, "dmcnomem");
		return;
	}
	iptodo->type = QETH_IP_TYPE_DEL_ALL_MC;
	spin_lock_irqsave(&card->ip_lock, flags);
	if (!__qeth_insert_ip_todo(card, iptodo, 0))
		kfree(iptodo);
	spin_unlock_irqrestore(&card->ip_lock, flags);
}

static inline void
qeth_add_mc(struct qeth_card *card, struct in_device *in4_dev)
{
	struct qeth_ipaddr *ipm;
	struct ip_mc_list *im4;
	char buf[MAX_ADDR_LEN];

	QETH_DBF_TEXT(trace,4,"addmc");
	for (im4 = in4_dev->mc_list; im4; im4 = im4->next) {
		qeth_get_mac_for_ipm(im4->multiaddr, buf, in4_dev->dev);
		ipm = qeth_get_addr_buffer(QETH_PROT_IPV4);
		if (!ipm)
			continue;
		ipm->u.a4.addr = im4->multiaddr;
		memcpy(ipm->mac,buf,OSA_ADDR_LEN);
		ipm->is_multicast = 1;
		if (!qeth_add_ip(card,ipm))
			kfree(ipm);
	}
}

static inline void
qeth_add_vlan_mc(struct qeth_card *card)
{
#ifdef CONFIG_QETH_VLAN
	struct in_device *in_dev;
	struct vlan_group *vg;
	int i;

	QETH_DBF_TEXT(trace,4,"addmcvl");
	if ( ((card->options.layer2 == 0) &&
	      (!qeth_is_supported(card,IPA_FULL_VLAN))) ||
	     (card->vlangrp == NULL) )
		return ;

	vg = card->vlangrp;
	for (i = 0; i < VLAN_GROUP_ARRAY_LEN; i++) {
		if (vg->vlan_devices[i] == NULL ||
		    !(vg->vlan_devices[i]->flags & IFF_UP))
			continue;
		in_dev = in_dev_get(vg->vlan_devices[i]);
		if (!in_dev)
			continue;
		read_lock(&in_dev->mc_list_lock);
		qeth_add_mc(card,in_dev);
		read_unlock(&in_dev->mc_list_lock);
		in_dev_put(in_dev);
	}
#endif
}

static void
qeth_add_multicast_ipv4(struct qeth_card *card)
{
	struct in_device *in4_dev;

	QETH_DBF_TEXT(trace,4,"chkmcv4");
	in4_dev = in_dev_get(card->dev);
	if (in4_dev == NULL)
		return;
	read_lock(&in4_dev->mc_list_lock);
	qeth_add_mc(card, in4_dev);
	qeth_add_vlan_mc(card);
	read_unlock(&in4_dev->mc_list_lock);
	in_dev_put(in4_dev);
}

static void
qeth_layer2_add_multicast(struct qeth_card *card)
{
	struct qeth_ipaddr *ipm;
	struct dev_mc_list *dm;

	QETH_DBF_TEXT(trace,4,"L2addmc");
	for (dm = card->dev->mc_list; dm; dm = dm->next) {
		ipm = qeth_get_addr_buffer(QETH_PROT_IPV4);
		if (!ipm)
			continue;
		memcpy(ipm->mac,dm->dmi_addr,MAX_ADDR_LEN);
		ipm->is_multicast = 1;
		if (!qeth_add_ip(card, ipm))
			kfree(ipm);
	}
}

#ifdef CONFIG_QETH_IPV6
static inline void
qeth_add_mc6(struct qeth_card *card, struct inet6_dev *in6_dev)
{
	struct qeth_ipaddr *ipm;
	struct ifmcaddr6 *im6;
	char buf[MAX_ADDR_LEN];

	QETH_DBF_TEXT(trace,4,"addmc6");
	for (im6 = in6_dev->mc_list; im6 != NULL; im6 = im6->next) {
		ndisc_mc_map(&im6->mca_addr, buf, in6_dev->dev, 0);
		ipm = qeth_get_addr_buffer(QETH_PROT_IPV6);
		if (!ipm)
			continue;
		ipm->is_multicast = 1;
		memcpy(ipm->mac,buf,OSA_ADDR_LEN);
		memcpy(&ipm->u.a6.addr,&im6->mca_addr.s6_addr,
		       sizeof(struct in6_addr));
		if (!qeth_add_ip(card,ipm))
			kfree(ipm);
	}
}

static inline void
qeth_add_vlan_mc6(struct qeth_card *card)
{
#ifdef CONFIG_QETH_VLAN
	struct inet6_dev *in_dev;
	struct vlan_group *vg;
	int i;

	QETH_DBF_TEXT(trace,4,"admc6vl");
	if ( ((card->options.layer2 == 0) &&
	      (!qeth_is_supported(card,IPA_FULL_VLAN))) ||
	     (card->vlangrp == NULL))
		return ;

	vg = card->vlangrp;
	for (i = 0; i < VLAN_GROUP_ARRAY_LEN; i++) {
		if (vg->vlan_devices[i] == NULL ||
		    !(vg->vlan_devices[i]->flags & IFF_UP))
			continue;
		in_dev = in6_dev_get(vg->vlan_devices[i]);
		if (!in_dev)
			continue;
		read_lock(&in_dev->lock);
		qeth_add_mc6(card,in_dev);
		read_unlock(&in_dev->lock);
		in6_dev_put(in_dev);
	}
#endif /* CONFIG_QETH_VLAN */
}

static void
qeth_add_multicast_ipv6(struct qeth_card *card)
{
	struct inet6_dev *in6_dev;

	QETH_DBF_TEXT(trace,4,"chkmcv6");
	if (!qeth_is_supported(card, IPA_IPV6)) 
		return ;
	in6_dev = in6_dev_get(card->dev);
	if (in6_dev == NULL)
		return;
	read_lock(&in6_dev->lock);
	qeth_add_mc6(card, in6_dev);
	qeth_add_vlan_mc6(card);
	read_unlock(&in6_dev->lock);
	in6_dev_put(in6_dev);
}
#endif /* CONFIG_QETH_IPV6 */

static int
qeth_layer2_send_setdelmac(struct qeth_card *card, __u8 *mac,
			   enum qeth_ipa_cmds ipacmd,
			   int (*reply_cb) (struct qeth_card *,
					    struct qeth_reply*,
					    unsigned long))
{
	struct qeth_ipa_cmd *cmd;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(trace, 2, "L2sdmac");
	iob = qeth_get_ipacmd_buffer(card, ipacmd, QETH_PROT_IPV4);
	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
        cmd->data.setdelmac.mac_length = OSA_ADDR_LEN;
        memcpy(&cmd->data.setdelmac.mac, mac, OSA_ADDR_LEN);
	return qeth_send_ipa_cmd(card, iob, reply_cb, NULL);
}

static int
qeth_layer2_send_setgroupmac_cb(struct qeth_card *card,
				struct qeth_reply *reply,
				unsigned long data)
{
	struct qeth_ipa_cmd *cmd;
	__u8 *mac;

	QETH_DBF_TEXT(trace, 2, "L2Sgmacb");
	cmd = (struct qeth_ipa_cmd *) data;
	mac = &cmd->data.setdelmac.mac[0];
	/* MAC already registered, needed in couple/uncouple case */
	if (cmd->hdr.return_code == 0x2005) {
		PRINT_WARN("Group MAC %02x:%02x:%02x:%02x:%02x:%02x " \
			  "already existing on %s \n",
			  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
			  QETH_CARD_IFNAME(card));
		cmd->hdr.return_code = 0;
	}
	if (cmd->hdr.return_code)
		PRINT_ERR("Could not set group MAC " \
			  "%02x:%02x:%02x:%02x:%02x:%02x on %s: %x\n",
			  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
			  QETH_CARD_IFNAME(card),cmd->hdr.return_code);
	return 0;
}

static int
qeth_layer2_send_setgroupmac(struct qeth_card *card, __u8 *mac)
{
	QETH_DBF_TEXT(trace, 2, "L2Sgmac");
	return qeth_layer2_send_setdelmac(card, mac, IPA_CMD_SETGMAC,
					  qeth_layer2_send_setgroupmac_cb);
}

static int
qeth_layer2_send_delgroupmac_cb(struct qeth_card *card,
				struct qeth_reply *reply,
				unsigned long data)
{
	struct qeth_ipa_cmd *cmd;
	__u8 *mac;

	QETH_DBF_TEXT(trace, 2, "L2Dgmacb");
	cmd = (struct qeth_ipa_cmd *) data;
	mac = &cmd->data.setdelmac.mac[0];
	if (cmd->hdr.return_code)
		PRINT_ERR("Could not delete group MAC " \
			  "%02x:%02x:%02x:%02x:%02x:%02x on %s: %x\n",
			  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
			  QETH_CARD_IFNAME(card), cmd->hdr.return_code);
	return 0;
}

static int
qeth_layer2_send_delgroupmac(struct qeth_card *card, __u8 *mac)
{
	QETH_DBF_TEXT(trace, 2, "L2Dgmac");
	return qeth_layer2_send_setdelmac(card, mac, IPA_CMD_DELGMAC,
					  qeth_layer2_send_delgroupmac_cb);
}

static int
qeth_layer2_send_setmac_cb(struct qeth_card *card,
			   struct qeth_reply *reply,
			   unsigned long data)
{
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(trace, 2, "L2Smaccb");
	cmd = (struct qeth_ipa_cmd *) data;
	if (cmd->hdr.return_code) {
		QETH_DBF_TEXT_(trace, 2, "L2er%x", cmd->hdr.return_code);
		PRINT_WARN("Error in registering MAC address on " \
			   "device %s: x%x\n", CARD_BUS_ID(card),
			   cmd->hdr.return_code);
		card->info.mac_bits &= ~QETH_LAYER2_MAC_REGISTERED;
		cmd->hdr.return_code = -EIO;
	} else {
		card->info.mac_bits |= QETH_LAYER2_MAC_REGISTERED;
		memcpy(card->dev->dev_addr,cmd->data.setdelmac.mac,
		       OSA_ADDR_LEN);
		PRINT_INFO("MAC address %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x "
			   "successfully registered on device %s\n",
			   card->dev->dev_addr[0], card->dev->dev_addr[1],
			   card->dev->dev_addr[2], card->dev->dev_addr[3],
			   card->dev->dev_addr[4], card->dev->dev_addr[5],
			   card->dev->name);
	}
	return 0;
}

static int
qeth_layer2_send_setmac(struct qeth_card *card, __u8 *mac)
{
	QETH_DBF_TEXT(trace, 2, "L2Setmac");
	return qeth_layer2_send_setdelmac(card, mac, IPA_CMD_SETVMAC,
					  qeth_layer2_send_setmac_cb);
}

static int
qeth_layer2_send_delmac_cb(struct qeth_card *card,
			   struct qeth_reply *reply,
			   unsigned long data)
{
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(trace, 2, "L2Dmaccb");
	cmd = (struct qeth_ipa_cmd *) data;
	if (cmd->hdr.return_code) {
		PRINT_WARN("Error in deregistering MAC address on " \
			   "device %s: x%x\n", CARD_BUS_ID(card),
			   cmd->hdr.return_code);
		QETH_DBF_TEXT_(trace, 2, "err%d", cmd->hdr.return_code);
		cmd->hdr.return_code = -EIO;
		return 0;
	}
	card->info.mac_bits &= ~QETH_LAYER2_MAC_REGISTERED;

	return 0;
}
static int
qeth_layer2_send_delmac(struct qeth_card *card, __u8 *mac)
{
	QETH_DBF_TEXT(trace, 2, "L2Delmac");
	if (!(card->info.mac_bits & QETH_LAYER2_MAC_REGISTERED))
		return 0;
	return qeth_layer2_send_setdelmac(card, mac, IPA_CMD_DELVMAC,
					  qeth_layer2_send_delmac_cb);
}

static int
qeth_layer2_set_mac_address(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	struct qeth_card *card;
	int rc = 0;

	QETH_DBF_TEXT(trace, 3, "setmac");

	if (qeth_verify_dev(dev) != QETH_REAL_CARD) {
		QETH_DBF_TEXT(trace, 3, "setmcINV");
		return -EOPNOTSUPP;
	}
	card = (struct qeth_card *) dev->priv;

	if (!card->options.layer2) {
		PRINT_WARN("Setting MAC address on %s is not supported "
			   "in Layer 3 mode.\n", dev->name);
		QETH_DBF_TEXT(trace, 3, "setmcLY3");
		return -EOPNOTSUPP;
	}
	if (card->info.type == QETH_CARD_TYPE_OSN) {
		PRINT_WARN("Setting MAC address on %s is not supported.\n",
			   dev->name);
		QETH_DBF_TEXT(trace, 3, "setmcOSN");
		return -EOPNOTSUPP;
	}
	QETH_DBF_TEXT_(trace, 3, "%s", CARD_BUS_ID(card));
	QETH_DBF_HEX(trace, 3, addr->sa_data, OSA_ADDR_LEN);
	rc = qeth_layer2_send_delmac(card, &card->dev->dev_addr[0]);
	if (!rc)
		rc = qeth_layer2_send_setmac(card, addr->sa_data);
	return rc;
}

static void
qeth_fill_ipacmd_header(struct qeth_card *card, struct qeth_ipa_cmd *cmd,
			__u8 command, enum qeth_prot_versions prot)
{
	memset(cmd, 0, sizeof (struct qeth_ipa_cmd));
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

static struct qeth_cmd_buffer *
qeth_get_ipacmd_buffer(struct qeth_card *card, enum qeth_ipa_cmds ipacmd,
		       enum qeth_prot_versions prot)
{
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;

	iob = qeth_wait_for_buffer(&card->write);
	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
	qeth_fill_ipacmd_header(card, cmd, ipacmd, prot);

	return iob;
}

static int
qeth_send_setdelmc(struct qeth_card *card, struct qeth_ipaddr *addr, int ipacmd)
{
	int rc;
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(trace,4,"setdelmc");

	iob = qeth_get_ipacmd_buffer(card, ipacmd, addr->proto);
	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
	memcpy(&cmd->data.setdelipm.mac,addr->mac, OSA_ADDR_LEN);
	if (addr->proto == QETH_PROT_IPV6)
		memcpy(cmd->data.setdelipm.ip6, &addr->u.a6.addr,
		       sizeof(struct in6_addr));
	else
		memcpy(&cmd->data.setdelipm.ip4, &addr->u.a4.addr,4);

	rc = qeth_send_ipa_cmd(card, iob, NULL, NULL);

	return rc;
}
static inline void
qeth_fill_netmask(u8 *netmask, unsigned int len)
{
	int i,j;
	for (i=0;i<16;i++) {
		j=(len)-(i*8);
		if (j >= 8)
			netmask[i] = 0xff;
		else if (j > 0)
			netmask[i] = (u8)(0xFF00>>j);
		else
			netmask[i] = 0;
	}
}

static int
qeth_send_setdelip(struct qeth_card *card, struct qeth_ipaddr *addr,
		   int ipacmd, unsigned int flags)
{
	int rc;
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;
	__u8 netmask[16];

	QETH_DBF_TEXT(trace,4,"setdelip");
	QETH_DBF_TEXT_(trace,4,"flags%02X", flags);

	iob = qeth_get_ipacmd_buffer(card, ipacmd, addr->proto);
	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
	if (addr->proto == QETH_PROT_IPV6) {
		memcpy(cmd->data.setdelip6.ip_addr, &addr->u.a6.addr,
		       sizeof(struct in6_addr));
		qeth_fill_netmask(netmask,addr->u.a6.pfxlen);
		memcpy(cmd->data.setdelip6.mask, netmask,
		       sizeof(struct in6_addr));
		cmd->data.setdelip6.flags = flags;
	} else {
		memcpy(cmd->data.setdelip4.ip_addr, &addr->u.a4.addr, 4);
		memcpy(cmd->data.setdelip4.mask, &addr->u.a4.mask, 4);
		cmd->data.setdelip4.flags = flags;
	}

	rc = qeth_send_ipa_cmd(card, iob, NULL, NULL);

	return rc;
}

static int
qeth_layer2_register_addr_entry(struct qeth_card *card,
				struct qeth_ipaddr *addr)
{
	if (!addr->is_multicast)
		return 0;
	QETH_DBF_TEXT(trace, 2, "setgmac");
	QETH_DBF_HEX(trace,3,&addr->mac[0],OSA_ADDR_LEN);
	return qeth_layer2_send_setgroupmac(card, &addr->mac[0]);
}

static int
qeth_layer2_deregister_addr_entry(struct qeth_card *card,
				  struct qeth_ipaddr *addr)
{
	if (!addr->is_multicast)
		return 0;
	QETH_DBF_TEXT(trace, 2, "delgmac");
	QETH_DBF_HEX(trace,3,&addr->mac[0],OSA_ADDR_LEN);
	return qeth_layer2_send_delgroupmac(card, &addr->mac[0]);
}

static int
qeth_layer3_register_addr_entry(struct qeth_card *card,
				struct qeth_ipaddr *addr)
{
	char buf[50];
	int rc;
	int cnt = 3;

	if (addr->proto == QETH_PROT_IPV4) {
		QETH_DBF_TEXT(trace, 2,"setaddr4");
		QETH_DBF_HEX(trace, 3, &addr->u.a4.addr, sizeof(int));
	} else if (addr->proto == QETH_PROT_IPV6) {
		QETH_DBF_TEXT(trace, 2, "setaddr6");
		QETH_DBF_HEX(trace,3,&addr->u.a6.addr,8);
		QETH_DBF_HEX(trace,3,((char *)&addr->u.a6.addr)+8,8);
	} else {
		QETH_DBF_TEXT(trace, 2, "setaddr?");
		QETH_DBF_HEX(trace, 3, addr, sizeof(struct qeth_ipaddr));
	}
	do {
		if (addr->is_multicast)
			rc =  qeth_send_setdelmc(card, addr, IPA_CMD_SETIPM);
		else
			rc = qeth_send_setdelip(card, addr, IPA_CMD_SETIP,
					addr->set_flags);
		if (rc)
			QETH_DBF_TEXT(trace, 2, "failed");
	} while ((--cnt > 0) && rc);
	if (rc){
		QETH_DBF_TEXT(trace, 2, "FAILED");
		qeth_ipaddr_to_string(addr->proto, (u8 *)&addr->u, buf);
		PRINT_WARN("Could not register IP address %s (rc=0x%x/%d)\n",
			   buf, rc, rc);
	}
	return rc;
}

static int
qeth_layer3_deregister_addr_entry(struct qeth_card *card,
				  struct qeth_ipaddr *addr)
{
	//char buf[50];
	int rc;

	if (addr->proto == QETH_PROT_IPV4) {
		QETH_DBF_TEXT(trace, 2,"deladdr4");
		QETH_DBF_HEX(trace, 3, &addr->u.a4.addr, sizeof(int));
	} else if (addr->proto == QETH_PROT_IPV6) {
		QETH_DBF_TEXT(trace, 2, "deladdr6");
		QETH_DBF_HEX(trace,3,&addr->u.a6.addr,8);
		QETH_DBF_HEX(trace,3,((char *)&addr->u.a6.addr)+8,8);
	} else {
		QETH_DBF_TEXT(trace, 2, "deladdr?");
		QETH_DBF_HEX(trace, 3, addr, sizeof(struct qeth_ipaddr));
	}
	if (addr->is_multicast)
		rc = qeth_send_setdelmc(card, addr, IPA_CMD_DELIPM);
	else
		rc = qeth_send_setdelip(card, addr, IPA_CMD_DELIP,
					addr->del_flags);
	if (rc) {
		QETH_DBF_TEXT(trace, 2, "failed");
		/* TODO: re-activate this warning as soon as we have a
		 * clean mirco code
		qeth_ipaddr_to_string(addr->proto, (u8 *)&addr->u, buf);
		PRINT_WARN("Could not deregister IP address %s (rc=%x)\n",
			   buf, rc);
		*/
	}
	return rc;
}

static int
qeth_register_addr_entry(struct qeth_card *card, struct qeth_ipaddr *addr)
{
	if (card->options.layer2)
		return qeth_layer2_register_addr_entry(card, addr);

	return qeth_layer3_register_addr_entry(card, addr);
}

static int
qeth_deregister_addr_entry(struct qeth_card *card, struct qeth_ipaddr *addr)
{
	if (card->options.layer2)
		return qeth_layer2_deregister_addr_entry(card, addr);

	return qeth_layer3_deregister_addr_entry(card, addr);
}

static u32
qeth_ethtool_get_tx_csum(struct net_device *dev)
{
	/* We may need to say that we support tx csum offload if
	 * we do EDDP or TSO. There are discussions going on to
	 * enforce rules in the stack and in ethtool that make
	 * SG and TSO depend on HW_CSUM. At the moment there are
	 * no such rules....
	 * If we say yes here, we have to checksum outbound packets
	 * any time. */
	return 0;
}

static int
qeth_ethtool_set_tx_csum(struct net_device *dev, u32 data)
{
	return -EINVAL;
}

static u32
qeth_ethtool_get_rx_csum(struct net_device *dev)
{
	struct qeth_card *card = (struct qeth_card *)dev->priv;

	return (card->options.checksum_type == HW_CHECKSUMMING);
}

static int
qeth_ethtool_set_rx_csum(struct net_device *dev, u32 data)
{
	struct qeth_card *card = (struct qeth_card *)dev->priv;

	if ((card->state != CARD_STATE_DOWN) &&
	    (card->state != CARD_STATE_RECOVER))
		return -EPERM;
	if (data)
		card->options.checksum_type = HW_CHECKSUMMING;
	else
		card->options.checksum_type = SW_CHECKSUMMING;
	return 0;
}

static u32
qeth_ethtool_get_sg(struct net_device *dev)
{
	struct qeth_card *card = (struct qeth_card *)dev->priv;

	return ((card->options.large_send != QETH_LARGE_SEND_NO) &&
		(dev->features & NETIF_F_SG));
}

static int
qeth_ethtool_set_sg(struct net_device *dev, u32 data)
{
	struct qeth_card *card = (struct qeth_card *)dev->priv;

	if (data) {
		if (card->options.large_send != QETH_LARGE_SEND_NO)
			dev->features |= NETIF_F_SG;
		else {
			dev->features &= ~NETIF_F_SG;
			return -EINVAL;
		}
	} else
		dev->features &= ~NETIF_F_SG;
	return 0;
}

static u32
qeth_ethtool_get_tso(struct net_device *dev)
{
	struct qeth_card *card = (struct qeth_card *)dev->priv;

	return ((card->options.large_send != QETH_LARGE_SEND_NO) &&
		(dev->features & NETIF_F_TSO));
}

static int
qeth_ethtool_set_tso(struct net_device *dev, u32 data)
{
	struct qeth_card *card = (struct qeth_card *)dev->priv;

	if (data) {
		if (card->options.large_send != QETH_LARGE_SEND_NO)
			dev->features |= NETIF_F_TSO;
		else {
			dev->features &= ~NETIF_F_TSO;
			return -EINVAL;
		}
	} else
		dev->features &= ~NETIF_F_TSO;
	return 0;
}

static struct ethtool_ops qeth_ethtool_ops = {
	.get_tx_csum = qeth_ethtool_get_tx_csum,
	.set_tx_csum = qeth_ethtool_set_tx_csum,
	.get_rx_csum = qeth_ethtool_get_rx_csum,
	.set_rx_csum = qeth_ethtool_set_rx_csum,
	.get_sg      = qeth_ethtool_get_sg,
	.set_sg      = qeth_ethtool_set_sg,
	.get_tso     = qeth_ethtool_get_tso,
	.set_tso     = qeth_ethtool_set_tso,
};

static int
qeth_netdev_init(struct net_device *dev)
{
	struct qeth_card *card;

	card = (struct qeth_card *) dev->priv;

	QETH_DBF_TEXT(trace,3,"initdev");

	dev->tx_timeout = &qeth_tx_timeout;
	dev->watchdog_timeo = QETH_TX_TIMEOUT;
	dev->open = qeth_open;
	dev->stop = qeth_stop;
	dev->hard_start_xmit = qeth_hard_start_xmit;
	dev->do_ioctl = qeth_do_ioctl;
	dev->get_stats = qeth_get_stats;
	dev->change_mtu = qeth_change_mtu;
	dev->neigh_setup = qeth_neigh_setup;
	dev->set_multicast_list = qeth_set_multicast_list;
#ifdef CONFIG_QETH_VLAN
	dev->vlan_rx_register = qeth_vlan_rx_register;
	dev->vlan_rx_kill_vid = qeth_vlan_rx_kill_vid;
	dev->vlan_rx_add_vid = qeth_vlan_rx_add_vid;
#endif
	dev->hard_header = card->orig_hard_header;
	if (qeth_get_netdev_flags(card) & IFF_NOARP) {
		dev->rebuild_header = NULL;
		dev->hard_header = NULL;
		if (card->options.fake_ll)
			dev->hard_header = qeth_fake_header;
		dev->header_cache_update = NULL;
		dev->hard_header_cache = NULL;
	}
#ifdef CONFIG_QETH_IPV6
	/*IPv6 address autoconfiguration stuff*/
	if (!(card->info.unique_id & UNIQUE_ID_NOT_BY_CARD))
		card->dev->dev_id = card->info.unique_id & 0xffff;
#endif
	dev->hard_header_parse = NULL;
	dev->set_mac_address = qeth_layer2_set_mac_address;
	dev->flags |= qeth_get_netdev_flags(card);
	if ((card->options.fake_broadcast) ||
	    (card->info.broadcast_capable))
		dev->flags |= IFF_BROADCAST;
	dev->hard_header_len =
			qeth_get_hlen(card->info.link_type) + card->options.add_hhlen;
	dev->addr_len = OSA_ADDR_LEN;
	dev->mtu = card->info.initial_mtu;
	if (card->info.type != QETH_CARD_TYPE_OSN)
		SET_ETHTOOL_OPS(dev, &qeth_ethtool_ops);
	SET_MODULE_OWNER(dev);
	return 0;
}

static void
qeth_init_func_level(struct qeth_card *card)
{
	if (card->ipato.enabled) {
		if (card->info.type == QETH_CARD_TYPE_IQD)
				card->info.func_level =
					QETH_IDX_FUNC_LEVEL_IQD_ENA_IPAT;
		else
				card->info.func_level =
					QETH_IDX_FUNC_LEVEL_OSAE_ENA_IPAT;
	} else {
		if (card->info.type == QETH_CARD_TYPE_IQD)
		/*FIXME:why do we have same values for  dis and ena for osae??? */
			card->info.func_level =
				QETH_IDX_FUNC_LEVEL_IQD_DIS_IPAT;
		else
			card->info.func_level =
				QETH_IDX_FUNC_LEVEL_OSAE_DIS_IPAT;
	}
}

/**
 * hardsetup card, initialize MPC and QDIO stuff
 */
static int
qeth_hardsetup_card(struct qeth_card *card)
{
	int retries = 3;
	int rc;

	QETH_DBF_TEXT(setup, 2, "hrdsetup");

retry:
	if (retries < 3){
		PRINT_WARN("Retrying to do IDX activates.\n");
		ccw_device_set_offline(CARD_DDEV(card));
		ccw_device_set_offline(CARD_WDEV(card));
		ccw_device_set_offline(CARD_RDEV(card));
		ccw_device_set_online(CARD_RDEV(card));
		ccw_device_set_online(CARD_WDEV(card));
		ccw_device_set_online(CARD_DDEV(card));
	}
	rc = qeth_qdio_clear_card(card,card->info.type!=QETH_CARD_TYPE_IQD);
	if (rc == -ERESTARTSYS) {
		QETH_DBF_TEXT(setup, 2, "break1");
		return rc;
	} else if (rc) {
		QETH_DBF_TEXT_(setup, 2, "1err%d", rc);
		if (--retries < 0)
			goto out;
		else
			goto retry;
	}
	if ((rc = qeth_get_unitaddr(card))){
		QETH_DBF_TEXT_(setup, 2, "2err%d", rc);
		return rc;
	}
	qeth_init_tokens(card);
	qeth_init_func_level(card);
	rc = qeth_idx_activate_channel(&card->read, qeth_idx_read_cb);
	if (rc == -ERESTARTSYS) {
		QETH_DBF_TEXT(setup, 2, "break2");
		return rc;
	} else if (rc) {
		QETH_DBF_TEXT_(setup, 2, "3err%d", rc);
		if (--retries < 0)
			goto out;
		else
			goto retry;
	}
	rc = qeth_idx_activate_channel(&card->write, qeth_idx_write_cb);
	if (rc == -ERESTARTSYS) {
		QETH_DBF_TEXT(setup, 2, "break3");
		return rc;
	} else if (rc) {
		QETH_DBF_TEXT_(setup, 2, "4err%d", rc);
		if (--retries < 0)
			goto out;
		else
			goto retry;
	}
	if ((rc = qeth_mpc_initialize(card))){
		QETH_DBF_TEXT_(setup, 2, "5err%d", rc);
		goto out;
	}
	/*network device will be recovered*/
	if (card->dev) {
		card->dev->hard_header = card->orig_hard_header;
		return 0;
	}
	/* at first set_online allocate netdev */
	card->dev = qeth_get_netdevice(card->info.type,
				       card->info.link_type);
	if (!card->dev){
		qeth_qdio_clear_card(card, card->info.type !=
				     QETH_CARD_TYPE_IQD);
		rc = -ENODEV;
		QETH_DBF_TEXT_(setup, 2, "6err%d", rc);
		goto out;
	}
	card->dev->priv = card;
	card->orig_hard_header = card->dev->hard_header;
	card->dev->type = qeth_get_arphdr_type(card->info.type,
					       card->info.link_type);
	card->dev->init = qeth_netdev_init;
	return 0;
out:
	PRINT_ERR("Initialization in hardsetup failed! rc=%d\n", rc);
	return rc;
}

static int
qeth_default_setassparms_cb(struct qeth_card *card, struct qeth_reply *reply,
			    unsigned long data)
{
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(trace,4,"defadpcb");

	cmd = (struct qeth_ipa_cmd *) data;
	if (cmd->hdr.return_code == 0){
		cmd->hdr.return_code = cmd->data.setassparms.hdr.return_code;
		if (cmd->hdr.prot_version == QETH_PROT_IPV4)
			card->options.ipa4.enabled_funcs = cmd->hdr.ipa_enabled;
#ifdef CONFIG_QETH_IPV6
		if (cmd->hdr.prot_version == QETH_PROT_IPV6)
			card->options.ipa6.enabled_funcs = cmd->hdr.ipa_enabled;
#endif
	}
	if (cmd->data.setassparms.hdr.assist_no == IPA_INBOUND_CHECKSUM &&
	    cmd->data.setassparms.hdr.command_code == IPA_CMD_ASS_START) {
		card->info.csum_mask = cmd->data.setassparms.data.flags_32bit;
		QETH_DBF_TEXT_(trace, 3, "csum:%d", card->info.csum_mask);
	}
	return 0;
}

static int
qeth_default_setadapterparms_cb(struct qeth_card *card,
				struct qeth_reply *reply,
				unsigned long data)
{
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(trace,4,"defadpcb");

	cmd = (struct qeth_ipa_cmd *) data;
	if (cmd->hdr.return_code == 0)
		cmd->hdr.return_code = cmd->data.setadapterparms.hdr.return_code;
	return 0;
}



static int
qeth_query_setadapterparms_cb(struct qeth_card *card, struct qeth_reply *reply,
			      unsigned long data)
{
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(trace,3,"quyadpcb");

	cmd = (struct qeth_ipa_cmd *) data;
	if (cmd->data.setadapterparms.data.query_cmds_supp.lan_type & 0x7f)
		card->info.link_type =
		      cmd->data.setadapterparms.data.query_cmds_supp.lan_type;
	card->options.adp.supported_funcs =
		cmd->data.setadapterparms.data.query_cmds_supp.supported_cmds;
	return qeth_default_setadapterparms_cb(card, reply, (unsigned long)cmd);
}

static int
qeth_query_setadapterparms(struct qeth_card *card)
{
	int rc;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(trace,3,"queryadp");
	iob = qeth_get_adapter_cmd(card, IPA_SETADP_QUERY_COMMANDS_SUPPORTED,
				   sizeof(struct qeth_ipacmd_setadpparms));
	rc = qeth_send_ipa_cmd(card, iob, qeth_query_setadapterparms_cb, NULL);
	return rc;
}

static int
qeth_setadpparms_change_macaddr_cb(struct qeth_card *card,
				   struct qeth_reply *reply,
				   unsigned long data)
{
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(trace,4,"chgmaccb");

	cmd = (struct qeth_ipa_cmd *) data;
	if (!card->options.layer2 || card->info.guestlan ||
	    !(card->info.mac_bits & QETH_LAYER2_MAC_READ)) {	
		memcpy(card->dev->dev_addr,
		       &cmd->data.setadapterparms.data.change_addr.addr,
		       OSA_ADDR_LEN);
		card->info.mac_bits |= QETH_LAYER2_MAC_READ;
	}
	qeth_default_setadapterparms_cb(card, reply, (unsigned long) cmd);
	return 0;
}

static int
qeth_setadpparms_change_macaddr(struct qeth_card *card)
{
	int rc;
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(trace,4,"chgmac");

	iob = qeth_get_adapter_cmd(card,IPA_SETADP_ALTER_MAC_ADDRESS,
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

static int
qeth_send_setadp_mode(struct qeth_card *card, __u32 command, __u32 mode)
{
	int rc;
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(trace,4,"adpmode");

	iob = qeth_get_adapter_cmd(card, command,
				   sizeof(struct qeth_ipacmd_setadpparms));
	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
	cmd->data.setadapterparms.data.mode = mode;
	rc = qeth_send_ipa_cmd(card, iob, qeth_default_setadapterparms_cb,
			       NULL);
	return rc;
}

static inline int
qeth_setadapter_hstr(struct qeth_card *card)
{
	int rc;

	QETH_DBF_TEXT(trace,4,"adphstr");

	if (qeth_adp_supported(card,IPA_SETADP_SET_BROADCAST_MODE)) {
		rc = qeth_send_setadp_mode(card, IPA_SETADP_SET_BROADCAST_MODE,
					   card->options.broadcast_mode);
		if (rc)
			PRINT_WARN("couldn't set broadcast mode on "
				   "device %s: x%x\n",
				   CARD_BUS_ID(card), rc);
		rc = qeth_send_setadp_mode(card, IPA_SETADP_ALTER_MAC_ADDRESS,
					   card->options.macaddr_mode);
		if (rc)
			PRINT_WARN("couldn't set macaddr mode on "
				   "device %s: x%x\n", CARD_BUS_ID(card), rc);
		return rc;
	}
	if (card->options.broadcast_mode == QETH_TR_BROADCAST_LOCAL)
		PRINT_WARN("set adapter parameters not available "
			   "to set broadcast mode, using ALLRINGS "
			   "on device %s:\n", CARD_BUS_ID(card));
	if (card->options.macaddr_mode == QETH_TR_MACADDR_CANONICAL)
		PRINT_WARN("set adapter parameters not available "
			   "to set macaddr mode, using NONCANONICAL "
			   "on device %s:\n", CARD_BUS_ID(card));
	return 0;
}

static int
qeth_setadapter_parms(struct qeth_card *card)
{
	int rc;

	QETH_DBF_TEXT(setup, 2, "setadprm");

	if (!qeth_is_supported(card, IPA_SETADAPTERPARMS)){
		PRINT_WARN("set adapter parameters not supported "
			   "on device %s.\n",
			   CARD_BUS_ID(card));
		QETH_DBF_TEXT(setup, 2, " notsupp");
		return 0;
	}
	rc = qeth_query_setadapterparms(card);
	if (rc) {
		PRINT_WARN("couldn't set adapter parameters on device %s: "
			   "x%x\n", CARD_BUS_ID(card), rc);
		return rc;
	}
	if (qeth_adp_supported(card,IPA_SETADP_ALTER_MAC_ADDRESS)) {
		rc = qeth_setadpparms_change_macaddr(card);
		if (rc)
			PRINT_WARN("couldn't get MAC address on "
				   "device %s: x%x\n",
				   CARD_BUS_ID(card), rc);
	}

	if ((card->info.link_type == QETH_LINK_TYPE_HSTR) ||
	    (card->info.link_type == QETH_LINK_TYPE_LANE_TR))
		rc = qeth_setadapter_hstr(card);

	return rc;
}

static int
qeth_layer2_initialize(struct qeth_card *card)
{
        int rc = 0;


        QETH_DBF_TEXT(setup, 2, "doL2init");
        QETH_DBF_TEXT_(setup, 2, "doL2%s", CARD_BUS_ID(card));

	rc = qeth_query_setadapterparms(card);
	if (rc) {
		PRINT_WARN("could not query adapter parameters on device %s: "
			   "x%x\n", CARD_BUS_ID(card), rc);
	}

	rc = qeth_setadpparms_change_macaddr(card);
	if (rc) {
		PRINT_WARN("couldn't get MAC address on "
			   "device %s: x%x\n",
			   CARD_BUS_ID(card), rc);
		QETH_DBF_TEXT_(setup, 2,"1err%d",rc);
		return rc;
        }
	QETH_DBF_HEX(setup,2, card->dev->dev_addr, OSA_ADDR_LEN);

	rc = qeth_layer2_send_setmac(card, &card->dev->dev_addr[0]);
        if (rc)
		QETH_DBF_TEXT_(setup, 2,"2err%d",rc);
        return 0;
}


static int
qeth_send_startstoplan(struct qeth_card *card, enum qeth_ipa_cmds ipacmd,
		       enum qeth_prot_versions prot)
{
	int rc;
	struct qeth_cmd_buffer *iob;

	iob = qeth_get_ipacmd_buffer(card,ipacmd,prot);
	rc = qeth_send_ipa_cmd(card, iob, NULL, NULL);

	return rc;
}

static int
qeth_send_startlan(struct qeth_card *card, enum qeth_prot_versions prot)
{
	int rc;

	QETH_DBF_TEXT_(setup, 2, "strtlan%i", prot);

	rc = qeth_send_startstoplan(card, IPA_CMD_STARTLAN, prot);
	return rc;
}

static int
qeth_send_stoplan(struct qeth_card *card)
{
	int rc = 0;

	/*
	 * TODO: according to the IPA format document page 14,
	 * TCP/IP (we!) never issue a STOPLAN
	 * is this right ?!?
	 */
	QETH_DBF_TEXT(trace, 2, "stoplan");

	rc = qeth_send_startstoplan(card, IPA_CMD_STOPLAN, QETH_PROT_IPV4);
	return rc;
}

static int
qeth_query_ipassists_cb(struct qeth_card *card, struct qeth_reply *reply,
			unsigned long data)
{
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(setup, 2, "qipasscb");

	cmd = (struct qeth_ipa_cmd *) data;
	if (cmd->hdr.prot_version == QETH_PROT_IPV4) {
		card->options.ipa4.supported_funcs = cmd->hdr.ipa_supported;
		card->options.ipa4.enabled_funcs = cmd->hdr.ipa_enabled;
		/* Disable IPV6 support hard coded for Hipersockets */
		if(card->info.type == QETH_CARD_TYPE_IQD)
			card->options.ipa4.supported_funcs &= ~IPA_IPV6;
	} else {
#ifdef CONFIG_QETH_IPV6
		card->options.ipa6.supported_funcs = cmd->hdr.ipa_supported;
		card->options.ipa6.enabled_funcs = cmd->hdr.ipa_enabled;
#endif
	}
	QETH_DBF_TEXT(setup, 2, "suppenbl");
	QETH_DBF_TEXT_(setup, 2, "%x",cmd->hdr.ipa_supported);
	QETH_DBF_TEXT_(setup, 2, "%x",cmd->hdr.ipa_enabled);
	return 0;
}

static int
qeth_query_ipassists(struct qeth_card *card, enum qeth_prot_versions prot)
{
	int rc;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT_(setup, 2, "qipassi%i", prot);
	if (card->options.layer2) {
		QETH_DBF_TEXT(setup, 2, "noprmly2");
		return -EPERM;
	}

	iob = qeth_get_ipacmd_buffer(card,IPA_CMD_QIPASSIST,prot);
	rc = qeth_send_ipa_cmd(card, iob, qeth_query_ipassists_cb, NULL);
	return rc;
}

static struct qeth_cmd_buffer *
qeth_get_setassparms_cmd(struct qeth_card *card, enum qeth_ipa_funcs ipa_func,
			 __u16 cmd_code, __u16 len,
			 enum qeth_prot_versions prot)
{
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(trace,4,"getasscm");
	iob = qeth_get_ipacmd_buffer(card,IPA_CMD_SETASSPARMS,prot);

	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
	cmd->data.setassparms.hdr.assist_no = ipa_func;
	cmd->data.setassparms.hdr.length = 8 + len;
	cmd->data.setassparms.hdr.command_code = cmd_code;
	cmd->data.setassparms.hdr.return_code = 0;
	cmd->data.setassparms.hdr.seq_no = 0;

	return iob;
}

static int
qeth_send_setassparms(struct qeth_card *card, struct qeth_cmd_buffer *iob,
		      __u16 len, long data,
		      int (*reply_cb)
		      (struct qeth_card *,struct qeth_reply *,unsigned long),
		      void *reply_param)
{
	int rc;
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(trace,4,"sendassp");

	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
	if (len <= sizeof(__u32))
		cmd->data.setassparms.data.flags_32bit = (__u32) data;
	else if (len > sizeof(__u32))
		memcpy(&cmd->data.setassparms.data, (void *) data, len);

	rc = qeth_send_ipa_cmd(card, iob, reply_cb, reply_param);
	return rc;
}

#ifdef CONFIG_QETH_IPV6
static int
qeth_send_simple_setassparms_ipv6(struct qeth_card *card,
				  enum qeth_ipa_funcs ipa_func, __u16 cmd_code)

{
	int rc;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(trace,4,"simassp6");
	iob = qeth_get_setassparms_cmd(card, ipa_func, cmd_code,
				       0, QETH_PROT_IPV6);
	rc = qeth_send_setassparms(card, iob, 0, 0,
				   qeth_default_setassparms_cb, NULL);
	return rc;
}
#endif

static int
qeth_send_simple_setassparms(struct qeth_card *card,
			     enum qeth_ipa_funcs ipa_func,
			     __u16 cmd_code, long data)
{
	int rc;
	int length = 0;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(trace,4,"simassp4");
	if (data)
		length = sizeof(__u32);
	iob = qeth_get_setassparms_cmd(card, ipa_func, cmd_code,
				       length, QETH_PROT_IPV4);
	rc = qeth_send_setassparms(card, iob, length, data,
				   qeth_default_setassparms_cb, NULL);
	return rc;
}

static inline int
qeth_start_ipa_arp_processing(struct qeth_card *card)
{
	int rc;

	QETH_DBF_TEXT(trace,3,"ipaarp");

	if (!qeth_is_supported(card,IPA_ARP_PROCESSING)) {
		PRINT_WARN("ARP processing not supported "
			   "on %s!\n", QETH_CARD_IFNAME(card));
		return 0;
	}
	rc = qeth_send_simple_setassparms(card,IPA_ARP_PROCESSING,
					  IPA_CMD_ASS_START, 0);
	if (rc) {
		PRINT_WARN("Could not start ARP processing "
			   "assist on %s: 0x%x\n",
			   QETH_CARD_IFNAME(card), rc);
	}
	return rc;
}

static int
qeth_start_ipa_ip_fragmentation(struct qeth_card *card)
{
	int rc;

	QETH_DBF_TEXT(trace,3,"ipaipfrg");

	if (!qeth_is_supported(card, IPA_IP_FRAGMENTATION)) {
		PRINT_INFO("Hardware IP fragmentation not supported on %s\n",
			   QETH_CARD_IFNAME(card));
		return  -EOPNOTSUPP;
	}

	rc = qeth_send_simple_setassparms(card, IPA_IP_FRAGMENTATION,
					  IPA_CMD_ASS_START, 0);
	if (rc) {
		PRINT_WARN("Could not start Hardware IP fragmentation "
			   "assist on %s: 0x%x\n",
			   QETH_CARD_IFNAME(card), rc);
	} else
		PRINT_INFO("Hardware IP fragmentation enabled \n");
	return rc;
}

static int
qeth_start_ipa_source_mac(struct qeth_card *card)
{
	int rc;

	QETH_DBF_TEXT(trace,3,"stsrcmac");

	if (!card->options.fake_ll)
		return -EOPNOTSUPP;

	if (!qeth_is_supported(card, IPA_SOURCE_MAC)) {
		PRINT_INFO("Inbound source address not "
			   "supported on %s\n", QETH_CARD_IFNAME(card));
		return -EOPNOTSUPP;
	}

	rc = qeth_send_simple_setassparms(card, IPA_SOURCE_MAC,
					  IPA_CMD_ASS_START, 0);
	if (rc)
		PRINT_WARN("Could not start inbound source "
			   "assist on %s: 0x%x\n",
			   QETH_CARD_IFNAME(card), rc);
	return rc;
}

static int
qeth_start_ipa_vlan(struct qeth_card *card)
{
	int rc = 0;

	QETH_DBF_TEXT(trace,3,"strtvlan");

#ifdef CONFIG_QETH_VLAN
	if (!qeth_is_supported(card, IPA_FULL_VLAN)) {
		PRINT_WARN("VLAN not supported on %s\n", QETH_CARD_IFNAME(card));
		return -EOPNOTSUPP;
	}

	rc = qeth_send_simple_setassparms(card, IPA_VLAN_PRIO,
					  IPA_CMD_ASS_START,0);
	if (rc) {
		PRINT_WARN("Could not start vlan "
			   "assist on %s: 0x%x\n",
			   QETH_CARD_IFNAME(card), rc);
	} else {
		PRINT_INFO("VLAN enabled \n");
		card->dev->features |=
			NETIF_F_HW_VLAN_FILTER |
			NETIF_F_HW_VLAN_TX |
			NETIF_F_HW_VLAN_RX;
	}
#endif /* QETH_VLAN */
	return rc;
}

static int
qeth_start_ipa_multicast(struct qeth_card *card)
{
	int rc;

	QETH_DBF_TEXT(trace,3,"stmcast");

	if (!qeth_is_supported(card, IPA_MULTICASTING)) {
		PRINT_WARN("Multicast not supported on %s\n",
			   QETH_CARD_IFNAME(card));
		return -EOPNOTSUPP;
	}

	rc = qeth_send_simple_setassparms(card, IPA_MULTICASTING,
					  IPA_CMD_ASS_START,0);
	if (rc) {
		PRINT_WARN("Could not start multicast "
			   "assist on %s: rc=%i\n",
			   QETH_CARD_IFNAME(card), rc);
	} else {
		PRINT_INFO("Multicast enabled\n");
		card->dev->flags |= IFF_MULTICAST;
	}
	return rc;
}

#ifdef CONFIG_QETH_IPV6
static int
qeth_softsetup_ipv6(struct qeth_card *card)
{
	int rc;

	QETH_DBF_TEXT(trace,3,"softipv6");

	netif_stop_queue(card->dev);
	rc = qeth_send_startlan(card, QETH_PROT_IPV6);
	if (rc) {
		PRINT_ERR("IPv6 startlan failed on %s\n",
			  QETH_CARD_IFNAME(card));
		return rc;
	}
	netif_wake_queue(card->dev);
	rc = qeth_query_ipassists(card,QETH_PROT_IPV6);
	if (rc) {
		PRINT_ERR("IPv6 query ipassist failed on %s\n",
			  QETH_CARD_IFNAME(card));
		return rc;
	}
	rc = qeth_send_simple_setassparms(card, IPA_IPV6,
					  IPA_CMD_ASS_START, 3);
	if (rc) {
		PRINT_WARN("IPv6 start assist (version 4) failed "
			   "on %s: 0x%x\n",
			   QETH_CARD_IFNAME(card), rc);
		return rc;
	}
	rc = qeth_send_simple_setassparms_ipv6(card, IPA_IPV6,
					       IPA_CMD_ASS_START);
	if (rc) {
		PRINT_WARN("IPV6 start assist (version 6) failed  "
			   "on %s: 0x%x\n",
			   QETH_CARD_IFNAME(card), rc);
		return rc;
	}
	rc = qeth_send_simple_setassparms_ipv6(card, IPA_PASSTHRU,
					       IPA_CMD_ASS_START);
	if (rc) {
		PRINT_WARN("Could not enable passthrough "
			   "on %s: 0x%x\n",
			   QETH_CARD_IFNAME(card), rc);
		return rc;
	}
	PRINT_INFO("IPV6 enabled \n");
	return 0;
}

#endif

static int
qeth_start_ipa_ipv6(struct qeth_card *card)
{
	int rc = 0;
#ifdef CONFIG_QETH_IPV6
	QETH_DBF_TEXT(trace,3,"strtipv6");

	if (!qeth_is_supported(card, IPA_IPV6)) {
		PRINT_WARN("IPv6 not supported on %s\n",
			   QETH_CARD_IFNAME(card));
		return 0;
	}
	rc = qeth_softsetup_ipv6(card);
#endif
	return rc ;
}

static int
qeth_start_ipa_broadcast(struct qeth_card *card)
{
	int rc;

	QETH_DBF_TEXT(trace,3,"stbrdcst");
	card->info.broadcast_capable = 0;
	if (!qeth_is_supported(card, IPA_FILTERING)) {
		PRINT_WARN("Broadcast not supported on %s\n",
			   QETH_CARD_IFNAME(card));
		rc = -EOPNOTSUPP;
		goto out;
	}
	rc = qeth_send_simple_setassparms(card, IPA_FILTERING,
					  IPA_CMD_ASS_START, 0);
	if (rc) {
		PRINT_WARN("Could not enable broadcasting filtering "
			   "on %s: 0x%x\n",
			   QETH_CARD_IFNAME(card), rc);
		goto out;
	}

	rc = qeth_send_simple_setassparms(card, IPA_FILTERING,
					  IPA_CMD_ASS_CONFIGURE, 1);
	if (rc) {
		PRINT_WARN("Could not set up broadcast filtering on %s: 0x%x\n",
			   QETH_CARD_IFNAME(card), rc);
		goto out;
	}
	card->info.broadcast_capable = QETH_BROADCAST_WITH_ECHO;
	PRINT_INFO("Broadcast enabled \n");
	rc = qeth_send_simple_setassparms(card, IPA_FILTERING,
					  IPA_CMD_ASS_ENABLE, 1);
	if (rc) {
		PRINT_WARN("Could not set up broadcast echo filtering on "
			   "%s: 0x%x\n", QETH_CARD_IFNAME(card), rc);
		goto out;
	}
	card->info.broadcast_capable = QETH_BROADCAST_WITHOUT_ECHO;
out:
	if (card->info.broadcast_capable)
		card->dev->flags |= IFF_BROADCAST;
	else
		card->dev->flags &= ~IFF_BROADCAST;
	return rc;
}

static int
qeth_send_checksum_command(struct qeth_card *card)
{
	int rc;

	rc = qeth_send_simple_setassparms(card, IPA_INBOUND_CHECKSUM,
					  IPA_CMD_ASS_START, 0);
	if (rc) {
		PRINT_WARN("Starting Inbound HW Checksumming failed on %s: "
			   "0x%x,\ncontinuing using Inbound SW Checksumming\n",
			   QETH_CARD_IFNAME(card), rc);
		return rc;
	}
	rc = qeth_send_simple_setassparms(card, IPA_INBOUND_CHECKSUM,
					  IPA_CMD_ASS_ENABLE,
					  card->info.csum_mask);
	if (rc) {
		PRINT_WARN("Enabling Inbound HW Checksumming failed on %s: "
			   "0x%x,\ncontinuing using Inbound SW Checksumming\n",
			   QETH_CARD_IFNAME(card), rc);
		return rc;
	}
	return 0;
}

static int
qeth_start_ipa_checksum(struct qeth_card *card)
{
	int rc = 0;

	QETH_DBF_TEXT(trace,3,"strtcsum");

	if (card->options.checksum_type == NO_CHECKSUMMING) {
		PRINT_WARN("Using no checksumming on %s.\n",
			   QETH_CARD_IFNAME(card));
		return 0;
	}
	if (card->options.checksum_type == SW_CHECKSUMMING) {
		PRINT_WARN("Using SW checksumming on %s.\n",
			   QETH_CARD_IFNAME(card));
		return 0;
	}
	if (!qeth_is_supported(card, IPA_INBOUND_CHECKSUM)) {
		PRINT_WARN("Inbound HW Checksumming not "
			   "supported on %s,\ncontinuing "
			   "using Inbound SW Checksumming\n",
			   QETH_CARD_IFNAME(card));
		card->options.checksum_type = SW_CHECKSUMMING;
		return 0;
	}
	rc = qeth_send_checksum_command(card);
	if (!rc) {
		PRINT_INFO("HW Checksumming (inbound) enabled \n");
	}
	return rc;
}

static int
qeth_start_ipa_tso(struct qeth_card *card)
{
	int rc;

	QETH_DBF_TEXT(trace,3,"sttso");

	if (!qeth_is_supported(card, IPA_OUTBOUND_TSO)) {
		PRINT_WARN("Outbound TSO not supported on %s\n",
			   QETH_CARD_IFNAME(card));
		rc = -EOPNOTSUPP;
	} else {
		rc = qeth_send_simple_setassparms(card, IPA_OUTBOUND_TSO,
						  IPA_CMD_ASS_START,0);
		if (rc)
			PRINT_WARN("Could not start outbound TSO "
				   "assist on %s: rc=%i\n",
				   QETH_CARD_IFNAME(card), rc);
		else
			PRINT_INFO("Outbound TSO enabled\n");
	}
	if (rc && (card->options.large_send == QETH_LARGE_SEND_TSO)){
		card->options.large_send = QETH_LARGE_SEND_NO;
		card->dev->features &= ~ (NETIF_F_TSO | NETIF_F_SG);
	}
	return rc;
}

static int
qeth_start_ipassists(struct qeth_card *card)
{
	QETH_DBF_TEXT(trace,3,"strtipas");
	qeth_start_ipa_arp_processing(card);	/* go on*/
	qeth_start_ipa_ip_fragmentation(card); 	/* go on*/
	qeth_start_ipa_source_mac(card);	/* go on*/
	qeth_start_ipa_vlan(card);		/* go on*/
	qeth_start_ipa_multicast(card);		/* go on*/
	qeth_start_ipa_ipv6(card);		/* go on*/
	qeth_start_ipa_broadcast(card);		/* go on*/
	qeth_start_ipa_checksum(card);		/* go on*/
	qeth_start_ipa_tso(card);		/* go on*/
	return 0;
}

static int
qeth_send_setrouting(struct qeth_card *card, enum qeth_routing_types type,
		     enum qeth_prot_versions prot)
{
	int rc;
	struct qeth_ipa_cmd *cmd;
	struct qeth_cmd_buffer *iob;

	QETH_DBF_TEXT(trace,4,"setroutg");
	iob = qeth_get_ipacmd_buffer(card, IPA_CMD_SETRTG, prot);
	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
	cmd->data.setrtg.type = (type);
	rc = qeth_send_ipa_cmd(card, iob, NULL, NULL);

	return rc;

}

static void
qeth_correct_routing_type(struct qeth_card *card, enum qeth_routing_types *type,
			enum qeth_prot_versions prot)
{
	if (card->info.type == QETH_CARD_TYPE_IQD) {
		switch (*type) {
		case NO_ROUTER:
		case PRIMARY_CONNECTOR:
		case SECONDARY_CONNECTOR:
		case MULTICAST_ROUTER:
			return;
		default:
			goto out_inval;
		}
	} else {
		switch (*type) {
		case NO_ROUTER:
		case PRIMARY_ROUTER:
		case SECONDARY_ROUTER:
			return;
		case MULTICAST_ROUTER:
			if (qeth_is_ipafunc_supported(card, prot,
						      IPA_OSA_MC_ROUTER))
				return;
		default:
			goto out_inval;
		}
	}
out_inval:
	PRINT_WARN("Routing type '%s' not supported for interface %s.\n"
		   "Router status set to 'no router'.\n",
		   ((*type == PRIMARY_ROUTER)? "primary router" :
		    (*type == SECONDARY_ROUTER)? "secondary router" :
		    (*type == PRIMARY_CONNECTOR)? "primary connector" :
		    (*type == SECONDARY_CONNECTOR)? "secondary connector" :
		    (*type == MULTICAST_ROUTER)? "multicast router" :
		    "unknown"),
		   card->dev->name);
	*type = NO_ROUTER;
}

int
qeth_setrouting_v4(struct qeth_card *card)
{
	int rc;

	QETH_DBF_TEXT(trace,3,"setrtg4");

	qeth_correct_routing_type(card, &card->options.route4.type,
				  QETH_PROT_IPV4);

	rc = qeth_send_setrouting(card, card->options.route4.type,
				  QETH_PROT_IPV4);
	if (rc) {
 		card->options.route4.type = NO_ROUTER;
		PRINT_WARN("Error (0x%04x) while setting routing type on %s. "
			   "Type set to 'no router'.\n",
			   rc, QETH_CARD_IFNAME(card));
	}
	return rc;
}

int
qeth_setrouting_v6(struct qeth_card *card)
{
	int rc = 0;

	QETH_DBF_TEXT(trace,3,"setrtg6");
#ifdef CONFIG_QETH_IPV6

	qeth_correct_routing_type(card, &card->options.route6.type,
				  QETH_PROT_IPV6);

	if ((card->options.route6.type == NO_ROUTER) ||
	    ((card->info.type == QETH_CARD_TYPE_OSAE) &&
	     (card->options.route6.type == MULTICAST_ROUTER) &&
	     !qeth_is_supported6(card,IPA_OSA_MC_ROUTER)))
		return 0;
	rc = qeth_send_setrouting(card, card->options.route6.type,
				  QETH_PROT_IPV6);
	if (rc) {
	 	card->options.route6.type = NO_ROUTER;
		PRINT_WARN("Error (0x%04x) while setting routing type on %s. "
			   "Type set to 'no router'.\n",
			   rc, QETH_CARD_IFNAME(card));
	}
#endif
	return rc;
}

int
qeth_set_large_send(struct qeth_card *card, enum qeth_large_send_types type)
{
	int rc = 0;

	if (card->dev == NULL) {
		card->options.large_send = type;
		return 0;
	}
	netif_stop_queue(card->dev);
	card->options.large_send = type;
	switch (card->options.large_send) {
	case QETH_LARGE_SEND_EDDP:
		card->dev->features |= NETIF_F_TSO | NETIF_F_SG;
		break;
	case QETH_LARGE_SEND_TSO:
		if (qeth_is_supported(card, IPA_OUTBOUND_TSO)){
			card->dev->features |= NETIF_F_TSO | NETIF_F_SG;
		} else {
			PRINT_WARN("TSO not supported on %s. "
				   "large_send set to 'no'.\n",
				   card->dev->name);
			card->dev->features &= ~(NETIF_F_TSO | NETIF_F_SG);
			card->options.large_send = QETH_LARGE_SEND_NO;
			rc = -EOPNOTSUPP;
		}
		break;
	default: /* includes QETH_LARGE_SEND_NO */
		card->dev->features &= ~(NETIF_F_TSO | NETIF_F_SG);
		break;
	}
	netif_wake_queue(card->dev);
	return rc;
}

/*
 * softsetup card: init IPA stuff
 */
static int
qeth_softsetup_card(struct qeth_card *card)
{
	int rc;

	QETH_DBF_TEXT(setup, 2, "softsetp");

	if ((rc = qeth_send_startlan(card, QETH_PROT_IPV4))){
		QETH_DBF_TEXT_(setup, 2, "1err%d", rc);
		if (rc == 0xe080){
			PRINT_WARN("LAN on card %s if offline! "
				   "Continuing softsetup.\n",
				   CARD_BUS_ID(card));
			card->lan_online = 0;
		} else
			return rc;
	} else
		card->lan_online = 1;
	if (card->info.type==QETH_CARD_TYPE_OSN)
		goto out;
	if (card->options.layer2) {
		card->dev->features |=
			NETIF_F_HW_VLAN_FILTER |
			NETIF_F_HW_VLAN_TX |
			NETIF_F_HW_VLAN_RX;
		card->dev->flags|=IFF_MULTICAST|IFF_BROADCAST;
		card->info.broadcast_capable=1;
		if ((rc = qeth_layer2_initialize(card))) {
			QETH_DBF_TEXT_(setup, 2, "L2err%d", rc);
			return rc;
		}
#ifdef CONFIG_QETH_VLAN
		qeth_layer2_process_vlans(card, 0);
#endif
		goto out;
	}
	if ((card->options.large_send == QETH_LARGE_SEND_EDDP) ||
	    (card->options.large_send == QETH_LARGE_SEND_TSO))
		card->dev->features |= NETIF_F_TSO | NETIF_F_SG;
	else
		card->dev->features &= ~(NETIF_F_TSO | NETIF_F_SG);

	if ((rc = qeth_setadapter_parms(card)))
		QETH_DBF_TEXT_(setup, 2, "2err%d", rc);
	if ((rc = qeth_start_ipassists(card)))
		QETH_DBF_TEXT_(setup, 2, "3err%d", rc);
	if ((rc = qeth_setrouting_v4(card)))
		QETH_DBF_TEXT_(setup, 2, "4err%d", rc);
	if ((rc = qeth_setrouting_v6(card)))
		QETH_DBF_TEXT_(setup, 2, "5err%d", rc);
out:
	netif_stop_queue(card->dev);
	return 0;
}

#ifdef CONFIG_QETH_IPV6
static int
qeth_get_unique_id_cb(struct qeth_card *card, struct qeth_reply *reply,
		      unsigned long data)
{
	struct qeth_ipa_cmd *cmd;

	cmd = (struct qeth_ipa_cmd *) data;
	if (cmd->hdr.return_code == 0)
		card->info.unique_id = *((__u16 *)
				&cmd->data.create_destroy_addr.unique_id[6]);
	else {
		card->info.unique_id =  UNIQUE_ID_IF_CREATE_ADDR_FAILED |
					UNIQUE_ID_NOT_BY_CARD;
		PRINT_WARN("couldn't get a unique id from the card on device "
			   "%s (result=x%x), using default id. ipv6 "
			   "autoconfig on other lpars may lead to duplicate "
			   "ip addresses. please use manually "
			   "configured ones.\n",
			   CARD_BUS_ID(card), cmd->hdr.return_code);
	}
	return 0;
}
#endif

static int
qeth_put_unique_id(struct qeth_card *card)
{

	int rc = 0;
#ifdef CONFIG_QETH_IPV6
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(trace,2,"puniqeid");

	if ((card->info.unique_id & UNIQUE_ID_NOT_BY_CARD) ==
	    	UNIQUE_ID_NOT_BY_CARD)
		return -1;
	iob = qeth_get_ipacmd_buffer(card, IPA_CMD_DESTROY_ADDR,
				     QETH_PROT_IPV6);
	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
	*((__u16 *) &cmd->data.create_destroy_addr.unique_id[6]) =
		            card->info.unique_id;
	memcpy(&cmd->data.create_destroy_addr.unique_id[0],
	       card->dev->dev_addr, OSA_ADDR_LEN);
	rc = qeth_send_ipa_cmd(card, iob, NULL, NULL);
#else
	card->info.unique_id =  UNIQUE_ID_IF_CREATE_ADDR_FAILED |
				UNIQUE_ID_NOT_BY_CARD;
#endif
	return rc;
}

/**
 * Clear IP List
 */
static void
qeth_clear_ip_list(struct qeth_card *card, int clean, int recover)
{
	struct qeth_ipaddr *addr, *tmp;
	unsigned long flags;

	QETH_DBF_TEXT(trace,4,"clearip");
	spin_lock_irqsave(&card->ip_lock, flags);
	/* clear todo list */
	list_for_each_entry_safe(addr, tmp, card->ip_tbd_list, entry){
		list_del(&addr->entry);
		kfree(addr);
	}

	while (!list_empty(&card->ip_list)) {
		addr = list_entry(card->ip_list.next,
				  struct qeth_ipaddr, entry);
		list_del_init(&addr->entry);
		if (clean) {
			spin_unlock_irqrestore(&card->ip_lock, flags);
			qeth_deregister_addr_entry(card, addr);
			spin_lock_irqsave(&card->ip_lock, flags);
		}
		if (!recover || addr->is_multicast) {
			kfree(addr);
			continue;
		}
		list_add_tail(&addr->entry, card->ip_tbd_list);
	}
	spin_unlock_irqrestore(&card->ip_lock, flags);
}

static void
qeth_set_allowed_threads(struct qeth_card *card, unsigned long threads,
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

static inline int
qeth_threads_running(struct qeth_card *card, unsigned long threads)
{
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&card->thread_mask_lock, flags);
	rc = (card->thread_running_mask & threads);
	spin_unlock_irqrestore(&card->thread_mask_lock, flags);
	return rc;
}

static int
qeth_wait_for_threads(struct qeth_card *card, unsigned long threads)
{
	return wait_event_interruptible(card->wait_q,
			qeth_threads_running(card, threads) == 0);
}

static int
qeth_stop_card(struct qeth_card *card, int recovery_mode)
{
	int rc = 0;

	QETH_DBF_TEXT(setup ,2,"stopcard");
	QETH_DBF_HEX(setup, 2, &card, sizeof(void *));

	qeth_set_allowed_threads(card, 0, 1);
	if (qeth_wait_for_threads(card, ~QETH_RECOVER_THREAD))
		return -ERESTARTSYS;
	if (card->read.state == CH_STATE_UP &&
	    card->write.state == CH_STATE_UP &&
	    (card->state == CARD_STATE_UP)) {
		if (recovery_mode && 
		    card->info.type != QETH_CARD_TYPE_OSN) {
			qeth_stop(card->dev);
		} else {
			rtnl_lock();
			dev_close(card->dev);
			rtnl_unlock();
		}
		if (!card->use_hard_stop) {
			__u8 *mac = &card->dev->dev_addr[0];
			rc = qeth_layer2_send_delmac(card, mac);
			QETH_DBF_TEXT_(setup, 2, "Lerr%d", rc);
			if ((rc = qeth_send_stoplan(card)))
				QETH_DBF_TEXT_(setup, 2, "1err%d", rc);
		}
		card->state = CARD_STATE_SOFTSETUP;
	}
	if (card->state == CARD_STATE_SOFTSETUP) {
#ifdef CONFIG_QETH_VLAN
		if (card->options.layer2)
			qeth_layer2_process_vlans(card, 1);
#endif
		qeth_clear_ip_list(card, !card->use_hard_stop, 1);
		qeth_clear_ipacmd_list(card);
		card->state = CARD_STATE_HARDSETUP;
	}
	if (card->state == CARD_STATE_HARDSETUP) {
		if ((!card->use_hard_stop) &&
		    (!card->options.layer2))
			if ((rc = qeth_put_unique_id(card)))
				QETH_DBF_TEXT_(setup, 2, "2err%d", rc);
		qeth_qdio_clear_card(card, 0);
		qeth_clear_qdio_buffers(card);
		qeth_clear_working_pool_list(card);
		card->state = CARD_STATE_DOWN;
	}
	if (card->state == CARD_STATE_DOWN) {
		qeth_clear_cmd_buffers(&card->read);
		qeth_clear_cmd_buffers(&card->write);
	}
	card->use_hard_stop = 0;
	return rc;
}


static int
qeth_get_unique_id(struct qeth_card *card)
{
	int rc = 0;
#ifdef CONFIG_QETH_IPV6
	struct qeth_cmd_buffer *iob;
	struct qeth_ipa_cmd *cmd;

	QETH_DBF_TEXT(setup, 2, "guniqeid");

	if (!qeth_is_supported(card,IPA_IPV6)) {
		card->info.unique_id =  UNIQUE_ID_IF_CREATE_ADDR_FAILED |
					UNIQUE_ID_NOT_BY_CARD;
		return 0;
	}

	iob = qeth_get_ipacmd_buffer(card, IPA_CMD_CREATE_ADDR,
				     QETH_PROT_IPV6);
	cmd = (struct qeth_ipa_cmd *)(iob->data+IPA_PDU_HEADER_SIZE);
	*((__u16 *) &cmd->data.create_destroy_addr.unique_id[6]) =
		            card->info.unique_id;

	rc = qeth_send_ipa_cmd(card, iob, qeth_get_unique_id_cb, NULL);
#else
	card->info.unique_id =  UNIQUE_ID_IF_CREATE_ADDR_FAILED |
				UNIQUE_ID_NOT_BY_CARD;
#endif
	return rc;
}
static void
qeth_print_status_with_portname(struct qeth_card *card)
{
	char dbf_text[15];
	int i;

	sprintf(dbf_text, "%s", card->info.portname + 1);
	for (i = 0; i < 8; i++)
		dbf_text[i] =
			(char) _ebcasc[(__u8) dbf_text[i]];
	dbf_text[8] = 0;
	printk("qeth: Device %s/%s/%s is a%s card%s%s%s\n"
	       "with link type %s (portname: %s)\n",
	       CARD_RDEV_ID(card),
	       CARD_WDEV_ID(card),
	       CARD_DDEV_ID(card),
	       qeth_get_cardname(card),
	       (card->info.mcl_level[0]) ? " (level: " : "",
	       (card->info.mcl_level[0]) ? card->info.mcl_level : "",
	       (card->info.mcl_level[0]) ? ")" : "",
	       qeth_get_cardname_short(card),
	       dbf_text);

}

static void
qeth_print_status_no_portname(struct qeth_card *card)
{
	if (card->info.portname[0])
		printk("qeth: Device %s/%s/%s is a%s "
		       "card%s%s%s\nwith link type %s "
		       "(no portname needed by interface).\n",
		       CARD_RDEV_ID(card),
		       CARD_WDEV_ID(card),
		       CARD_DDEV_ID(card),
		       qeth_get_cardname(card),
		       (card->info.mcl_level[0]) ? " (level: " : "",
		       (card->info.mcl_level[0]) ? card->info.mcl_level : "",
		       (card->info.mcl_level[0]) ? ")" : "",
		       qeth_get_cardname_short(card));
	else
		printk("qeth: Device %s/%s/%s is a%s "
		       "card%s%s%s\nwith link type %s.\n",
		       CARD_RDEV_ID(card),
		       CARD_WDEV_ID(card),
		       CARD_DDEV_ID(card),
		       qeth_get_cardname(card),
		       (card->info.mcl_level[0]) ? " (level: " : "",
		       (card->info.mcl_level[0]) ? card->info.mcl_level : "",
		       (card->info.mcl_level[0]) ? ")" : "",
		       qeth_get_cardname_short(card));
}

static void
qeth_print_status_message(struct qeth_card *card)
{
	switch (card->info.type) {
	case QETH_CARD_TYPE_OSAE:
		/* VM will use a non-zero first character
		 * to indicate a HiperSockets like reporting
		 * of the level OSA sets the first character to zero
		 * */
		if (!card->info.mcl_level[0]) {
			sprintf(card->info.mcl_level,"%02x%02x",
				card->info.mcl_level[2],
				card->info.mcl_level[3]);

			card->info.mcl_level[QETH_MCL_LENGTH] = 0;
			break;
		}
		/* fallthrough */
	case QETH_CARD_TYPE_IQD:
		card->info.mcl_level[0] = (char) _ebcasc[(__u8)
			card->info.mcl_level[0]];
		card->info.mcl_level[1] = (char) _ebcasc[(__u8)
			card->info.mcl_level[1]];
		card->info.mcl_level[2] = (char) _ebcasc[(__u8)
			card->info.mcl_level[2]];
		card->info.mcl_level[3] = (char) _ebcasc[(__u8)
			card->info.mcl_level[3]];
		card->info.mcl_level[QETH_MCL_LENGTH] = 0;
		break;
	default:
		memset(&card->info.mcl_level[0], 0, QETH_MCL_LENGTH + 1);
	}
	if (card->info.portname_required)
		qeth_print_status_with_portname(card);
	else
		qeth_print_status_no_portname(card);
}

static int
qeth_register_netdev(struct qeth_card *card)
{
	QETH_DBF_TEXT(setup, 3, "regnetd");
	if (card->dev->reg_state != NETREG_UNINITIALIZED) {
		qeth_netdev_init(card->dev);
		return 0;
	}
	/* sysfs magic */
	SET_NETDEV_DEV(card->dev, &card->gdev->dev);
	return register_netdev(card->dev);
}

static void
qeth_start_again(struct qeth_card *card, int recovery_mode)
{
	QETH_DBF_TEXT(setup ,2, "startag");

	if (recovery_mode && 
	    card->info.type != QETH_CARD_TYPE_OSN) {
		qeth_open(card->dev);
	} else {
		rtnl_lock();
		dev_open(card->dev);
		rtnl_unlock();
	}
	/* this also sets saved unicast addresses */
	qeth_set_multicast_list(card->dev);
}


/* Layer 2 specific stuff */
#define IGNORE_PARAM_EQ(option,value,reset_value,msg) \
        if (card->options.option == value) { \
                PRINT_ERR("%s not supported with layer 2 " \
                          "functionality, ignoring option on read" \
			  "channel device %s .\n",msg,CARD_RDEV_ID(card)); \
                card->options.option = reset_value; \
        }
#define IGNORE_PARAM_NEQ(option,value,reset_value,msg) \
        if (card->options.option != value) { \
                PRINT_ERR("%s not supported with layer 2 " \
                          "functionality, ignoring option on read" \
			  "channel device %s .\n",msg,CARD_RDEV_ID(card)); \
                card->options.option = reset_value; \
        }


static void qeth_make_parameters_consistent(struct qeth_card *card)
{

	if (card->options.layer2 == 0)
		return;
	if (card->info.type == QETH_CARD_TYPE_OSN)
		return;
	if (card->info.type == QETH_CARD_TYPE_IQD) {
       		PRINT_ERR("Device %s does not support layer 2 functionality." \
	               	  " Ignoring layer2 option.\n",CARD_BUS_ID(card));
       		card->options.layer2 = 0;
		return;
	}
       	IGNORE_PARAM_NEQ(route4.type, NO_ROUTER, NO_ROUTER,
               	         "Routing options are");
#ifdef CONFIG_QETH_IPV6
       	IGNORE_PARAM_NEQ(route6.type, NO_ROUTER, NO_ROUTER,
               	         "Routing options are");
#endif
       	IGNORE_PARAM_EQ(checksum_type, HW_CHECKSUMMING,
                       	QETH_CHECKSUM_DEFAULT,
               	        "Checksumming options are");
       	IGNORE_PARAM_NEQ(broadcast_mode, QETH_TR_BROADCAST_ALLRINGS,
                       	 QETH_TR_BROADCAST_ALLRINGS,
               	         "Broadcast mode options are");
       	IGNORE_PARAM_NEQ(macaddr_mode, QETH_TR_MACADDR_NONCANONICAL,
                       	 QETH_TR_MACADDR_NONCANONICAL,
               	         "Canonical MAC addr options are");
       	IGNORE_PARAM_NEQ(fake_broadcast, 0, 0,
			 "Broadcast faking options are");
       	IGNORE_PARAM_NEQ(add_hhlen, DEFAULT_ADD_HHLEN,
       	                 DEFAULT_ADD_HHLEN,"Option add_hhlen is");
        IGNORE_PARAM_NEQ(fake_ll, 0, 0,"Option fake_ll is");
}


static int
__qeth_set_online(struct ccwgroup_device *gdev, int recovery_mode)
{
	struct qeth_card *card = gdev->dev.driver_data;
	int rc = 0;
	enum qeth_card_states recover_flag;

	BUG_ON(!card);
	QETH_DBF_TEXT(setup ,2, "setonlin");
	QETH_DBF_HEX(setup, 2, &card, sizeof(void *));

	qeth_set_allowed_threads(card, QETH_RECOVER_THREAD, 1);
	if (qeth_wait_for_threads(card, ~QETH_RECOVER_THREAD)){
		PRINT_WARN("set_online of card %s interrupted by user!\n",
			   CARD_BUS_ID(card));
		return -ERESTARTSYS;
	}

	recover_flag = card->state;
	if ((rc = ccw_device_set_online(CARD_RDEV(card))) ||
	    (rc = ccw_device_set_online(CARD_WDEV(card))) ||
	    (rc = ccw_device_set_online(CARD_DDEV(card)))){
		QETH_DBF_TEXT_(setup, 2, "1err%d", rc);
		return -EIO;
	}

	qeth_make_parameters_consistent(card);

	if ((rc = qeth_hardsetup_card(card))){
		QETH_DBF_TEXT_(setup, 2, "2err%d", rc);
		goto out_remove;
	}
	card->state = CARD_STATE_HARDSETUP;

	if (!(rc = qeth_query_ipassists(card,QETH_PROT_IPV4)))
		rc = qeth_get_unique_id(card);

	if (rc && card->options.layer2 == 0) {
		QETH_DBF_TEXT_(setup, 2, "3err%d", rc);
		goto out_remove;
	}
	qeth_print_status_message(card);
	if ((rc = qeth_register_netdev(card))){
		QETH_DBF_TEXT_(setup, 2, "4err%d", rc);
		goto out_remove;
	}
	if ((rc = qeth_softsetup_card(card))){
		QETH_DBF_TEXT_(setup, 2, "5err%d", rc);
		goto out_remove;
	}
	card->state = CARD_STATE_SOFTSETUP;

	if ((rc = qeth_init_qdio_queues(card))){
		QETH_DBF_TEXT_(setup, 2, "6err%d", rc);
		goto out_remove;
	}
	netif_carrier_on(card->dev);

	qeth_set_allowed_threads(card, 0xffffffff, 0);
	if (recover_flag == CARD_STATE_RECOVER)
		qeth_start_again(card, recovery_mode);
	qeth_notify_processes();
	return 0;
out_remove:
	card->use_hard_stop = 1;
	qeth_stop_card(card, 0);
	ccw_device_set_offline(CARD_DDEV(card));
	ccw_device_set_offline(CARD_WDEV(card));
	ccw_device_set_offline(CARD_RDEV(card));
	if (recover_flag == CARD_STATE_RECOVER)
		card->state = CARD_STATE_RECOVER;
	else
		card->state = CARD_STATE_DOWN;
	return -ENODEV;
}

static int
qeth_set_online(struct ccwgroup_device *gdev)
{
	return __qeth_set_online(gdev, 0);
}

static struct ccw_device_id qeth_ids[] = {
	{CCW_DEVICE(0x1731, 0x01), driver_info:QETH_CARD_TYPE_OSAE},
	{CCW_DEVICE(0x1731, 0x05), driver_info:QETH_CARD_TYPE_IQD},
	{CCW_DEVICE(0x1731, 0x06), driver_info:QETH_CARD_TYPE_OSN},
	{},
};
MODULE_DEVICE_TABLE(ccw, qeth_ids);

struct device *qeth_root_dev = NULL;

struct ccwgroup_driver qeth_ccwgroup_driver = {
	.owner = THIS_MODULE,
	.name = "qeth",
	.driver_id = 0xD8C5E3C8,
	.probe = qeth_probe_device,
	.remove = qeth_remove_device,
	.set_online = qeth_set_online,
	.set_offline = qeth_set_offline,
};

struct ccw_driver qeth_ccw_driver = {
	.name = "qeth",
	.ids = qeth_ids,
	.probe = ccwgroup_probe_ccwdev,
	.remove = ccwgroup_remove_ccwdev,
};


static void
qeth_unregister_dbf_views(void)
{
	if (qeth_dbf_setup)
		debug_unregister(qeth_dbf_setup);
	if (qeth_dbf_qerr)
		debug_unregister(qeth_dbf_qerr);
	if (qeth_dbf_sense)
		debug_unregister(qeth_dbf_sense);
	if (qeth_dbf_misc)
		debug_unregister(qeth_dbf_misc);
	if (qeth_dbf_data)
		debug_unregister(qeth_dbf_data);
	if (qeth_dbf_control)
		debug_unregister(qeth_dbf_control);
	if (qeth_dbf_trace)
		debug_unregister(qeth_dbf_trace);
}
static int
qeth_register_dbf_views(void)
{
	qeth_dbf_setup = debug_register(QETH_DBF_SETUP_NAME,
					QETH_DBF_SETUP_PAGES,
					QETH_DBF_SETUP_NR_AREAS,
					QETH_DBF_SETUP_LEN);
	qeth_dbf_misc = debug_register(QETH_DBF_MISC_NAME,
				       QETH_DBF_MISC_PAGES,
				       QETH_DBF_MISC_NR_AREAS,
				       QETH_DBF_MISC_LEN);
	qeth_dbf_data = debug_register(QETH_DBF_DATA_NAME,
				       QETH_DBF_DATA_PAGES,
				       QETH_DBF_DATA_NR_AREAS,
				       QETH_DBF_DATA_LEN);
	qeth_dbf_control = debug_register(QETH_DBF_CONTROL_NAME,
					  QETH_DBF_CONTROL_PAGES,
					  QETH_DBF_CONTROL_NR_AREAS,
					  QETH_DBF_CONTROL_LEN);
	qeth_dbf_sense = debug_register(QETH_DBF_SENSE_NAME,
					QETH_DBF_SENSE_PAGES,
					QETH_DBF_SENSE_NR_AREAS,
					QETH_DBF_SENSE_LEN);
	qeth_dbf_qerr = debug_register(QETH_DBF_QERR_NAME,
				       QETH_DBF_QERR_PAGES,
				       QETH_DBF_QERR_NR_AREAS,
				       QETH_DBF_QERR_LEN);
	qeth_dbf_trace = debug_register(QETH_DBF_TRACE_NAME,
					QETH_DBF_TRACE_PAGES,
					QETH_DBF_TRACE_NR_AREAS,
					QETH_DBF_TRACE_LEN);

	if ((qeth_dbf_setup == NULL) || (qeth_dbf_misc == NULL)    ||
	    (qeth_dbf_data == NULL)  || (qeth_dbf_control == NULL) ||
	    (qeth_dbf_sense == NULL) || (qeth_dbf_qerr == NULL)    ||
	    (qeth_dbf_trace == NULL)) {
		qeth_unregister_dbf_views();
		return -ENOMEM;
	}
	debug_register_view(qeth_dbf_setup, &debug_hex_ascii_view);
	debug_set_level(qeth_dbf_setup, QETH_DBF_SETUP_LEVEL);

	debug_register_view(qeth_dbf_misc, &debug_hex_ascii_view);
	debug_set_level(qeth_dbf_misc, QETH_DBF_MISC_LEVEL);

	debug_register_view(qeth_dbf_data, &debug_hex_ascii_view);
	debug_set_level(qeth_dbf_data, QETH_DBF_DATA_LEVEL);

	debug_register_view(qeth_dbf_control, &debug_hex_ascii_view);
	debug_set_level(qeth_dbf_control, QETH_DBF_CONTROL_LEVEL);

	debug_register_view(qeth_dbf_sense, &debug_hex_ascii_view);
	debug_set_level(qeth_dbf_sense, QETH_DBF_SENSE_LEVEL);

	debug_register_view(qeth_dbf_qerr, &debug_hex_ascii_view);
	debug_set_level(qeth_dbf_qerr, QETH_DBF_QERR_LEVEL);

	debug_register_view(qeth_dbf_trace, &debug_hex_ascii_view);
	debug_set_level(qeth_dbf_trace, QETH_DBF_TRACE_LEVEL);

	return 0;
}

#ifdef CONFIG_QETH_IPV6
extern struct neigh_table arp_tbl;
static struct neigh_ops *arp_direct_ops;
static int (*qeth_old_arp_constructor) (struct neighbour *);

static struct neigh_ops arp_direct_ops_template = {
	.family = AF_INET,
	.destructor = NULL,
	.solicit = NULL,
	.error_report = NULL,
	.output = dev_queue_xmit,
	.connected_output = dev_queue_xmit,
	.hh_output = dev_queue_xmit,
	.queue_xmit = dev_queue_xmit
};

static int
qeth_arp_constructor(struct neighbour *neigh)
{
	struct net_device *dev = neigh->dev;
	struct in_device *in_dev;
	struct neigh_parms *parms;
	struct qeth_card *card;

	card = qeth_get_card_from_dev(dev);
	if (card == NULL)
		goto out;
	if((card->options.layer2) ||
	   (card->dev->hard_header == qeth_fake_header))
		goto out;

	rcu_read_lock();
	in_dev = __in_dev_get_rcu(dev);
	if (in_dev == NULL) {
		rcu_read_unlock();
		return -EINVAL;
	}

	parms = in_dev->arp_parms;
	__neigh_parms_put(neigh->parms);
	neigh->parms = neigh_parms_clone(parms);
	rcu_read_unlock();

	neigh->type = inet_addr_type(*(u32 *) neigh->primary_key);
	neigh->nud_state = NUD_NOARP;
	neigh->ops = arp_direct_ops;
	neigh->output = neigh->ops->queue_xmit;
	return 0;
out:
	return qeth_old_arp_constructor(neigh);
}
#endif  /*CONFIG_QETH_IPV6*/

/*
 * IP address takeover related functions
 */
static void
qeth_clear_ipato_list(struct qeth_card *card)
{
	struct qeth_ipato_entry *ipatoe, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&card->ip_lock, flags);
	list_for_each_entry_safe(ipatoe, tmp, &card->ipato.entries, entry) {
		list_del(&ipatoe->entry);
		kfree(ipatoe);
	}
	spin_unlock_irqrestore(&card->ip_lock, flags);
}

int
qeth_add_ipato_entry(struct qeth_card *card, struct qeth_ipato_entry *new)
{
	struct qeth_ipato_entry *ipatoe;
	unsigned long flags;
	int rc = 0;

	QETH_DBF_TEXT(trace, 2, "addipato");
	spin_lock_irqsave(&card->ip_lock, flags);
	list_for_each_entry(ipatoe, &card->ipato.entries, entry){
		if (ipatoe->proto != new->proto)
			continue;
		if (!memcmp(ipatoe->addr, new->addr,
			    (ipatoe->proto == QETH_PROT_IPV4)? 4:16) &&
		    (ipatoe->mask_bits == new->mask_bits)){
			PRINT_WARN("ipato entry already exists!\n");
			rc = -EEXIST;
			break;
		}
	}
	if (!rc) {
		list_add_tail(&new->entry, &card->ipato.entries);
	}
	spin_unlock_irqrestore(&card->ip_lock, flags);
	return rc;
}

void
qeth_del_ipato_entry(struct qeth_card *card, enum qeth_prot_versions proto,
		     u8 *addr, int mask_bits)
{
	struct qeth_ipato_entry *ipatoe, *tmp;
	unsigned long flags;

	QETH_DBF_TEXT(trace, 2, "delipato");
	spin_lock_irqsave(&card->ip_lock, flags);
	list_for_each_entry_safe(ipatoe, tmp, &card->ipato.entries, entry){
		if (ipatoe->proto != proto)
			continue;
		if (!memcmp(ipatoe->addr, addr,
			    (proto == QETH_PROT_IPV4)? 4:16) &&
		    (ipatoe->mask_bits == mask_bits)){
			list_del(&ipatoe->entry);
			kfree(ipatoe);
		}
	}
	spin_unlock_irqrestore(&card->ip_lock, flags);
}

static inline void
qeth_convert_addr_to_bits(u8 *addr, u8 *bits, int len)
{
	int i, j;
	u8 octet;

	for (i = 0; i < len; ++i){
		octet = addr[i];
		for (j = 7; j >= 0; --j){
			bits[i*8 + j] = octet & 1;
			octet >>= 1;
		}
	}
}

static int
qeth_is_addr_covered_by_ipato(struct qeth_card *card, struct qeth_ipaddr *addr)
{
	struct qeth_ipato_entry *ipatoe;
	u8 addr_bits[128] = {0, };
	u8 ipatoe_bits[128] = {0, };
	int rc = 0;

	if (!card->ipato.enabled)
		return 0;

	qeth_convert_addr_to_bits((u8 *) &addr->u, addr_bits,
				  (addr->proto == QETH_PROT_IPV4)? 4:16);
	list_for_each_entry(ipatoe, &card->ipato.entries, entry){
		if (addr->proto != ipatoe->proto)
			continue;
		qeth_convert_addr_to_bits(ipatoe->addr, ipatoe_bits,
					  (ipatoe->proto==QETH_PROT_IPV4) ?
					  4:16);
		if (addr->proto == QETH_PROT_IPV4)
			rc = !memcmp(addr_bits, ipatoe_bits,
				     min(32, ipatoe->mask_bits));
		else
			rc = !memcmp(addr_bits, ipatoe_bits,
				     min(128, ipatoe->mask_bits));
		if (rc)
			break;
	}
	/* invert? */
	if ((addr->proto == QETH_PROT_IPV4) && card->ipato.invert4)
		rc = !rc;
	else if ((addr->proto == QETH_PROT_IPV6) && card->ipato.invert6)
		rc = !rc;

	return rc;
}

/*
 * VIPA related functions
 */
int
qeth_add_vipa(struct qeth_card *card, enum qeth_prot_versions proto,
	      const u8 *addr)
{
	struct qeth_ipaddr *ipaddr;
	unsigned long flags;
	int rc = 0;

	ipaddr = qeth_get_addr_buffer(proto);
	if (ipaddr){
		if (proto == QETH_PROT_IPV4){
			QETH_DBF_TEXT(trace, 2, "addvipa4");
			memcpy(&ipaddr->u.a4.addr, addr, 4);
			ipaddr->u.a4.mask = 0;
#ifdef CONFIG_QETH_IPV6
		} else if (proto == QETH_PROT_IPV6){
			QETH_DBF_TEXT(trace, 2, "addvipa6");
			memcpy(&ipaddr->u.a6.addr, addr, 16);
			ipaddr->u.a6.pfxlen = 0;
#endif
		}
		ipaddr->type = QETH_IP_TYPE_VIPA;
		ipaddr->set_flags = QETH_IPA_SETIP_VIPA_FLAG;
		ipaddr->del_flags = QETH_IPA_DELIP_VIPA_FLAG;
	} else
		return -ENOMEM;
	spin_lock_irqsave(&card->ip_lock, flags);
	if (__qeth_address_exists_in_list(&card->ip_list, ipaddr, 0) ||
	    __qeth_address_exists_in_list(card->ip_tbd_list, ipaddr, 0))
		rc = -EEXIST;
	spin_unlock_irqrestore(&card->ip_lock, flags);
	if (rc){
		PRINT_WARN("Cannot add VIPA. Address already exists!\n");
		return rc;
	}
	if (!qeth_add_ip(card, ipaddr))
		kfree(ipaddr);
 	if (qeth_set_thread_start_bit(card, QETH_SET_IP_THREAD) == 0)
		schedule_work(&card->kernel_thread_starter);
	return rc;
}

void
qeth_del_vipa(struct qeth_card *card, enum qeth_prot_versions proto,
	      const u8 *addr)
{
	struct qeth_ipaddr *ipaddr;

	ipaddr = qeth_get_addr_buffer(proto);
	if (ipaddr){
		if (proto == QETH_PROT_IPV4){
			QETH_DBF_TEXT(trace, 2, "delvipa4");
			memcpy(&ipaddr->u.a4.addr, addr, 4);
			ipaddr->u.a4.mask = 0;
#ifdef CONFIG_QETH_IPV6
		} else if (proto == QETH_PROT_IPV6){
			QETH_DBF_TEXT(trace, 2, "delvipa6");
			memcpy(&ipaddr->u.a6.addr, addr, 16);
			ipaddr->u.a6.pfxlen = 0;
#endif
		}
		ipaddr->type = QETH_IP_TYPE_VIPA;
	} else
		return;
	if (!qeth_delete_ip(card, ipaddr))
		kfree(ipaddr);
 	if (qeth_set_thread_start_bit(card, QETH_SET_IP_THREAD) == 0)
		schedule_work(&card->kernel_thread_starter);
}

/*
 * proxy ARP related functions
 */
int
qeth_add_rxip(struct qeth_card *card, enum qeth_prot_versions proto,
	      const u8 *addr)
{
	struct qeth_ipaddr *ipaddr;
	unsigned long flags;
	int rc = 0;

	ipaddr = qeth_get_addr_buffer(proto);
	if (ipaddr){
		if (proto == QETH_PROT_IPV4){
			QETH_DBF_TEXT(trace, 2, "addrxip4");
			memcpy(&ipaddr->u.a4.addr, addr, 4);
			ipaddr->u.a4.mask = 0;
#ifdef CONFIG_QETH_IPV6
		} else if (proto == QETH_PROT_IPV6){
			QETH_DBF_TEXT(trace, 2, "addrxip6");
			memcpy(&ipaddr->u.a6.addr, addr, 16);
			ipaddr->u.a6.pfxlen = 0;
#endif
		}
		ipaddr->type = QETH_IP_TYPE_RXIP;
		ipaddr->set_flags = QETH_IPA_SETIP_TAKEOVER_FLAG;
		ipaddr->del_flags = 0;
	} else
		return -ENOMEM;
	spin_lock_irqsave(&card->ip_lock, flags);
	if (__qeth_address_exists_in_list(&card->ip_list, ipaddr, 0) ||
	    __qeth_address_exists_in_list(card->ip_tbd_list, ipaddr, 0))
		rc = -EEXIST;
	spin_unlock_irqrestore(&card->ip_lock, flags);
	if (rc){
		PRINT_WARN("Cannot add RXIP. Address already exists!\n");
		return rc;
	}
	if (!qeth_add_ip(card, ipaddr))
		kfree(ipaddr);
 	if (qeth_set_thread_start_bit(card, QETH_SET_IP_THREAD) == 0)
		schedule_work(&card->kernel_thread_starter);
	return 0;
}

void
qeth_del_rxip(struct qeth_card *card, enum qeth_prot_versions proto,
	      const u8 *addr)
{
	struct qeth_ipaddr *ipaddr;

	ipaddr = qeth_get_addr_buffer(proto);
	if (ipaddr){
		if (proto == QETH_PROT_IPV4){
			QETH_DBF_TEXT(trace, 2, "addrxip4");
			memcpy(&ipaddr->u.a4.addr, addr, 4);
			ipaddr->u.a4.mask = 0;
#ifdef CONFIG_QETH_IPV6
		} else if (proto == QETH_PROT_IPV6){
			QETH_DBF_TEXT(trace, 2, "addrxip6");
			memcpy(&ipaddr->u.a6.addr, addr, 16);
			ipaddr->u.a6.pfxlen = 0;
#endif
		}
		ipaddr->type = QETH_IP_TYPE_RXIP;
	} else
		return;
	if (!qeth_delete_ip(card, ipaddr))
		kfree(ipaddr);
 	if (qeth_set_thread_start_bit(card, QETH_SET_IP_THREAD) == 0)
		schedule_work(&card->kernel_thread_starter);
}

/**
 * IP event handler
 */
static int
qeth_ip_event(struct notifier_block *this,
	      unsigned long event,void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;
	struct net_device *dev =(struct net_device *) ifa->ifa_dev->dev;
	struct qeth_ipaddr *addr;
	struct qeth_card *card;

	QETH_DBF_TEXT(trace,3,"ipevent");
	card = qeth_get_card_from_dev(dev);
	if (!card)
		return NOTIFY_DONE;
	if (card->options.layer2)
		return NOTIFY_DONE;

	addr = qeth_get_addr_buffer(QETH_PROT_IPV4);
	if (addr != NULL) {
		addr->u.a4.addr = ifa->ifa_address;
		addr->u.a4.mask = ifa->ifa_mask;
		addr->type = QETH_IP_TYPE_NORMAL;
	} else
		goto out;

	switch(event) {
	case NETDEV_UP:
		if (!qeth_add_ip(card, addr))
			kfree(addr);
		break;
	case NETDEV_DOWN:
		if (!qeth_delete_ip(card, addr))
			kfree(addr);
		break;
	default:
		break;
	}
 	if (qeth_set_thread_start_bit(card, QETH_SET_IP_THREAD) == 0)
		schedule_work(&card->kernel_thread_starter);
out:
	return NOTIFY_DONE;
}

static struct notifier_block qeth_ip_notifier = {
	qeth_ip_event,
	0
};

#ifdef CONFIG_QETH_IPV6
/**
 * IPv6 event handler
 */
static int
qeth_ip6_event(struct notifier_block *this,
	      unsigned long event,void *ptr)
{

	struct inet6_ifaddr *ifa = (struct inet6_ifaddr *)ptr;
	struct net_device *dev = (struct net_device *)ifa->idev->dev;
	struct qeth_ipaddr *addr;
	struct qeth_card *card;

	QETH_DBF_TEXT(trace,3,"ip6event");

	card = qeth_get_card_from_dev(dev);
	if (!card)
		return NOTIFY_DONE;
	if (!qeth_is_supported(card, IPA_IPV6))
		return NOTIFY_DONE;

	addr = qeth_get_addr_buffer(QETH_PROT_IPV6);
	if (addr != NULL) {
		memcpy(&addr->u.a6.addr, &ifa->addr, sizeof(struct in6_addr));
		addr->u.a6.pfxlen = ifa->prefix_len;
		addr->type = QETH_IP_TYPE_NORMAL;
	} else
		goto out;

	switch(event) {
	case NETDEV_UP:
		if (!qeth_add_ip(card, addr))
			kfree(addr);
		break;
	case NETDEV_DOWN:
		if (!qeth_delete_ip(card, addr))
			kfree(addr);
		break;
	default:
		break;
	}
 	if (qeth_set_thread_start_bit(card, QETH_SET_IP_THREAD) == 0)
		schedule_work(&card->kernel_thread_starter);
out:
	return NOTIFY_DONE;
}

static struct notifier_block qeth_ip6_notifier = {
	qeth_ip6_event,
	0
};
#endif

static int
__qeth_reboot_event_card(struct device *dev, void *data)
{
	struct qeth_card *card;

	card = (struct qeth_card *) dev->driver_data;
	qeth_clear_ip_list(card, 0, 0);
	qeth_qdio_clear_card(card, 0);
	return 0;
}

static int
qeth_reboot_event(struct notifier_block *this, unsigned long event, void *ptr)
{

	driver_for_each_device(&qeth_ccwgroup_driver.driver, NULL, NULL,
			       __qeth_reboot_event_card);
	return NOTIFY_DONE;
}


static struct notifier_block qeth_reboot_notifier = {
	qeth_reboot_event,
	0
};

static int
qeth_register_notifiers(void)
{
        int r;

	QETH_DBF_TEXT(trace,5,"regnotif");
	if ((r = register_reboot_notifier(&qeth_reboot_notifier)))
		return r;
	if ((r = register_inetaddr_notifier(&qeth_ip_notifier)))
		goto out_reboot;
#ifdef CONFIG_QETH_IPV6
	if ((r = register_inet6addr_notifier(&qeth_ip6_notifier)))
		goto out_ipv4;
#endif
	return 0;

#ifdef CONFIG_QETH_IPV6
out_ipv4:
	unregister_inetaddr_notifier(&qeth_ip_notifier);
#endif
out_reboot:
	unregister_reboot_notifier(&qeth_reboot_notifier);
	return r;
}

/**
 * unregister all event notifiers
 */
static void
qeth_unregister_notifiers(void)
{

	QETH_DBF_TEXT(trace,5,"unregnot");
	BUG_ON(unregister_reboot_notifier(&qeth_reboot_notifier));
	BUG_ON(unregister_inetaddr_notifier(&qeth_ip_notifier));
#ifdef CONFIG_QETH_IPV6
	BUG_ON(unregister_inet6addr_notifier(&qeth_ip6_notifier));
#endif /* QETH_IPV6 */

}

#ifdef CONFIG_QETH_IPV6
static int
qeth_ipv6_init(void)
{
	qeth_old_arp_constructor = arp_tbl.constructor;
	write_lock(&arp_tbl.lock);
	arp_tbl.constructor = qeth_arp_constructor;
	write_unlock(&arp_tbl.lock);

	arp_direct_ops = (struct neigh_ops*)
		kmalloc(sizeof(struct neigh_ops), GFP_KERNEL);
	if (!arp_direct_ops)
		return -ENOMEM;

	memcpy(arp_direct_ops, &arp_direct_ops_template,
	       sizeof(struct neigh_ops));

	return 0;
}

static void
qeth_ipv6_uninit(void)
{
	write_lock(&arp_tbl.lock);
	arp_tbl.constructor = qeth_old_arp_constructor;
	write_unlock(&arp_tbl.lock);
	kfree(arp_direct_ops);
}
#endif /* CONFIG_QETH_IPV6 */

static void
qeth_sysfs_unregister(void)
{
	qeth_remove_driver_attributes();
	ccw_driver_unregister(&qeth_ccw_driver);
	ccwgroup_driver_unregister(&qeth_ccwgroup_driver);
	s390_root_dev_unregister(qeth_root_dev);
}
/**
 * register qeth at sysfs
 */
static int
qeth_sysfs_register(void)
{
	int rc=0;

	rc = ccwgroup_driver_register(&qeth_ccwgroup_driver);
	if (rc)
		return rc;
	rc = ccw_driver_register(&qeth_ccw_driver);
	if (rc)
	 	return rc;
	rc = qeth_create_driver_attributes();
	if (rc)
		return rc;
	qeth_root_dev = s390_root_dev_register("qeth");
	if (IS_ERR(qeth_root_dev)) {
		rc = PTR_ERR(qeth_root_dev);
		return rc;
	}
	return 0;
}

/***
 * init function
 */
static int __init
qeth_init(void)
{
	int rc=0;

	PRINT_INFO("loading %s (%s/%s/%s/%s/%s/%s/%s %s %s)\n",
		   version, VERSION_QETH_C, VERSION_QETH_H,
		   VERSION_QETH_MPC_H, VERSION_QETH_MPC_C,
		   VERSION_QETH_FS_H, VERSION_QETH_PROC_C,
		   VERSION_QETH_SYS_C, QETH_VERSION_IPV6,
		   QETH_VERSION_VLAN);

	INIT_LIST_HEAD(&qeth_card_list.list);
	INIT_LIST_HEAD(&qeth_notify_list);
	spin_lock_init(&qeth_notify_lock);
	rwlock_init(&qeth_card_list.rwlock);

	if (qeth_register_dbf_views())
		goto out_err;
	if (qeth_sysfs_register())
		goto out_sysfs;

#ifdef CONFIG_QETH_IPV6
	if (qeth_ipv6_init()) {
		PRINT_ERR("Out of memory during ipv6 init.\n");
		goto out_sysfs;
	}
#endif /* QETH_IPV6 */
	if (qeth_register_notifiers())
		goto out_ipv6;
	if (qeth_create_procfs_entries())
		goto out_notifiers;

	return rc;

out_notifiers:
	qeth_unregister_notifiers();
out_ipv6:
#ifdef CONFIG_QETH_IPV6
	qeth_ipv6_uninit();
#endif /* QETH_IPV6 */
out_sysfs:
	qeth_sysfs_unregister();
	qeth_unregister_dbf_views();
out_err:
	PRINT_ERR("Initialization failed");
	return rc;
}

static void
__exit qeth_exit(void)
{
	struct qeth_card *card, *tmp;
	unsigned long flags;

	QETH_DBF_TEXT(trace,1, "cleanup.");

	/*
	 * Weed would not need to clean up our devices here, because the
	 * common device layer calls qeth_remove_device for each device
	 * as soon as we unregister our driver (done in qeth_sysfs_unregister).
	 * But we do cleanup here so we can do a "soft" shutdown of our cards.
	 * qeth_remove_device called by the common device layer would otherwise
	 * do a "hard" shutdown (card->use_hard_stop is set to one in
	 * qeth_remove_device).
	 */
again:
	read_lock_irqsave(&qeth_card_list.rwlock, flags);
	list_for_each_entry_safe(card, tmp, &qeth_card_list.list, list){
		read_unlock_irqrestore(&qeth_card_list.rwlock, flags);
		qeth_set_offline(card->gdev);
		qeth_remove_device(card->gdev);
		goto again;
	}
	read_unlock_irqrestore(&qeth_card_list.rwlock, flags);
#ifdef CONFIG_QETH_IPV6
	qeth_ipv6_uninit();
#endif
	qeth_unregister_notifiers();
	qeth_remove_procfs_entries();
	qeth_sysfs_unregister();
	qeth_unregister_dbf_views();
	printk("qeth: removed\n");
}

EXPORT_SYMBOL(qeth_osn_register);
EXPORT_SYMBOL(qeth_osn_deregister);
EXPORT_SYMBOL(qeth_osn_assist);
module_init(qeth_init);
module_exit(qeth_exit);
MODULE_AUTHOR("Frank Pavlic <fpavlic@de.ibm.com>");
MODULE_DESCRIPTION("Linux on zSeries OSA Express and HiperSockets support\n" \
		                      "Copyright 2000,2003 IBM Corporation\n");

MODULE_LICENSE("GPL");
