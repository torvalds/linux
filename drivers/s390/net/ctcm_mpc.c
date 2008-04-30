/*
 *	drivers/s390/net/ctcm_mpc.c
 *
 *	Copyright IBM Corp. 2004, 2007
 *	Authors:	Belinda Thompson (belindat@us.ibm.com)
 *			Andy Richter (richtera@us.ibm.com)
 *			Peter Tiedemann (ptiedem@de.ibm.com)
 */

/*
	This module exports functions to be used by CCS:
	EXPORT_SYMBOL(ctc_mpc_alloc_channel);
	EXPORT_SYMBOL(ctc_mpc_establish_connectivity);
	EXPORT_SYMBOL(ctc_mpc_dealloc_ch);
	EXPORT_SYMBOL(ctc_mpc_flow_control);
*/

#undef DEBUG
#undef DEBUGDATA
#undef DEBUGCCW

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/sched.h>

#include <linux/signal.h>
#include <linux/string.h>
#include <linux/proc_fs.h>

#include <linux/ip.h>
#include <linux/if_arp.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/ctype.h>
#include <linux/netdevice.h>
#include <net/dst.h>

#include <linux/io.h>		/* instead of <asm/io.h> ok ? */
#include <asm/ccwdev.h>
#include <asm/ccwgroup.h>
#include <linux/bitops.h>	/* instead of <asm/bitops.h> ok ? */
#include <linux/uaccess.h>	/* instead of <asm/uaccess.h> ok ? */
#include <linux/wait.h>
#include <linux/moduleparam.h>
#include <asm/idals.h>

#include "cu3088.h"
#include "ctcm_mpc.h"
#include "ctcm_main.h"
#include "ctcm_fsms.h"

static const struct xid2 init_xid = {
	.xid2_type_id	=	XID_FM2,
	.xid2_len	=	0x45,
	.xid2_adj_id	=	0,
	.xid2_rlen	=	0x31,
	.xid2_resv1	=	0,
	.xid2_flag1	=	0,
	.xid2_fmtt	=	0,
	.xid2_flag4	=	0x80,
	.xid2_resv2	=	0,
	.xid2_tgnum	=	0,
	.xid2_sender_id	=	0,
	.xid2_flag2	=	0,
	.xid2_option	=	XID2_0,
	.xid2_resv3	=	"\x00",
	.xid2_resv4	=	0,
	.xid2_dlc_type	=	XID2_READ_SIDE,
	.xid2_resv5	=	0,
	.xid2_mpc_flag	=	0,
	.xid2_resv6	=	0,
	.xid2_buf_len	=	(MPC_BUFSIZE_DEFAULT - 35),
};

static const struct th_header thnorm = {
	.th_seg		=	0x00,
	.th_ch_flag	=	TH_IS_XID,
	.th_blk_flag	=	TH_DATA_IS_XID,
	.th_is_xid	=	0x01,
	.th_seq_num	=	0x00000000,
};

static const struct th_header thdummy = {
	.th_seg		=	0x00,
	.th_ch_flag	=	0x00,
	.th_blk_flag	=	TH_DATA_IS_XID,
	.th_is_xid	=	0x01,
	.th_seq_num	=	0x00000000,
};

/*
 * Definition of one MPC group
 */

/*
 * Compatibility macros for busy handling
 * of network devices.
 */

static void ctcmpc_unpack_skb(struct channel *ch, struct sk_buff *pskb);

/*
 * MPC Group state machine actions (static prototypes)
 */
static void mpc_action_nop(fsm_instance *fsm, int event, void *arg);
static void mpc_action_go_ready(fsm_instance *fsm, int event, void *arg);
static void mpc_action_go_inop(fsm_instance *fi, int event, void *arg);
static void mpc_action_timeout(fsm_instance *fi, int event, void *arg);
static int  mpc_validate_xid(struct mpcg_info *mpcginfo);
static void mpc_action_yside_xid(fsm_instance *fsm, int event, void *arg);
static void mpc_action_doxid0(fsm_instance *fsm, int event, void *arg);
static void mpc_action_doxid7(fsm_instance *fsm, int event, void *arg);
static void mpc_action_xside_xid(fsm_instance *fsm, int event, void *arg);
static void mpc_action_rcvd_xid0(fsm_instance *fsm, int event, void *arg);
static void mpc_action_rcvd_xid7(fsm_instance *fsm, int event, void *arg);

#ifdef DEBUGDATA
/*-------------------------------------------------------------------*
* Dump buffer format						     *
*								     *
*--------------------------------------------------------------------*/
void ctcmpc_dumpit(char *buf, int len)
{
	__u32	ct, sw, rm, dup;
	char	*ptr, *rptr;
	char	tbuf[82], tdup[82];
	#if (UTS_MACHINE == s390x)
	char	addr[22];
	#else
	char	addr[12];
	#endif
	char	boff[12];
	char	bhex[82], duphex[82];
	char	basc[40];

	sw  = 0;
	rptr = ptr = buf;
	rm  = 16;
	duphex[0] = 0x00;
	dup = 0;

	for (ct = 0; ct < len; ct++, ptr++, rptr++) {
		if (sw == 0) {
			#if (UTS_MACHINE == s390x)
			sprintf(addr, "%16.16lx", (unsigned long)rptr);
			#else
			sprintf(addr, "%8.8X", (__u32)rptr);
			#endif

			sprintf(boff, "%4.4X", (__u32)ct);
			bhex[0] = '\0';
			basc[0] = '\0';
		}
		if ((sw == 4) || (sw == 12))
			strcat(bhex, " ");
		if (sw == 8)
			strcat(bhex, "	");

		#if (UTS_MACHINE == s390x)
		sprintf(tbuf, "%2.2lX", (unsigned long)*ptr);
		#else
		sprintf(tbuf, "%2.2X", (__u32)*ptr);
		#endif

		tbuf[2] = '\0';
		strcat(bhex, tbuf);
		if ((0 != isprint(*ptr)) && (*ptr >= 0x20))
			basc[sw] = *ptr;
		else
			basc[sw] = '.';

		basc[sw+1] = '\0';
		sw++;
		rm--;
		if (sw == 16) {
			if ((strcmp(duphex, bhex)) != 0) {
				if (dup != 0) {
					sprintf(tdup, "Duplicate as above "
						"to %s", addr);
					printk(KERN_INFO "		  "
						"     --- %s ---\n", tdup);
				}
				printk(KERN_INFO "   %s (+%s) : %s  [%s]\n",
					addr, boff, bhex, basc);
				dup = 0;
				strcpy(duphex, bhex);
			} else
				dup++;

			sw = 0;
			rm = 16;
		}
	}  /* endfor */

	if (sw != 0) {
		for ( ; rm > 0; rm--, sw++) {
			if ((sw == 4) || (sw == 12))
				strcat(bhex, " ");
			if (sw == 8)
				strcat(bhex, "	");
			strcat(bhex, "	");
			strcat(basc, " ");
		}
		if (dup != 0) {
			sprintf(tdup, "Duplicate as above to %s", addr);
			printk(KERN_INFO "		  "
				"     --- %s ---\n", tdup);
		}
		printk(KERN_INFO "   %s (+%s) : %s  [%s]\n",
			addr, boff, bhex, basc);
	} else {
		if (dup >= 1) {
			sprintf(tdup, "Duplicate as above to %s", addr);
			printk(KERN_INFO "		  "
				"     --- %s ---\n", tdup);
		}
		if (dup != 0) {
			printk(KERN_INFO "   %s (+%s) : %s  [%s]\n",
				addr, boff, bhex, basc);
		}
	}

	return;

}   /*	 end of ctcmpc_dumpit  */
#endif

#ifdef DEBUGDATA
/*
 * Dump header and first 16 bytes of an sk_buff for debugging purposes.
 *
 * skb		The sk_buff to dump.
 * offset	Offset relative to skb-data, where to start the dump.
 */
void ctcmpc_dump_skb(struct sk_buff *skb, int offset)
{
	unsigned char *p = skb->data;
	struct th_header *header;
	struct pdu *pheader;
	int bl = skb->len;
	int i;

	if (p == NULL)
		return;

	p += offset;
	header = (struct th_header *)p;

	printk(KERN_INFO "dump:\n");
	printk(KERN_INFO "skb len=%d \n", skb->len);
	if (skb->len > 2) {
		switch (header->th_ch_flag) {
		case TH_HAS_PDU:
			break;
		case 0x00:
		case TH_IS_XID:
			if ((header->th_blk_flag == TH_DATA_IS_XID) &&
			   (header->th_is_xid == 0x01))
				goto dumpth;
		case TH_SWEEP_REQ:
				goto dumpth;
		case TH_SWEEP_RESP:
				goto dumpth;
		default:
			break;
		}

		pheader = (struct pdu *)p;
		printk(KERN_INFO "pdu->offset: %d hex: %04x\n",
		       pheader->pdu_offset, pheader->pdu_offset);
		printk(KERN_INFO "pdu->flag  : %02x\n", pheader->pdu_flag);
		printk(KERN_INFO "pdu->proto : %02x\n", pheader->pdu_proto);
		printk(KERN_INFO "pdu->seq   : %02x\n", pheader->pdu_seq);
					goto dumpdata;

dumpth:
		printk(KERN_INFO "th->seg     : %02x\n", header->th_seg);
		printk(KERN_INFO "th->ch      : %02x\n", header->th_ch_flag);
		printk(KERN_INFO "th->blk_flag: %02x\n", header->th_blk_flag);
		printk(KERN_INFO "th->type    : %s\n",
		       (header->th_is_xid) ? "DATA" : "XID");
		printk(KERN_INFO "th->seqnum  : %04x\n", header->th_seq_num);

	}
dumpdata:
	if (bl > 32)
		bl = 32;
	printk(KERN_INFO "data: ");
	for (i = 0; i < bl; i++)
		printk(KERN_INFO "%02x%s", *p++, (i % 16) ? " " : "\n<7>");
	printk(KERN_INFO "\n");
}
#endif

/*
 * ctc_mpc_alloc_channel
 *	(exported interface)
 *
 * Device Initialization :
 *	ACTPATH  driven IO operations
 */
int ctc_mpc_alloc_channel(int port_num, void (*callback)(int, int))
{
	char device[20];
	struct net_device *dev;
	struct mpc_group *grp;
	struct ctcm_priv *priv;

	ctcm_pr_debug("ctcmpc enter:	%s()\n", __FUNCTION__);

	sprintf(device, "%s%i", MPC_DEVICE_NAME, port_num);
	dev = __dev_get_by_name(&init_net, device);

	if (dev == NULL) {
		printk(KERN_INFO "ctc_mpc_alloc_channel %s dev=NULL\n", device);
		return 1;
	}

	priv = dev->priv;
	grp = priv->mpcg;
	if (!grp)
		return 1;

	grp->allochanfunc = callback;
	grp->port_num = port_num;
	grp->port_persist = 1;

	ctcm_pr_debug("ctcmpc: %s called for device %s state=%s\n",
		       __FUNCTION__,
		       dev->name,
		       fsm_getstate_str(grp->fsm));

	switch (fsm_getstate(grp->fsm)) {
	case MPCG_STATE_INOP:
		/* Group is in the process of terminating */
		grp->alloc_called = 1;
		break;
	case MPCG_STATE_RESET:
		/* MPC Group will transition to state		  */
		/* MPCG_STATE_XID2INITW iff the minimum number	  */
		/* of 1 read and 1 write channel have successfully*/
		/* activated					  */
		/*fsm_newstate(grp->fsm, MPCG_STATE_XID2INITW);*/
		if (callback)
			grp->send_qllc_disc = 1;
	case MPCG_STATE_XID0IOWAIT:
		fsm_deltimer(&grp->timer);
		grp->outstanding_xid2 = 0;
		grp->outstanding_xid7 = 0;
		grp->outstanding_xid7_p2 = 0;
		grp->saved_xid2 = NULL;
		if (callback)
			ctcm_open(dev);
		fsm_event(priv->fsm, DEV_EVENT_START, dev);
		break;
	case MPCG_STATE_READY:
		/* XID exchanges completed after PORT was activated */
		/* Link station already active			    */
		/* Maybe timing issue...retry callback		    */
		grp->allocchan_callback_retries++;
		if (grp->allocchan_callback_retries < 4) {
			if (grp->allochanfunc)
				grp->allochanfunc(grp->port_num,
					      grp->group_max_buflen);
		} else {
			/* there are problems...bail out	    */
			/* there may be a state mismatch so restart */
			grp->port_persist = 1;
			fsm_event(grp->fsm, MPCG_EVENT_INOP, dev);
			grp->allocchan_callback_retries = 0;
		}
		break;
	default:
		return 0;

	}

	ctcm_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
	return 0;
}
EXPORT_SYMBOL(ctc_mpc_alloc_channel);

/*
 * ctc_mpc_establish_connectivity
 *	(exported interface)
 */
void ctc_mpc_establish_connectivity(int port_num,
				void (*callback)(int, int, int))
{
	char device[20];
	struct net_device *dev;
	struct mpc_group *grp;
	struct ctcm_priv *priv;
	struct channel *rch, *wch;

	ctcm_pr_debug("ctcmpc enter:	%s()\n", __FUNCTION__);

	sprintf(device, "%s%i", MPC_DEVICE_NAME, port_num);
	dev = __dev_get_by_name(&init_net, device);

	if (dev == NULL) {
		printk(KERN_INFO "ctc_mpc_establish_connectivity "
				"%s dev=NULL\n", device);
		return;
	}
	priv = dev->priv;
	rch = priv->channel[READ];
	wch = priv->channel[WRITE];

	grp = priv->mpcg;

	ctcm_pr_debug("ctcmpc: %s() called for device %s state=%s\n",
			__FUNCTION__, dev->name,
			fsm_getstate_str(grp->fsm));

	grp->estconnfunc = callback;
	grp->port_num = port_num;

	switch (fsm_getstate(grp->fsm)) {
	case MPCG_STATE_READY:
		/* XID exchanges completed after PORT was activated */
		/* Link station already active			    */
		/* Maybe timing issue...retry callback		    */
		fsm_deltimer(&grp->timer);
		grp->estconn_callback_retries++;
		if (grp->estconn_callback_retries < 4) {
			if (grp->estconnfunc) {
				grp->estconnfunc(grp->port_num, 0,
						grp->group_max_buflen);
				grp->estconnfunc = NULL;
			}
		} else {
			/* there are problems...bail out	 */
			fsm_event(grp->fsm, MPCG_EVENT_INOP, dev);
			grp->estconn_callback_retries = 0;
		}
		break;
	case MPCG_STATE_INOP:
	case MPCG_STATE_RESET:
		/* MPC Group is not ready to start XID - min num of */
		/* 1 read and 1 write channel have not been acquired*/
		printk(KERN_WARNING "ctcmpc: %s() REJECTED ACTIVE XID Req"
			"uest - Channel Pair is not Active\n", __FUNCTION__);
		if (grp->estconnfunc) {
			grp->estconnfunc(grp->port_num, -1, 0);
			grp->estconnfunc = NULL;
		}
		break;
	case MPCG_STATE_XID2INITW:
		/* alloc channel was called but no XID exchange    */
		/* has occurred. initiate xside XID exchange	   */
		/* make sure yside XID0 processing has not started */
		if ((fsm_getstate(rch->fsm) > CH_XID0_PENDING) ||
			(fsm_getstate(wch->fsm) > CH_XID0_PENDING)) {
			printk(KERN_WARNING "mpc: %s() ABORT ACTIVE XID"
			       " Request- PASSIVE XID in process\n"
			       , __FUNCTION__);
			break;
		}
		grp->send_qllc_disc = 1;
		fsm_newstate(grp->fsm, MPCG_STATE_XID0IOWAIT);
		fsm_deltimer(&grp->timer);
		fsm_addtimer(&grp->timer, MPC_XID_TIMEOUT_VALUE,
						MPCG_EVENT_TIMER, dev);
		grp->outstanding_xid7 = 0;
		grp->outstanding_xid7_p2 = 0;
		grp->saved_xid2 = NULL;
		if ((rch->in_mpcgroup) &&
				(fsm_getstate(rch->fsm) == CH_XID0_PENDING))
			fsm_event(grp->fsm, MPCG_EVENT_XID0DO, rch);
		else {
			printk(KERN_WARNING "mpc: %s() Unable to start"
			       " ACTIVE XID0 on read channel\n",
			       __FUNCTION__);
			if (grp->estconnfunc) {
				grp->estconnfunc(grp->port_num, -1, 0);
				grp->estconnfunc = NULL;
			}
			fsm_deltimer(&grp->timer);
				goto done;
		}
		if ((wch->in_mpcgroup) &&
				(fsm_getstate(wch->fsm) == CH_XID0_PENDING))
			fsm_event(grp->fsm, MPCG_EVENT_XID0DO, wch);
		else {
			printk(KERN_WARNING "mpc: %s() Unable to start"
				" ACTIVE XID0 on write channel\n",
					__FUNCTION__);
			if (grp->estconnfunc) {
				grp->estconnfunc(grp->port_num, -1, 0);
				grp->estconnfunc = NULL;
			}
			fsm_deltimer(&grp->timer);
				goto done;
			}
		break;
	case MPCG_STATE_XID0IOWAIT:
		/* already in active XID negotiations */
	default:
		break;
	}

done:
	ctcm_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
	return;
}
EXPORT_SYMBOL(ctc_mpc_establish_connectivity);

/*
 * ctc_mpc_dealloc_ch
 *	(exported interface)
 */
void ctc_mpc_dealloc_ch(int port_num)
{
	struct net_device *dev;
	char device[20];
	struct ctcm_priv *priv;
	struct mpc_group *grp;

	ctcm_pr_debug("ctcmpc enter:	%s()\n", __FUNCTION__);
	sprintf(device, "%s%i", MPC_DEVICE_NAME, port_num);
	dev = __dev_get_by_name(&init_net, device);

	if (dev == NULL) {
		printk(KERN_INFO "%s() %s dev=NULL\n", __FUNCTION__, device);
					goto done;
	}

	ctcm_pr_debug("ctcmpc:%s %s() called for device %s refcount=%d\n",
			dev->name, __FUNCTION__,
			dev->name, atomic_read(&dev->refcnt));

	priv = dev->priv;
	if (priv == NULL) {
		printk(KERN_INFO "%s() %s priv=NULL\n",
				__FUNCTION__, device);
					goto done;
	}
	fsm_deltimer(&priv->restart_timer);

	grp = priv->mpcg;
	if (grp == NULL) {
		printk(KERN_INFO "%s() %s dev=NULL\n", __FUNCTION__, device);
					goto done;
	}
	grp->channels_terminating = 0;

	fsm_deltimer(&grp->timer);

	grp->allochanfunc = NULL;
	grp->estconnfunc = NULL;
	grp->port_persist = 0;
	grp->send_qllc_disc = 0;
	fsm_event(grp->fsm, MPCG_EVENT_INOP, dev);

	ctcm_close(dev);
done:
	ctcm_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
	return;
}
EXPORT_SYMBOL(ctc_mpc_dealloc_ch);

/*
 * ctc_mpc_flow_control
 *	(exported interface)
 */
void ctc_mpc_flow_control(int port_num, int flowc)
{
	char device[20];
	struct ctcm_priv *priv;
	struct mpc_group *grp;
	struct net_device *dev;
	struct channel *rch;
	int mpcg_state;

	ctcm_pr_debug("ctcmpc enter:	%s() %i\n", __FUNCTION__, flowc);

	sprintf(device, "%s%i", MPC_DEVICE_NAME, port_num);
	dev = __dev_get_by_name(&init_net, device);

	if (dev == NULL) {
		printk(KERN_INFO "ctc_mpc_flow_control %s dev=NULL\n", device);
		return;
	}

	ctcm_pr_debug("ctcmpc: %s %s called \n", dev->name, __FUNCTION__);

	priv  = dev->priv;
	if (priv == NULL) {
		printk(KERN_INFO "ctcmpc:%s() %s priv=NULL\n",
		       __FUNCTION__, device);
		return;
	}
	grp = priv->mpcg;
	rch = priv->channel[READ];

	mpcg_state = fsm_getstate(grp->fsm);
	switch (flowc) {
	case 1:
		if (mpcg_state == MPCG_STATE_FLOWC)
			break;
		if (mpcg_state == MPCG_STATE_READY) {
			if (grp->flow_off_called == 1)
				grp->flow_off_called = 0;
			else
				fsm_newstate(grp->fsm, MPCG_STATE_FLOWC);
			break;
		}
		break;
	case 0:
		if (mpcg_state == MPCG_STATE_FLOWC) {
			fsm_newstate(grp->fsm, MPCG_STATE_READY);
			/* ensure any data that has accumulated */
			/* on the io_queue will now be sen t	*/
			tasklet_schedule(&rch->ch_tasklet);
		}
		/* possible race condition			*/
		if (mpcg_state == MPCG_STATE_READY) {
			grp->flow_off_called = 1;
			break;
		}
		break;
	}

	ctcm_pr_debug("ctcmpc exit:  %s() %i\n", __FUNCTION__, flowc);
}
EXPORT_SYMBOL(ctc_mpc_flow_control);

static int mpc_send_qllc_discontact(struct net_device *);

/*
 * helper function of ctcmpc_unpack_skb
*/
static void mpc_rcvd_sweep_resp(struct mpcg_info *mpcginfo)
{
	struct channel	  *rch = mpcginfo->ch;
	struct net_device *dev = rch->netdev;
	struct ctcm_priv   *priv = dev->priv;
	struct mpc_group  *grp = priv->mpcg;
	struct channel	  *ch = priv->channel[WRITE];

	if (do_debug)
		ctcm_pr_debug("ctcmpc enter: %s(): ch=0x%p id=%s\n",
			__FUNCTION__, ch, ch->id);

	if (do_debug_data)
		ctcmpc_dumpit((char *)mpcginfo->sweep, TH_SWEEP_LENGTH);

	grp->sweep_rsp_pend_num--;

	if ((grp->sweep_req_pend_num == 0) &&
			(grp->sweep_rsp_pend_num == 0)) {
		fsm_deltimer(&ch->sweep_timer);
		grp->in_sweep = 0;
		rch->th_seq_num = 0x00;
		ch->th_seq_num = 0x00;
		ctcm_clear_busy_do(dev);
	}

	kfree(mpcginfo);

	return;

}

/*
 * helper function of mpc_rcvd_sweep_req
 * which is a helper of ctcmpc_unpack_skb
 */
static void ctcmpc_send_sweep_resp(struct channel *rch)
{
	struct net_device *dev = rch->netdev;
	struct ctcm_priv *priv = dev->priv;
	struct mpc_group *grp = priv->mpcg;
	int rc = 0;
	struct th_sweep *header;
	struct sk_buff *sweep_skb;
	struct channel *ch  = priv->channel[WRITE];

	if (do_debug)
		ctcm_pr_debug("ctcmpc exit : %s(): ch=0x%p id=%s\n",
			__FUNCTION__, rch, rch->id);

	sweep_skb = __dev_alloc_skb(MPC_BUFSIZE_DEFAULT,
				    GFP_ATOMIC|GFP_DMA);
	if (sweep_skb == NULL) {
		printk(KERN_INFO "Couldn't alloc sweep_skb\n");
		rc = -ENOMEM;
				goto done;
	}

	header = (struct th_sweep *)
			kmalloc(sizeof(struct th_sweep), gfp_type());

	if (!header) {
		dev_kfree_skb_any(sweep_skb);
		rc = -ENOMEM;
				goto done;
	}

	header->th.th_seg	= 0x00 ;
	header->th.th_ch_flag	= TH_SWEEP_RESP;
	header->th.th_blk_flag	= 0x00;
	header->th.th_is_xid	= 0x00;
	header->th.th_seq_num	= 0x00;
	header->sw.th_last_seq	= ch->th_seq_num;

	memcpy(skb_put(sweep_skb, TH_SWEEP_LENGTH), header, TH_SWEEP_LENGTH);

	kfree(header);

	dev->trans_start = jiffies;
	skb_queue_tail(&ch->sweep_queue, sweep_skb);

	fsm_addtimer(&ch->sweep_timer, 100, CTC_EVENT_RSWEEP_TIMER, ch);

	return;

done:
	if (rc != 0) {
		grp->in_sweep = 0;
		ctcm_clear_busy_do(dev);
		fsm_event(grp->fsm, MPCG_EVENT_INOP, dev);
	}

	return;
}

/*
 * helper function of ctcmpc_unpack_skb
 */
static void mpc_rcvd_sweep_req(struct mpcg_info *mpcginfo)
{
	struct channel	  *rch     = mpcginfo->ch;
	struct net_device *dev     = rch->netdev;
	struct ctcm_priv  *priv = dev->priv;
	struct mpc_group  *grp  = priv->mpcg;
	struct channel	  *ch	   = priv->channel[WRITE];

	if (do_debug)
		CTCM_DBF_TEXT_(MPC_TRACE, CTC_DBF_DEBUG,
			" %s(): ch=0x%p id=%s\n", __FUNCTION__, ch, ch->id);

	if (grp->in_sweep == 0) {
		grp->in_sweep = 1;
		ctcm_test_and_set_busy(dev);
		grp->sweep_req_pend_num = grp->active_channels[READ];
		grp->sweep_rsp_pend_num = grp->active_channels[READ];
	}

	if (do_debug_data)
		ctcmpc_dumpit((char *)mpcginfo->sweep, TH_SWEEP_LENGTH);

	grp->sweep_req_pend_num--;
	ctcmpc_send_sweep_resp(ch);
	kfree(mpcginfo);
	return;
}

/*
  * MPC Group Station FSM definitions
 */
static const char *mpcg_event_names[] = {
	[MPCG_EVENT_INOP]	= "INOP Condition",
	[MPCG_EVENT_DISCONC]	= "Discontact Received",
	[MPCG_EVENT_XID0DO]	= "Channel Active - Start XID",
	[MPCG_EVENT_XID2]	= "XID2 Received",
	[MPCG_EVENT_XID2DONE]	= "XID0 Complete",
	[MPCG_EVENT_XID7DONE]	= "XID7 Complete",
	[MPCG_EVENT_TIMER]	= "XID Setup Timer",
	[MPCG_EVENT_DOIO]	= "XID DoIO",
};

static const char *mpcg_state_names[] = {
	[MPCG_STATE_RESET]	= "Reset",
	[MPCG_STATE_INOP]	= "INOP",
	[MPCG_STATE_XID2INITW]	= "Passive XID- XID0 Pending Start",
	[MPCG_STATE_XID2INITX]	= "Passive XID- XID0 Pending Complete",
	[MPCG_STATE_XID7INITW]	= "Passive XID- XID7 Pending P1 Start",
	[MPCG_STATE_XID7INITX]	= "Passive XID- XID7 Pending P2 Complete",
	[MPCG_STATE_XID0IOWAIT]	= "Active  XID- XID0 Pending Start",
	[MPCG_STATE_XID0IOWAIX]	= "Active  XID- XID0 Pending Complete",
	[MPCG_STATE_XID7INITI]	= "Active  XID- XID7 Pending Start",
	[MPCG_STATE_XID7INITZ]	= "Active  XID- XID7 Pending Complete ",
	[MPCG_STATE_XID7INITF]	= "XID        - XID7 Complete ",
	[MPCG_STATE_FLOWC]	= "FLOW CONTROL ON",
	[MPCG_STATE_READY]	= "READY",
};

/*
 * The MPC Group Station FSM
 *   22 events
 */
static const fsm_node mpcg_fsm[] = {
	{ MPCG_STATE_RESET,	MPCG_EVENT_INOP,	mpc_action_go_inop    },
	{ MPCG_STATE_INOP,	MPCG_EVENT_INOP,	mpc_action_nop        },
	{ MPCG_STATE_FLOWC,	MPCG_EVENT_INOP,	mpc_action_go_inop    },

	{ MPCG_STATE_READY,	MPCG_EVENT_DISCONC,	mpc_action_discontact },
	{ MPCG_STATE_READY,	MPCG_EVENT_INOP,	mpc_action_go_inop    },

	{ MPCG_STATE_XID2INITW,	MPCG_EVENT_XID0DO,	mpc_action_doxid0     },
	{ MPCG_STATE_XID2INITW,	MPCG_EVENT_XID2,	mpc_action_rcvd_xid0  },
	{ MPCG_STATE_XID2INITW,	MPCG_EVENT_INOP,	mpc_action_go_inop    },
	{ MPCG_STATE_XID2INITW,	MPCG_EVENT_TIMER,	mpc_action_timeout    },
	{ MPCG_STATE_XID2INITW,	MPCG_EVENT_DOIO,	mpc_action_yside_xid  },

	{ MPCG_STATE_XID2INITX,	MPCG_EVENT_XID0DO,	mpc_action_doxid0     },
	{ MPCG_STATE_XID2INITX,	MPCG_EVENT_XID2,	mpc_action_rcvd_xid0  },
	{ MPCG_STATE_XID2INITX,	MPCG_EVENT_INOP,	mpc_action_go_inop    },
	{ MPCG_STATE_XID2INITX,	MPCG_EVENT_TIMER,	mpc_action_timeout    },
	{ MPCG_STATE_XID2INITX,	MPCG_EVENT_DOIO,	mpc_action_yside_xid  },

	{ MPCG_STATE_XID7INITW,	MPCG_EVENT_XID2DONE,	mpc_action_doxid7     },
	{ MPCG_STATE_XID7INITW,	MPCG_EVENT_DISCONC,	mpc_action_discontact },
	{ MPCG_STATE_XID7INITW,	MPCG_EVENT_XID2,	mpc_action_rcvd_xid7  },
	{ MPCG_STATE_XID7INITW,	MPCG_EVENT_INOP,	mpc_action_go_inop    },
	{ MPCG_STATE_XID7INITW,	MPCG_EVENT_TIMER,	mpc_action_timeout    },
	{ MPCG_STATE_XID7INITW,	MPCG_EVENT_XID7DONE,	mpc_action_doxid7     },
	{ MPCG_STATE_XID7INITW,	MPCG_EVENT_DOIO,	mpc_action_yside_xid  },

	{ MPCG_STATE_XID7INITX,	MPCG_EVENT_DISCONC,	mpc_action_discontact },
	{ MPCG_STATE_XID7INITX,	MPCG_EVENT_XID2,	mpc_action_rcvd_xid7  },
	{ MPCG_STATE_XID7INITX,	MPCG_EVENT_INOP,	mpc_action_go_inop    },
	{ MPCG_STATE_XID7INITX,	MPCG_EVENT_XID7DONE,	mpc_action_doxid7     },
	{ MPCG_STATE_XID7INITX,	MPCG_EVENT_TIMER,	mpc_action_timeout    },
	{ MPCG_STATE_XID7INITX,	MPCG_EVENT_DOIO,	mpc_action_yside_xid  },

	{ MPCG_STATE_XID0IOWAIT, MPCG_EVENT_XID0DO,	mpc_action_doxid0     },
	{ MPCG_STATE_XID0IOWAIT, MPCG_EVENT_DISCONC,	mpc_action_discontact },
	{ MPCG_STATE_XID0IOWAIT, MPCG_EVENT_XID2,	mpc_action_rcvd_xid0  },
	{ MPCG_STATE_XID0IOWAIT, MPCG_EVENT_INOP,	mpc_action_go_inop    },
	{ MPCG_STATE_XID0IOWAIT, MPCG_EVENT_TIMER,	mpc_action_timeout    },
	{ MPCG_STATE_XID0IOWAIT, MPCG_EVENT_DOIO,	mpc_action_xside_xid  },

	{ MPCG_STATE_XID0IOWAIX, MPCG_EVENT_XID0DO,	mpc_action_doxid0     },
	{ MPCG_STATE_XID0IOWAIX, MPCG_EVENT_DISCONC,	mpc_action_discontact },
	{ MPCG_STATE_XID0IOWAIX, MPCG_EVENT_XID2,	mpc_action_rcvd_xid0  },
	{ MPCG_STATE_XID0IOWAIX, MPCG_EVENT_INOP,	mpc_action_go_inop    },
	{ MPCG_STATE_XID0IOWAIX, MPCG_EVENT_TIMER,	mpc_action_timeout    },
	{ MPCG_STATE_XID0IOWAIX, MPCG_EVENT_DOIO,	mpc_action_xside_xid  },

	{ MPCG_STATE_XID7INITI,	MPCG_EVENT_XID2DONE,	mpc_action_doxid7     },
	{ MPCG_STATE_XID7INITI,	MPCG_EVENT_XID2,	mpc_action_rcvd_xid7  },
	{ MPCG_STATE_XID7INITI,	MPCG_EVENT_DISCONC,	mpc_action_discontact },
	{ MPCG_STATE_XID7INITI,	MPCG_EVENT_INOP,	mpc_action_go_inop    },
	{ MPCG_STATE_XID7INITI,	MPCG_EVENT_TIMER,	mpc_action_timeout    },
	{ MPCG_STATE_XID7INITI,	MPCG_EVENT_XID7DONE,	mpc_action_doxid7     },
	{ MPCG_STATE_XID7INITI,	MPCG_EVENT_DOIO,	mpc_action_xside_xid  },

	{ MPCG_STATE_XID7INITZ,	MPCG_EVENT_XID2,	mpc_action_rcvd_xid7  },
	{ MPCG_STATE_XID7INITZ,	MPCG_EVENT_XID7DONE,	mpc_action_doxid7     },
	{ MPCG_STATE_XID7INITZ,	MPCG_EVENT_DISCONC,	mpc_action_discontact },
	{ MPCG_STATE_XID7INITZ,	MPCG_EVENT_INOP,	mpc_action_go_inop    },
	{ MPCG_STATE_XID7INITZ,	MPCG_EVENT_TIMER,	mpc_action_timeout    },
	{ MPCG_STATE_XID7INITZ,	MPCG_EVENT_DOIO,	mpc_action_xside_xid  },

	{ MPCG_STATE_XID7INITF,	MPCG_EVENT_INOP,	mpc_action_go_inop    },
	{ MPCG_STATE_XID7INITF,	MPCG_EVENT_XID7DONE,	mpc_action_go_ready   },
};

static int mpcg_fsm_len = ARRAY_SIZE(mpcg_fsm);

/*
 * MPC Group Station FSM action
 * CTCM_PROTO_MPC only
 */
static void mpc_action_go_ready(fsm_instance *fsm, int event, void *arg)
{
	struct net_device *dev = arg;
	struct ctcm_priv *priv = NULL;
	struct mpc_group *grp = NULL;

	if (dev == NULL) {
		printk(KERN_INFO "%s() dev=NULL\n", __FUNCTION__);
		return;
	}

	ctcm_pr_debug("ctcmpc enter: %s  %s()\n", dev->name, __FUNCTION__);

	priv = dev->priv;
	if (priv == NULL) {
		printk(KERN_INFO "%s() priv=NULL\n", __FUNCTION__);
		return;
	}

	grp = priv->mpcg;
	if (grp == NULL) {
		printk(KERN_INFO "%s() grp=NULL\n", __FUNCTION__);
		return;
	}

	fsm_deltimer(&grp->timer);

	if (grp->saved_xid2->xid2_flag2 == 0x40) {
		priv->xid->xid2_flag2 = 0x00;
		if (grp->estconnfunc) {
			grp->estconnfunc(grp->port_num, 1,
					grp->group_max_buflen);
			grp->estconnfunc = NULL;
		} else if (grp->allochanfunc)
			grp->send_qllc_disc = 1;
					goto done;
	}

	grp->port_persist = 1;
	grp->out_of_sequence = 0;
	grp->estconn_called = 0;

	tasklet_hi_schedule(&grp->mpc_tasklet2);

	ctcm_pr_debug("ctcmpc exit: %s  %s()\n", dev->name, __FUNCTION__);
	return;

done:
	fsm_event(grp->fsm, MPCG_EVENT_INOP, dev);


	ctcm_pr_info("ctcmpc: %s()failure occurred\n", __FUNCTION__);
}

/*
 * helper of ctcm_init_netdevice
 * CTCM_PROTO_MPC only
 */
void mpc_group_ready(unsigned long adev)
{
	struct net_device *dev = (struct net_device *)adev;
	struct ctcm_priv *priv = NULL;
	struct mpc_group  *grp = NULL;
	struct channel *ch = NULL;


	ctcm_pr_debug("ctcmpc enter:	%s()\n", __FUNCTION__);

	if (dev == NULL) {
		printk(KERN_INFO "%s() dev=NULL\n", __FUNCTION__);
		return;
	}

	priv = dev->priv;
	if (priv == NULL) {
		printk(KERN_INFO "%s() priv=NULL\n", __FUNCTION__);
		return;
	}

	grp = priv->mpcg;
	if (grp == NULL) {
		printk(KERN_INFO "ctcmpc:%s() grp=NULL\n", __FUNCTION__);
		return;
	}

	printk(KERN_NOTICE "ctcmpc: %s GROUP TRANSITIONED TO READY"
	       "  maxbuf:%d\n",
	       dev->name, grp->group_max_buflen);

	fsm_newstate(grp->fsm, MPCG_STATE_READY);

	/* Put up a read on the channel */
	ch = priv->channel[READ];
	ch->pdu_seq = 0;
	if (do_debug_data)
		ctcm_pr_debug("ctcmpc: %s() ToDCM_pdu_seq= %08x\n" ,
			__FUNCTION__, ch->pdu_seq);

	ctcmpc_chx_rxidle(ch->fsm, CTC_EVENT_START, ch);
	/* Put the write channel in idle state */
	ch = priv->channel[WRITE];
	if (ch->collect_len > 0) {
		spin_lock(&ch->collect_lock);
		ctcm_purge_skb_queue(&ch->collect_queue);
		ch->collect_len = 0;
		spin_unlock(&ch->collect_lock);
	}
	ctcm_chx_txidle(ch->fsm, CTC_EVENT_START, ch);

	ctcm_clear_busy(dev);

	if (grp->estconnfunc) {
		grp->estconnfunc(grp->port_num, 0,
				    grp->group_max_buflen);
		grp->estconnfunc = NULL;
	} else
		if (grp->allochanfunc)
		grp->allochanfunc(grp->port_num,
				     grp->group_max_buflen);

	grp->send_qllc_disc = 1;
	grp->changed_side = 0;

	ctcm_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
	return;

}

/*
 * Increment the MPC Group Active Channel Counts
 * helper of dev_action (called from channel fsm)
 */
int mpc_channel_action(struct channel *ch, int direction, int action)
{
	struct net_device  *dev     = ch->netdev;
	struct ctcm_priv    *priv;
	struct mpc_group   *grp  = NULL;
	int	    rc = 0;

	if (do_debug)
		ctcm_pr_debug("ctcmpc enter: %s(): ch=0x%p id=%s\n",
			__FUNCTION__, ch, ch->id);

	if (dev == NULL) {
		printk(KERN_INFO "ctcmpc_channel_action %i dev=NULL\n",
		       action);
		rc = 1;
					goto done;
	}

	priv = dev->priv;
	if (priv == NULL) {
		printk(KERN_INFO
		       "ctcmpc_channel_action%i priv=NULL, dev=%s\n",
		       action, dev->name);
		rc = 2;
					goto done;
	}

	grp = priv->mpcg;

	if (grp == NULL) {
		printk(KERN_INFO "ctcmpc: %s()%i mpcgroup=NULL, dev=%s\n",
		       __FUNCTION__, action, dev->name);
		rc = 3;
					goto done;
	}

	ctcm_pr_info(
		      "ctcmpc: %s() %i(): Grp:%s total_channel_paths=%i "
		      "active_channels read=%i, write=%i\n",
		      __FUNCTION__,
		      action,
		      fsm_getstate_str(grp->fsm),
		      grp->num_channel_paths,
		      grp->active_channels[READ],
		      grp->active_channels[WRITE]);

	if ((action == MPC_CHANNEL_ADD) && (ch->in_mpcgroup == 0)) {
		grp->num_channel_paths++;
		grp->active_channels[direction]++;
		grp->outstanding_xid2++;
		ch->in_mpcgroup = 1;

		if (ch->xid_skb != NULL)
			dev_kfree_skb_any(ch->xid_skb);

		ch->xid_skb = __dev_alloc_skb(MPC_BUFSIZE_DEFAULT,
					GFP_ATOMIC | GFP_DMA);
		if (ch->xid_skb == NULL) {
			printk(KERN_INFO "ctcmpc: %s()"
			       "Couldn't alloc ch xid_skb\n", __FUNCTION__);
			fsm_event(grp->fsm, MPCG_EVENT_INOP, dev);
			return 1;
		}
		ch->xid_skb_data = ch->xid_skb->data;
		ch->xid_th = (struct th_header *)ch->xid_skb->data;
		skb_put(ch->xid_skb, TH_HEADER_LENGTH);
		ch->xid = (struct xid2 *)skb_tail_pointer(ch->xid_skb);
		skb_put(ch->xid_skb, XID2_LENGTH);
		ch->xid_id = skb_tail_pointer(ch->xid_skb);
		ch->xid_skb->data = ch->xid_skb_data;
		skb_reset_tail_pointer(ch->xid_skb);
		ch->xid_skb->len = 0;

		memcpy(skb_put(ch->xid_skb, grp->xid_skb->len),
				grp->xid_skb->data,
				grp->xid_skb->len);

		ch->xid->xid2_dlc_type = ((CHANNEL_DIRECTION(ch->flags) == READ)
				? XID2_READ_SIDE : XID2_WRITE_SIDE);

		if (CHANNEL_DIRECTION(ch->flags) == WRITE)
			ch->xid->xid2_buf_len = 0x00;

		ch->xid_skb->data = ch->xid_skb_data;
		skb_reset_tail_pointer(ch->xid_skb);
		ch->xid_skb->len = 0;

		fsm_newstate(ch->fsm, CH_XID0_PENDING);

		if ((grp->active_channels[READ]  > 0) &&
		    (grp->active_channels[WRITE] > 0) &&
			(fsm_getstate(grp->fsm) < MPCG_STATE_XID2INITW)) {
			fsm_newstate(grp->fsm, MPCG_STATE_XID2INITW);
			printk(KERN_NOTICE "ctcmpc: %s MPC GROUP "
					"CHANNELS ACTIVE\n", dev->name);
		}
	} else if ((action == MPC_CHANNEL_REMOVE) &&
			(ch->in_mpcgroup == 1)) {
		ch->in_mpcgroup = 0;
		grp->num_channel_paths--;
		grp->active_channels[direction]--;

		if (ch->xid_skb != NULL)
			dev_kfree_skb_any(ch->xid_skb);
		ch->xid_skb = NULL;

		if (grp->channels_terminating)
					goto done;

		if (((grp->active_channels[READ] == 0) &&
					(grp->active_channels[WRITE] > 0))
			|| ((grp->active_channels[WRITE] == 0) &&
					(grp->active_channels[READ] > 0)))
			fsm_event(grp->fsm, MPCG_EVENT_INOP, dev);
	}

done:

	if (do_debug) {
		ctcm_pr_debug(
		       "ctcmpc: %s() %i Grp:%s ttl_chan_paths=%i "
		       "active_chans read=%i, write=%i\n",
		       __FUNCTION__,
		       action,
		       fsm_getstate_str(grp->fsm),
		       grp->num_channel_paths,
		       grp->active_channels[READ],
		       grp->active_channels[WRITE]);

		ctcm_pr_debug("ctcmpc exit : %s(): ch=0x%p id=%s\n",
				__FUNCTION__, ch, ch->id);
	}
	return rc;

}

/**
 * Unpack a just received skb and hand it over to
 * upper layers.
 * special MPC version of unpack_skb.
 *
 * ch		The channel where this skb has been received.
 * pskb		The received skb.
 */
static void ctcmpc_unpack_skb(struct channel *ch, struct sk_buff *pskb)
{
	struct net_device *dev	= ch->netdev;
	struct ctcm_priv *priv = dev->priv;
	struct mpc_group *grp = priv->mpcg;
	struct pdu *curr_pdu;
	struct mpcg_info *mpcginfo;
	struct th_header *header = NULL;
	struct th_sweep *sweep = NULL;
	int pdu_last_seen = 0;
	__u32 new_len;
	struct sk_buff *skb;
	int skblen;
	int sendrc = 0;

	if (do_debug)
		ctcm_pr_debug("ctcmpc enter: %s() %s cp:%i ch:%s\n",
		       __FUNCTION__, dev->name, smp_processor_id(), ch->id);

	header = (struct th_header *)pskb->data;
	if ((header->th_seg == 0) &&
		(header->th_ch_flag == 0) &&
		(header->th_blk_flag == 0) &&
		(header->th_seq_num == 0))
		/* nothing for us */	goto done;

	if (do_debug_data) {
		ctcm_pr_debug("ctcmpc: %s() th_header\n", __FUNCTION__);
		ctcmpc_dumpit((char *)header, TH_HEADER_LENGTH);
		ctcm_pr_debug("ctcmpc: %s() pskb len: %04x \n",
		       __FUNCTION__, pskb->len);
	}

	pskb->dev = dev;
	pskb->ip_summed = CHECKSUM_UNNECESSARY;
	skb_pull(pskb, TH_HEADER_LENGTH);

	if (likely(header->th_ch_flag == TH_HAS_PDU)) {
		if (do_debug_data)
			ctcm_pr_debug("ctcmpc: %s() came into th_has_pdu\n",
			       __FUNCTION__);
		if ((fsm_getstate(grp->fsm) == MPCG_STATE_FLOWC) ||
		   ((fsm_getstate(grp->fsm) == MPCG_STATE_READY) &&
		    (header->th_seq_num != ch->th_seq_num + 1) &&
		    (ch->th_seq_num != 0))) {
			/* This is NOT the next segment		*
			 * we are not the correct race winner	*
			 * go away and let someone else win	*
			 * BUT..this only applies if xid negot	*
			 * is done				*
			*/
			grp->out_of_sequence += 1;
			__skb_push(pskb, TH_HEADER_LENGTH);
			skb_queue_tail(&ch->io_queue, pskb);
			if (do_debug_data)
				ctcm_pr_debug("ctcmpc: %s() th_seq_num "
				       "expect:%08x got:%08x\n", __FUNCTION__,
				       ch->th_seq_num + 1, header->th_seq_num);

			return;
		}
		grp->out_of_sequence = 0;
		ch->th_seq_num = header->th_seq_num;

		if (do_debug_data)
			ctcm_pr_debug("ctcmpc: %s() FromVTAM_th_seq=%08x\n",
			       __FUNCTION__, ch->th_seq_num);

		if (unlikely(fsm_getstate(grp->fsm) != MPCG_STATE_READY))
					goto done;
		pdu_last_seen = 0;
		while ((pskb->len > 0) && !pdu_last_seen) {
			curr_pdu = (struct pdu *)pskb->data;
			if (do_debug_data) {
				ctcm_pr_debug("ctcm: %s() pdu_header\n",
				       __FUNCTION__);
				ctcmpc_dumpit((char *)pskb->data,
						PDU_HEADER_LENGTH);
				ctcm_pr_debug("ctcm: %s() pskb len: %04x \n",
				       __FUNCTION__, pskb->len);
			}
			skb_pull(pskb, PDU_HEADER_LENGTH);

			if (curr_pdu->pdu_flag & PDU_LAST)
				pdu_last_seen = 1;
			if (curr_pdu->pdu_flag & PDU_CNTL)
				pskb->protocol = htons(ETH_P_SNAP);
			else
				pskb->protocol = htons(ETH_P_SNA_DIX);

			if ((pskb->len <= 0) || (pskb->len > ch->max_bufsize)) {
				printk(KERN_INFO
				       "%s Illegal packet size %d "
				       "received "
				       "dropping\n", dev->name,
				       pskb->len);
				priv->stats.rx_dropped++;
				priv->stats.rx_length_errors++;
					goto done;
			}
			skb_reset_mac_header(pskb);
			new_len = curr_pdu->pdu_offset;
			if (do_debug_data)
				ctcm_pr_debug("ctcmpc: %s() new_len: %04x \n",
				       __FUNCTION__, new_len);
			if ((new_len == 0) || (new_len > pskb->len)) {
				/* should never happen		    */
				/* pskb len must be hosed...bail out */
				printk(KERN_INFO
				       "ctcmpc: %s(): invalid pdu"
				       " offset of %04x - data may be"
				       "lost\n", __FUNCTION__, new_len);
						goto done;
			}
			skb = __dev_alloc_skb(new_len+4, GFP_ATOMIC);

			if (!skb) {
				printk(KERN_INFO
				       "ctcm: %s Out of memory in "
				       "%s()- request-len:%04x \n",
				       dev->name,
				       __FUNCTION__,
				       new_len+4);
				priv->stats.rx_dropped++;
				fsm_event(grp->fsm,
					  MPCG_EVENT_INOP, dev);
						goto done;
			}

			memcpy(skb_put(skb, new_len),
					pskb->data, new_len);

			skb_reset_mac_header(skb);
			skb->dev = pskb->dev;
			skb->protocol = pskb->protocol;
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			*((__u32 *) skb_push(skb, 4)) = ch->pdu_seq;
			ch->pdu_seq++;

			if (do_debug_data)
				ctcm_pr_debug("%s: ToDCM_pdu_seq= %08x\n",
				       __FUNCTION__, ch->pdu_seq);

			ctcm_pr_debug("ctcm: %s() skb:%0lx "
				"skb len: %d \n", __FUNCTION__,
			       (unsigned long)skb, skb->len);
			if (do_debug_data) {
				ctcm_pr_debug("ctcmpc: %s() up to 32 bytes"
					       " of pdu_data sent\n",
					       __FUNCTION__);
				ctcmpc_dump32((char *)skb->data, skb->len);
			}

			skblen = skb->len;
			sendrc = netif_rx(skb);
			priv->stats.rx_packets++;
			priv->stats.rx_bytes += skblen;
			skb_pull(pskb, new_len); /* point to next PDU */
		}
	} else {
		mpcginfo = (struct mpcg_info *)
				kmalloc(sizeof(struct mpcg_info), gfp_type());
		if (mpcginfo == NULL)
					goto done;

		mpcginfo->ch = ch;
		mpcginfo->th = header;
		mpcginfo->skb = pskb;
		ctcm_pr_debug("ctcmpc: %s() Not PDU - may be control pkt\n",
			       __FUNCTION__);
		/*  it's a sweep?   */
		sweep = (struct th_sweep *)pskb->data;
		mpcginfo->sweep = sweep;
		if (header->th_ch_flag == TH_SWEEP_REQ)
			mpc_rcvd_sweep_req(mpcginfo);
		else if (header->th_ch_flag == TH_SWEEP_RESP)
			mpc_rcvd_sweep_resp(mpcginfo);
		else if (header->th_blk_flag == TH_DATA_IS_XID) {
			struct xid2 *thisxid = (struct xid2 *)pskb->data;
			skb_pull(pskb, XID2_LENGTH);
			mpcginfo->xid = thisxid;
			fsm_event(grp->fsm, MPCG_EVENT_XID2, mpcginfo);
		} else if (header->th_blk_flag == TH_DISCONTACT)
			fsm_event(grp->fsm, MPCG_EVENT_DISCONC, mpcginfo);
		else if (header->th_seq_num != 0) {
			printk(KERN_INFO "%s unexpected packet"
					" expected control pkt\n", dev->name);
			priv->stats.rx_dropped++;
			/* mpcginfo only used for non-data transfers */
			kfree(mpcginfo);
			if (do_debug_data)
				ctcmpc_dump_skb(pskb, -8);
		}
	}
done:

	dev_kfree_skb_any(pskb);
	if (sendrc == NET_RX_DROP) {
		printk(KERN_WARNING "%s %s() NETWORK BACKLOG EXCEEDED"
		       " - PACKET DROPPED\n", dev->name, __FUNCTION__);
		fsm_event(grp->fsm, MPCG_EVENT_INOP, dev);
	}

	if (do_debug)
		ctcm_pr_debug("ctcmpc exit : %s %s(): ch=0x%p id=%s\n",
				dev->name, __FUNCTION__, ch, ch->id);
}

/**
 * tasklet helper for mpc's skb unpacking.
 *
 * ch		The channel to work on.
 * Allow flow control back pressure to occur here.
 * Throttling back channel can result in excessive
 * channel inactivity and system deact of channel
 */
void ctcmpc_bh(unsigned long thischan)
{
	struct channel	  *ch	    = (struct channel *)thischan;
	struct sk_buff	  *skb;
	struct net_device *dev	    = ch->netdev;
	struct ctcm_priv  *priv  = dev->priv;
	struct mpc_group  *grp   = priv->mpcg;

	if (do_debug)
		ctcm_pr_debug("%s cp:%i enter:  %s() %s\n",
		       dev->name, smp_processor_id(), __FUNCTION__, ch->id);
	/* caller has requested driver to throttle back */
	while ((fsm_getstate(grp->fsm) != MPCG_STATE_FLOWC) &&
			(skb = skb_dequeue(&ch->io_queue))) {
		ctcmpc_unpack_skb(ch, skb);
		if (grp->out_of_sequence > 20) {
			/* assume data loss has occurred if */
			/* missing seq_num for extended     */
			/* period of time		    */
			grp->out_of_sequence = 0;
			fsm_event(grp->fsm, MPCG_EVENT_INOP, dev);
			break;
		}
		if (skb == skb_peek(&ch->io_queue))
			break;
	}
	if (do_debug)
		ctcm_pr_debug("ctcmpc exit : %s %s(): ch=0x%p id=%s\n",
			dev->name, __FUNCTION__, ch,  ch->id);
	return;
}

/*
 *  MPC Group Initializations
 */
struct mpc_group *ctcmpc_init_mpc_group(struct ctcm_priv *priv)
{
	struct mpc_group *grp;

	CTCM_DBF_TEXT(MPC_SETUP, 3, __FUNCTION__);

	grp = kzalloc(sizeof(struct mpc_group), GFP_KERNEL);
	if (grp == NULL)
		return NULL;

	grp->fsm =
		init_fsm("mpcg", mpcg_state_names, mpcg_event_names,
				 MPCG_NR_STATES, MPCG_NR_EVENTS, mpcg_fsm,
				 mpcg_fsm_len, GFP_KERNEL);
	if (grp->fsm == NULL) {
		kfree(grp);
		return NULL;
	}

	fsm_newstate(grp->fsm, MPCG_STATE_RESET);
	fsm_settimer(grp->fsm, &grp->timer);

	grp->xid_skb =
		 __dev_alloc_skb(MPC_BUFSIZE_DEFAULT, GFP_ATOMIC | GFP_DMA);
	if (grp->xid_skb == NULL) {
		printk(KERN_INFO "Couldn't alloc MPCgroup xid_skb\n");
		kfree_fsm(grp->fsm);
		kfree(grp);
		return NULL;
	}
	/*  base xid for all channels in group  */
	grp->xid_skb_data = grp->xid_skb->data;
	grp->xid_th = (struct th_header *)grp->xid_skb->data;
	memcpy(skb_put(grp->xid_skb, TH_HEADER_LENGTH),
			&thnorm, TH_HEADER_LENGTH);

	grp->xid = (struct xid2 *) skb_tail_pointer(grp->xid_skb);
	memcpy(skb_put(grp->xid_skb, XID2_LENGTH), &init_xid, XID2_LENGTH);
	grp->xid->xid2_adj_id = jiffies | 0xfff00000;
	grp->xid->xid2_sender_id = jiffies;

	grp->xid_id = skb_tail_pointer(grp->xid_skb);
	memcpy(skb_put(grp->xid_skb, 4), "VTAM", 4);

	grp->rcvd_xid_skb =
		__dev_alloc_skb(MPC_BUFSIZE_DEFAULT, GFP_ATOMIC|GFP_DMA);
	if (grp->rcvd_xid_skb == NULL) {
		printk(KERN_INFO "Couldn't alloc MPCgroup rcvd_xid_skb\n");
		kfree_fsm(grp->fsm);
		dev_kfree_skb(grp->xid_skb);
		kfree(grp);
		return NULL;
	}
	grp->rcvd_xid_data = grp->rcvd_xid_skb->data;
	grp->rcvd_xid_th = (struct th_header *)grp->rcvd_xid_skb->data;
	memcpy(skb_put(grp->rcvd_xid_skb, TH_HEADER_LENGTH),
			&thnorm, TH_HEADER_LENGTH);
	grp->saved_xid2 = NULL;
	priv->xid = grp->xid;
	priv->mpcg = grp;
	return grp;
}

/*
 * The MPC Group Station FSM
 */

/*
 * MPC Group Station FSM actions
 * CTCM_PROTO_MPC only
 */

/**
 * NOP action for statemachines
 */
static void mpc_action_nop(fsm_instance *fi, int event, void *arg)
{
}

/*
 * invoked when the device transitions to dev_stopped
 * MPC will stop each individual channel if a single XID failure
 * occurs, or will intitiate all channels be stopped if a GROUP
 * level failure occurs.
 */
static void mpc_action_go_inop(fsm_instance *fi, int event, void *arg)
{
	struct net_device  *dev = arg;
	struct ctcm_priv    *priv;
	struct mpc_group *grp;
	int rc = 0;
	struct channel *wch, *rch;

	if (dev == NULL) {
		printk(KERN_INFO "%s() dev=NULL\n", __FUNCTION__);
		return;
	}

	ctcm_pr_debug("ctcmpc enter: %s  %s()\n", dev->name, __FUNCTION__);

	priv  = dev->priv;
	grp =  priv->mpcg;
	grp->flow_off_called = 0;

	fsm_deltimer(&grp->timer);

	if (grp->channels_terminating)
					goto done;

	grp->channels_terminating = 1;

	grp->saved_state = fsm_getstate(grp->fsm);
	fsm_newstate(grp->fsm, MPCG_STATE_INOP);
	if (grp->saved_state > MPCG_STATE_XID7INITF)
		printk(KERN_NOTICE "%s:MPC GROUP INOPERATIVE\n", dev->name);
	if ((grp->saved_state != MPCG_STATE_RESET) ||
		/* dealloc_channel has been called */
		((grp->saved_state == MPCG_STATE_RESET) &&
				(grp->port_persist == 0)))
		fsm_deltimer(&priv->restart_timer);

	wch = priv->channel[WRITE];
	rch = priv->channel[READ];

	switch (grp->saved_state) {
	case MPCG_STATE_RESET:
	case MPCG_STATE_INOP:
	case MPCG_STATE_XID2INITW:
	case MPCG_STATE_XID0IOWAIT:
	case MPCG_STATE_XID2INITX:
	case MPCG_STATE_XID7INITW:
	case MPCG_STATE_XID7INITX:
	case MPCG_STATE_XID0IOWAIX:
	case MPCG_STATE_XID7INITI:
	case MPCG_STATE_XID7INITZ:
	case MPCG_STATE_XID7INITF:
		break;
	case MPCG_STATE_FLOWC:
	case MPCG_STATE_READY:
	default:
		tasklet_hi_schedule(&wch->ch_disc_tasklet);
	}

	grp->xid2_tgnum = 0;
	grp->group_max_buflen = 0;  /*min of all received */
	grp->outstanding_xid2 = 0;
	grp->outstanding_xid7 = 0;
	grp->outstanding_xid7_p2 = 0;
	grp->saved_xid2 = NULL;
	grp->xidnogood = 0;
	grp->changed_side = 0;

	grp->rcvd_xid_skb->data = grp->rcvd_xid_data;
	skb_reset_tail_pointer(grp->rcvd_xid_skb);
	grp->rcvd_xid_skb->len = 0;
	grp->rcvd_xid_th = (struct th_header *)grp->rcvd_xid_skb->data;
	memcpy(skb_put(grp->rcvd_xid_skb, TH_HEADER_LENGTH), &thnorm,
	       TH_HEADER_LENGTH);

	if (grp->send_qllc_disc == 1) {
		grp->send_qllc_disc = 0;
		rc = mpc_send_qllc_discontact(dev);
	}

	/* DO NOT issue DEV_EVENT_STOP directly out of this code */
	/* This can result in INOP of VTAM PU due to halting of  */
	/* outstanding IO which causes a sense to be returned	 */
	/* Only about 3 senses are allowed and then IOS/VTAM will*/
	/* ebcome unreachable without manual intervention	 */
	if ((grp->port_persist == 1)	|| (grp->alloc_called)) {
		grp->alloc_called = 0;
		fsm_deltimer(&priv->restart_timer);
		fsm_addtimer(&priv->restart_timer,
			     500,
			     DEV_EVENT_RESTART,
			     dev);
		fsm_newstate(grp->fsm, MPCG_STATE_RESET);
		if (grp->saved_state > MPCG_STATE_XID7INITF)
			printk(KERN_NOTICE "%s:MPC GROUP RECOVERY SCHEDULED\n",
			       dev->name);
	} else {
		fsm_deltimer(&priv->restart_timer);
		fsm_addtimer(&priv->restart_timer, 500, DEV_EVENT_STOP, dev);
		fsm_newstate(grp->fsm, MPCG_STATE_RESET);
		printk(KERN_NOTICE "%s:MPC GROUP RECOVERY NOT ATTEMPTED\n",
		       dev->name);
	}

done:
	ctcm_pr_debug("ctcmpc exit:%s  %s()\n", dev->name, __FUNCTION__);
	return;
}

/**
 * Handle mpc group  action timeout.
 * MPC Group Station FSM action
 * CTCM_PROTO_MPC only
 *
 * fi		An instance of an mpc_group fsm.
 * event	The event, just happened.
 * arg		Generic pointer, casted from net_device * upon call.
 */
static void mpc_action_timeout(fsm_instance *fi, int event, void *arg)
{
	struct net_device *dev = arg;
	struct ctcm_priv *priv;
	struct mpc_group *grp;
	struct channel *wch;
	struct channel *rch;

	CTCM_DBF_TEXT(MPC_TRACE, 6, __FUNCTION__);

	if (dev == NULL) {
		CTCM_DBF_TEXT_(MPC_ERROR, 4, "%s: dev=NULL\n", __FUNCTION__);
		return;
	}

	priv = dev->priv;
	grp = priv->mpcg;
	wch = priv->channel[WRITE];
	rch = priv->channel[READ];

	switch (fsm_getstate(grp->fsm)) {
	case MPCG_STATE_XID2INITW:
		/* Unless there is outstanding IO on the  */
		/* channel just return and wait for ATTN  */
		/* interrupt to begin XID negotiations	  */
		if ((fsm_getstate(rch->fsm) == CH_XID0_PENDING) &&
		   (fsm_getstate(wch->fsm) == CH_XID0_PENDING))
			break;
	default:
		fsm_event(grp->fsm, MPCG_EVENT_INOP, dev);
	}

	CTCM_DBF_TEXT_(MPC_TRACE, 6, "%s: dev=%s exit",
					__FUNCTION__, dev->name);
	return;
}

/*
 * MPC Group Station FSM action
 * CTCM_PROTO_MPC only
 */
void mpc_action_discontact(fsm_instance *fi, int event, void *arg)
{
	struct mpcg_info   *mpcginfo   = arg;
	struct channel	   *ch	       = mpcginfo->ch;
	struct net_device  *dev        = ch->netdev;
	struct ctcm_priv   *priv    = dev->priv;
	struct mpc_group   *grp     = priv->mpcg;

	if (ch == NULL)	{
		printk(KERN_INFO "%s() ch=NULL\n", __FUNCTION__);
		return;
	}
	if (ch->netdev == NULL)	{
		printk(KERN_INFO "%s() dev=NULL\n", __FUNCTION__);
		return;
	}

	ctcm_pr_debug("ctcmpc enter: %s  %s()\n", dev->name, __FUNCTION__);

	grp->send_qllc_disc = 1;
	fsm_event(grp->fsm, MPCG_EVENT_INOP, dev);

	ctcm_pr_debug("ctcmpc exit: %s  %s()\n", dev->name, __FUNCTION__);
	return;
}

/*
 * MPC Group Station - not part of FSM
 * CTCM_PROTO_MPC only
 * called from add_channel in ctcm_main.c
 */
void mpc_action_send_discontact(unsigned long thischan)
{
	struct channel	   *ch;
	struct net_device  *dev;
	struct ctcm_priv    *priv;
	struct mpc_group   *grp;
	int rc = 0;
	unsigned long	  saveflags;

	ch = (struct channel *)thischan;
	dev = ch->netdev;
	priv = dev->priv;
	grp = priv->mpcg;

	ctcm_pr_info("ctcmpc: %s cp:%i enter: %s() GrpState:%s ChState:%s\n",
		       dev->name,
		       smp_processor_id(),
		       __FUNCTION__,
		       fsm_getstate_str(grp->fsm),
		       fsm_getstate_str(ch->fsm));
	saveflags = 0;	/* avoids compiler warning with
			   spin_unlock_irqrestore */

	spin_lock_irqsave(get_ccwdev_lock(ch->cdev), saveflags);
	rc = ccw_device_start(ch->cdev, &ch->ccw[15],
					(unsigned long)ch, 0xff, 0);
	spin_unlock_irqrestore(get_ccwdev_lock(ch->cdev), saveflags);

	if (rc != 0) {
		ctcm_pr_info("ctcmpc: %s() ch:%s IO failed \n",
			       __FUNCTION__,
			       ch->id);
		ctcm_ccw_check_rc(ch, rc, "send discontact");
		/* Not checking return code value here */
		/* Making best effort to notify partner*/
		/* that MPC Group is going down        */
	}

	ctcm_pr_debug("ctcmpc exit: %s  %s()\n", dev->name, __FUNCTION__);
	return;
}


/*
 * helper function of mpc FSM
 * CTCM_PROTO_MPC only
 * mpc_action_rcvd_xid7
*/
static int mpc_validate_xid(struct mpcg_info *mpcginfo)
{
	struct channel	   *ch	    = mpcginfo->ch;
	struct net_device  *dev     = ch->netdev;
	struct ctcm_priv   *priv = dev->priv;
	struct mpc_group   *grp  = priv->mpcg;
	struct xid2	   *xid     = mpcginfo->xid;
	int	failed	= 0;
	int	rc	= 0;
	__u64	our_id, their_id = 0;
	int	len;

	len = TH_HEADER_LENGTH + PDU_HEADER_LENGTH;

	ctcm_pr_debug("ctcmpc enter:	%s()\n", __FUNCTION__);

	if (mpcginfo->xid == NULL) {
		printk(KERN_INFO "%s() xid=NULL\n", __FUNCTION__);
		rc = 1;
					goto done;
	}

	ctcm_pr_debug("ctcmpc :  %s  xid received()\n", __FUNCTION__);
	ctcmpc_dumpit((char *)mpcginfo->xid, XID2_LENGTH);

	/*the received direction should be the opposite of ours  */
	if (((CHANNEL_DIRECTION(ch->flags) == READ) ? XID2_WRITE_SIDE :
				XID2_READ_SIDE) != xid->xid2_dlc_type) {
		failed = 1;
		printk(KERN_INFO "ctcmpc:%s() XID REJECTED - READ-WRITE CH "
			"Pairing Invalid \n", __FUNCTION__);
	}

	if (xid->xid2_dlc_type == XID2_READ_SIDE) {
		ctcm_pr_debug("ctcmpc: %s(): grpmaxbuf:%d xid2buflen:%d\n",
				__FUNCTION__, grp->group_max_buflen,
				xid->xid2_buf_len);

		if (grp->group_max_buflen == 0 ||
			grp->group_max_buflen > xid->xid2_buf_len - len)
			grp->group_max_buflen = xid->xid2_buf_len - len;
	}


	if (grp->saved_xid2 == NULL)	{
		grp->saved_xid2 =
			(struct xid2 *)skb_tail_pointer(grp->rcvd_xid_skb);

		memcpy(skb_put(grp->rcvd_xid_skb,
					XID2_LENGTH), xid, XID2_LENGTH);
		grp->rcvd_xid_skb->data = grp->rcvd_xid_data;

		skb_reset_tail_pointer(grp->rcvd_xid_skb);
		grp->rcvd_xid_skb->len = 0;

		/* convert two 32 bit numbers into 1 64 bit for id compare */
		our_id = (__u64)priv->xid->xid2_adj_id;
		our_id = our_id << 32;
		our_id = our_id + priv->xid->xid2_sender_id;
		their_id = (__u64)xid->xid2_adj_id;
		their_id = their_id << 32;
		their_id = their_id + xid->xid2_sender_id;
		/* lower id assume the xside role */
		if (our_id < their_id) {
			grp->roll = XSIDE;
			ctcm_pr_debug("ctcmpc :%s() WE HAVE LOW ID-"
				       "TAKE XSIDE\n", __FUNCTION__);
		} else {
			grp->roll = YSIDE;
			ctcm_pr_debug("ctcmpc :%s() WE HAVE HIGH ID-"
				       "TAKE YSIDE\n", __FUNCTION__);
		}

	} else {
		if (xid->xid2_flag4 != grp->saved_xid2->xid2_flag4) {
			failed = 1;
			printk(KERN_INFO "%s XID REJECTED - XID Flag Byte4\n",
			       __FUNCTION__);
		}
		if (xid->xid2_flag2 == 0x40) {
			failed = 1;
			printk(KERN_INFO "%s XID REJECTED - XID NOGOOD\n",
			       __FUNCTION__);
		}
		if (xid->xid2_adj_id != grp->saved_xid2->xid2_adj_id) {
			failed = 1;
			printk(KERN_INFO "%s XID REJECTED - "
				"Adjacent Station ID Mismatch\n",
				__FUNCTION__);
		}
		if (xid->xid2_sender_id != grp->saved_xid2->xid2_sender_id) {
			failed = 1;
			printk(KERN_INFO "%s XID REJECTED - "
				"Sender Address Mismatch\n", __FUNCTION__);

		}
	}

	if (failed) {
		ctcm_pr_info("ctcmpc	   :  %s() failed\n", __FUNCTION__);
		priv->xid->xid2_flag2 = 0x40;
		grp->saved_xid2->xid2_flag2 = 0x40;
		rc = 1;
	}

done:

	ctcm_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
	return rc;
}

/*
 * MPC Group Station FSM action
 * CTCM_PROTO_MPC only
 */
static void mpc_action_side_xid(fsm_instance *fsm, void *arg, int side)
{
	struct channel *ch = arg;
	struct ctcm_priv *priv;
	struct mpc_group *grp = NULL;
	struct net_device *dev = NULL;
	int rc = 0;
	int gotlock = 0;
	unsigned long saveflags = 0;	/* avoids compiler warning with
			   spin_unlock_irqrestore */

	if (ch == NULL)	{
		printk(KERN_INFO "%s ch=NULL\n", __FUNCTION__);
					goto done;
	}

	if (do_debug)
		ctcm_pr_debug("ctcmpc enter: %s(): cp=%i ch=0x%p id=%s\n",
			__FUNCTION__, smp_processor_id(), ch, ch->id);

	dev = ch->netdev;
	if (dev == NULL) {
		printk(KERN_INFO "%s dev=NULL\n", __FUNCTION__);
					goto done;
	}

	priv = dev->priv;
	if (priv == NULL) {
		printk(KERN_INFO "%s priv=NULL\n", __FUNCTION__);
					goto done;
	}

	grp = priv->mpcg;
	if (grp == NULL) {
		printk(KERN_INFO "%s grp=NULL\n", __FUNCTION__);
					goto done;
	}

	if (ctcm_checkalloc_buffer(ch))
					goto done;

	/* skb data-buffer referencing: */

	ch->trans_skb->data = ch->trans_skb_data;
	skb_reset_tail_pointer(ch->trans_skb);
	ch->trans_skb->len = 0;
	/* result of the previous 3 statements is NOT always
	 * already set after ctcm_checkalloc_buffer
	 * because of possible reuse of the trans_skb
	 */
	memset(ch->trans_skb->data, 0, 16);
	ch->rcvd_xid_th =  (struct th_header *)ch->trans_skb_data;
	/* check is main purpose here: */
	skb_put(ch->trans_skb, TH_HEADER_LENGTH);
	ch->rcvd_xid = (struct xid2 *)skb_tail_pointer(ch->trans_skb);
	/* check is main purpose here: */
	skb_put(ch->trans_skb, XID2_LENGTH);
	ch->rcvd_xid_id = skb_tail_pointer(ch->trans_skb);
	/* cleanup back to startpoint */
	ch->trans_skb->data = ch->trans_skb_data;
	skb_reset_tail_pointer(ch->trans_skb);
	ch->trans_skb->len = 0;

	/* non-checking rewrite of above skb data-buffer referencing: */
	/*
	memset(ch->trans_skb->data, 0, 16);
	ch->rcvd_xid_th =  (struct th_header *)ch->trans_skb_data;
	ch->rcvd_xid = (struct xid2 *)(ch->trans_skb_data + TH_HEADER_LENGTH);
	ch->rcvd_xid_id = ch->trans_skb_data + TH_HEADER_LENGTH + XID2_LENGTH;
	 */

	ch->ccw[8].flags	= CCW_FLAG_SLI | CCW_FLAG_CC;
	ch->ccw[8].count	= 0;
	ch->ccw[8].cda		= 0x00;

	if (side == XSIDE) {
		/* mpc_action_xside_xid */
		if (ch->xid_th == NULL) {
			printk(KERN_INFO "%s ch->xid_th=NULL\n", __FUNCTION__);
					goto done;
		}
		ch->ccw[9].cmd_code	= CCW_CMD_WRITE;
		ch->ccw[9].flags	= CCW_FLAG_SLI | CCW_FLAG_CC;
		ch->ccw[9].count	= TH_HEADER_LENGTH;
		ch->ccw[9].cda		= virt_to_phys(ch->xid_th);

		if (ch->xid == NULL) {
			printk(KERN_INFO "%s ch->xid=NULL\n", __FUNCTION__);
					goto done;
		}

		ch->ccw[10].cmd_code	= CCW_CMD_WRITE;
		ch->ccw[10].flags	= CCW_FLAG_SLI | CCW_FLAG_CC;
		ch->ccw[10].count	= XID2_LENGTH;
		ch->ccw[10].cda		= virt_to_phys(ch->xid);

		ch->ccw[11].cmd_code	= CCW_CMD_READ;
		ch->ccw[11].flags	= CCW_FLAG_SLI | CCW_FLAG_CC;
		ch->ccw[11].count	= TH_HEADER_LENGTH;
		ch->ccw[11].cda		= virt_to_phys(ch->rcvd_xid_th);

		ch->ccw[12].cmd_code	= CCW_CMD_READ;
		ch->ccw[12].flags	= CCW_FLAG_SLI | CCW_FLAG_CC;
		ch->ccw[12].count	= XID2_LENGTH;
		ch->ccw[12].cda		= virt_to_phys(ch->rcvd_xid);

		ch->ccw[13].cmd_code	= CCW_CMD_READ;
		ch->ccw[13].cda		= virt_to_phys(ch->rcvd_xid_id);

	} else { /* side == YSIDE : mpc_action_yside_xid */
		ch->ccw[9].cmd_code	= CCW_CMD_READ;
		ch->ccw[9].flags	= CCW_FLAG_SLI | CCW_FLAG_CC;
		ch->ccw[9].count	= TH_HEADER_LENGTH;
		ch->ccw[9].cda		= virt_to_phys(ch->rcvd_xid_th);

		ch->ccw[10].cmd_code	= CCW_CMD_READ;
		ch->ccw[10].flags	= CCW_FLAG_SLI | CCW_FLAG_CC;
		ch->ccw[10].count	= XID2_LENGTH;
		ch->ccw[10].cda		= virt_to_phys(ch->rcvd_xid);

		if (ch->xid_th == NULL)	{
			printk(KERN_INFO "%s ch->xid_th=NULL\n", __FUNCTION__);
					goto done;
		}
		ch->ccw[11].cmd_code	= CCW_CMD_WRITE;
		ch->ccw[11].flags	= CCW_FLAG_SLI | CCW_FLAG_CC;
		ch->ccw[11].count	= TH_HEADER_LENGTH;
		ch->ccw[11].cda		= virt_to_phys(ch->xid_th);

		if (ch->xid == NULL) {
			printk(KERN_INFO "%s ch->xid=NULL\n", __FUNCTION__);
					goto done;
		}
		ch->ccw[12].cmd_code	= CCW_CMD_WRITE;
		ch->ccw[12].flags	= CCW_FLAG_SLI | CCW_FLAG_CC;
		ch->ccw[12].count	= XID2_LENGTH;
		ch->ccw[12].cda		= virt_to_phys(ch->xid);

		if (ch->xid_id == NULL)	{
			printk(KERN_INFO "%s ch->xid_id=NULL\n", __FUNCTION__);
					goto done;
		}
		ch->ccw[13].cmd_code	= CCW_CMD_WRITE;
		ch->ccw[13].cda		= virt_to_phys(ch->xid_id);

	}
	ch->ccw[13].flags	= CCW_FLAG_SLI | CCW_FLAG_CC;
	ch->ccw[13].count	= 4;

	ch->ccw[14].cmd_code	= CCW_CMD_NOOP;
	ch->ccw[14].flags	= CCW_FLAG_SLI;
	ch->ccw[14].count	= 0;
	ch->ccw[14].cda		= 0;

	if (do_debug_ccw)
		ctcmpc_dumpit((char *)&ch->ccw[8], sizeof(struct ccw1) * 7);

	ctcmpc_dumpit((char *)ch->xid_th, TH_HEADER_LENGTH);
	ctcmpc_dumpit((char *)ch->xid, XID2_LENGTH);
	ctcmpc_dumpit((char *)ch->xid_id, 4);
	if (!in_irq()) {
			 /* Such conditional locking is a known problem for
			  * sparse because its static undeterministic.
			  * Warnings should be ignored here. */
		spin_lock_irqsave(get_ccwdev_lock(ch->cdev), saveflags);
		gotlock = 1;
	}

	fsm_addtimer(&ch->timer, 5000 , CTC_EVENT_TIMER, ch);
	rc = ccw_device_start(ch->cdev, &ch->ccw[8],
				(unsigned long)ch, 0xff, 0);

	if (gotlock)	/* see remark above about conditional locking */
		spin_unlock_irqrestore(get_ccwdev_lock(ch->cdev), saveflags);

	if (rc != 0) {
		ctcm_pr_info("ctcmpc: %s() ch:%s IO failed \n",
				__FUNCTION__, ch->id);
		ctcm_ccw_check_rc(ch, rc,
				(side == XSIDE) ? "x-side XID" : "y-side XID");
	}

done:
	if (do_debug)
		ctcm_pr_debug("ctcmpc exit : %s(): ch=0x%p id=%s\n",
				__FUNCTION__, ch, ch->id);
	return;

}

/*
 * MPC Group Station FSM action
 * CTCM_PROTO_MPC only
 */
static void mpc_action_xside_xid(fsm_instance *fsm, int event, void *arg)
{
	mpc_action_side_xid(fsm, arg, XSIDE);
}

/*
 * MPC Group Station FSM action
 * CTCM_PROTO_MPC only
 */
static void mpc_action_yside_xid(fsm_instance *fsm, int event, void *arg)
{
	mpc_action_side_xid(fsm, arg, YSIDE);
}

/*
 * MPC Group Station FSM action
 * CTCM_PROTO_MPC only
 */
static void mpc_action_doxid0(fsm_instance *fsm, int event, void *arg)
{
	struct channel	   *ch = arg;
	struct ctcm_priv    *priv;
	struct mpc_group   *grp     = NULL;
	struct net_device *dev = NULL;

	if (do_debug)
		ctcm_pr_debug("ctcmpc enter: %s(): cp=%i ch=0x%p id=%s\n",
			__FUNCTION__, smp_processor_id(), ch, ch->id);

	if (ch == NULL) {
		printk(KERN_WARNING "%s ch=NULL\n", __FUNCTION__);
					goto done;
	}

	dev = ch->netdev;
	if (dev == NULL) {
		printk(KERN_WARNING "%s dev=NULL\n", __FUNCTION__);
					goto done;
	}

	priv = dev->priv;
	if (priv == NULL) {
		printk(KERN_WARNING "%s priv=NULL\n", __FUNCTION__);
					goto done;
	}

	grp = priv->mpcg;
	if (grp == NULL) {
		printk(KERN_WARNING "%s grp=NULL\n", __FUNCTION__);
					goto done;
	}

	if (ch->xid == NULL) {
		printk(KERN_WARNING "%s ch-xid=NULL\n", __FUNCTION__);
					goto done;
	}

	fsm_newstate(ch->fsm, CH_XID0_INPROGRESS);

	ch->xid->xid2_option =	XID2_0;

	switch (fsm_getstate(grp->fsm)) {
	case MPCG_STATE_XID2INITW:
	case MPCG_STATE_XID2INITX:
		ch->ccw[8].cmd_code = CCW_CMD_SENSE_CMD;
		break;
	case MPCG_STATE_XID0IOWAIT:
	case MPCG_STATE_XID0IOWAIX:
		ch->ccw[8].cmd_code = CCW_CMD_WRITE_CTL;
		break;
	}

	fsm_event(grp->fsm, MPCG_EVENT_DOIO, ch);

done:
	if (do_debug)
		ctcm_pr_debug("ctcmpc exit : %s(): ch=0x%p id=%s\n",
			__FUNCTION__, ch, ch->id);
	return;

}

/*
 * MPC Group Station FSM action
 * CTCM_PROTO_MPC only
*/
static void mpc_action_doxid7(fsm_instance *fsm, int event, void *arg)
{
	struct net_device *dev = arg;
	struct ctcm_priv   *priv = NULL;
	struct mpc_group  *grp = NULL;
	int direction;
	int rc = 0;
	int send = 0;

	ctcm_pr_debug("ctcmpc enter:	%s() \n", __FUNCTION__);

	if (dev == NULL) {
		printk(KERN_INFO "%s dev=NULL \n", __FUNCTION__);
		rc = 1;
					goto done;
	}

	priv = dev->priv;
	if (priv == NULL) {
		printk(KERN_INFO "%s priv=NULL \n", __FUNCTION__);
		rc = 1;
					goto done;
	}

	grp = priv->mpcg;
	if (grp == NULL) {
		printk(KERN_INFO "%s grp=NULL \n", __FUNCTION__);
		rc = 1;
					goto done;
	}

	for (direction = READ; direction <= WRITE; direction++)	{
		struct channel *ch = priv->channel[direction];
		struct xid2 *thisxid = ch->xid;
		ch->xid_skb->data = ch->xid_skb_data;
		skb_reset_tail_pointer(ch->xid_skb);
		ch->xid_skb->len = 0;
		thisxid->xid2_option = XID2_7;
		send = 0;

		/* xid7 phase 1 */
		if (grp->outstanding_xid7_p2 > 0) {
			if (grp->roll == YSIDE) {
				if (fsm_getstate(ch->fsm) == CH_XID7_PENDING1) {
					fsm_newstate(ch->fsm, CH_XID7_PENDING2);
					ch->ccw[8].cmd_code = CCW_CMD_SENSE_CMD;
					memcpy(skb_put(ch->xid_skb,
							TH_HEADER_LENGTH),
					       &thdummy, TH_HEADER_LENGTH);
					send = 1;
				}
			} else if (fsm_getstate(ch->fsm) < CH_XID7_PENDING2) {
					fsm_newstate(ch->fsm, CH_XID7_PENDING2);
					ch->ccw[8].cmd_code = CCW_CMD_WRITE_CTL;
					memcpy(skb_put(ch->xid_skb,
						       TH_HEADER_LENGTH),
					       &thnorm, TH_HEADER_LENGTH);
					send = 1;
			}
		} else {
			/* xid7 phase 2 */
			if (grp->roll == YSIDE) {
				if (fsm_getstate(ch->fsm) < CH_XID7_PENDING4) {
					fsm_newstate(ch->fsm, CH_XID7_PENDING4);
					memcpy(skb_put(ch->xid_skb,
						       TH_HEADER_LENGTH),
					       &thnorm, TH_HEADER_LENGTH);
					ch->ccw[8].cmd_code = CCW_CMD_WRITE_CTL;
					send = 1;
				}
			} else if (fsm_getstate(ch->fsm) == CH_XID7_PENDING3) {
				fsm_newstate(ch->fsm, CH_XID7_PENDING4);
				ch->ccw[8].cmd_code = CCW_CMD_SENSE_CMD;
				memcpy(skb_put(ch->xid_skb, TH_HEADER_LENGTH),
						&thdummy, TH_HEADER_LENGTH);
				send = 1;
			}
		}

		if (send)
			fsm_event(grp->fsm, MPCG_EVENT_DOIO, ch);
	}

done:

	if (rc != 0)
		fsm_event(grp->fsm, MPCG_EVENT_INOP, dev);

	return;
}

/*
 * MPC Group Station FSM action
 * CTCM_PROTO_MPC only
 */
static void mpc_action_rcvd_xid0(fsm_instance *fsm, int event, void *arg)
{

	struct mpcg_info   *mpcginfo   = arg;
	struct channel	   *ch	       = mpcginfo->ch;
	struct net_device  *dev        = ch->netdev;
	struct ctcm_priv   *priv;
	struct mpc_group   *grp;

	if (do_debug)
		ctcm_pr_debug("ctcmpc enter: %s(): cp=%i ch=0x%p id=%s\n",
			__FUNCTION__, smp_processor_id(), ch, ch->id);

	priv = dev->priv;
	grp = priv->mpcg;

	ctcm_pr_debug("ctcmpc in:%s() %s xid2:%i xid7:%i xidt_p2:%i \n",
		       __FUNCTION__, ch->id,
		       grp->outstanding_xid2,
		       grp->outstanding_xid7,
		       grp->outstanding_xid7_p2);

	if (fsm_getstate(ch->fsm) < CH_XID7_PENDING)
		fsm_newstate(ch->fsm, CH_XID7_PENDING);

	grp->outstanding_xid2--;
	grp->outstanding_xid7++;
	grp->outstanding_xid7_p2++;

	/* must change state before validating xid to */
	/* properly handle interim interrupts received*/
	switch (fsm_getstate(grp->fsm)) {
	case MPCG_STATE_XID2INITW:
		fsm_newstate(grp->fsm, MPCG_STATE_XID2INITX);
		mpc_validate_xid(mpcginfo);
		break;
	case MPCG_STATE_XID0IOWAIT:
		fsm_newstate(grp->fsm, MPCG_STATE_XID0IOWAIX);
		mpc_validate_xid(mpcginfo);
		break;
	case MPCG_STATE_XID2INITX:
		if (grp->outstanding_xid2 == 0) {
			fsm_newstate(grp->fsm, MPCG_STATE_XID7INITW);
			mpc_validate_xid(mpcginfo);
			fsm_event(grp->fsm, MPCG_EVENT_XID2DONE, dev);
		}
		break;
	case MPCG_STATE_XID0IOWAIX:
		if (grp->outstanding_xid2 == 0) {
			fsm_newstate(grp->fsm, MPCG_STATE_XID7INITI);
			mpc_validate_xid(mpcginfo);
			fsm_event(grp->fsm, MPCG_EVENT_XID2DONE, dev);
		}
		break;
	}
	kfree(mpcginfo);

	if (do_debug) {
		ctcm_pr_debug("ctcmpc:%s() %s xid2:%i xid7:%i xidt_p2:%i \n",
				__FUNCTION__, ch->id,
				grp->outstanding_xid2,
				grp->outstanding_xid7,
				grp->outstanding_xid7_p2);
		ctcm_pr_debug("ctcmpc:%s() %s grpstate: %s chanstate: %s \n",
				__FUNCTION__, ch->id,
				fsm_getstate_str(grp->fsm),
				fsm_getstate_str(ch->fsm));
	}
	return;

}


/*
 * MPC Group Station FSM action
 * CTCM_PROTO_MPC only
 */
static void mpc_action_rcvd_xid7(fsm_instance *fsm, int event, void *arg)
{
	struct mpcg_info   *mpcginfo   = arg;
	struct channel	   *ch	       = mpcginfo->ch;
	struct net_device  *dev        = ch->netdev;
	struct ctcm_priv   *priv    = dev->priv;
	struct mpc_group   *grp     = priv->mpcg;

	if (do_debug) {
		ctcm_pr_debug("ctcmpc enter: %s(): cp=%i ch=0x%p id=%s\n",
				__FUNCTION__, smp_processor_id(), ch, ch->id);

		ctcm_pr_debug("ctcmpc:  outstanding_xid7: %i, "
				" outstanding_xid7_p2: %i\n",
				grp->outstanding_xid7,
				grp->outstanding_xid7_p2);
	}

	grp->outstanding_xid7--;
	ch->xid_skb->data = ch->xid_skb_data;
	skb_reset_tail_pointer(ch->xid_skb);
	ch->xid_skb->len = 0;

	switch (fsm_getstate(grp->fsm)) {
	case MPCG_STATE_XID7INITI:
		fsm_newstate(grp->fsm, MPCG_STATE_XID7INITZ);
		mpc_validate_xid(mpcginfo);
		break;
	case MPCG_STATE_XID7INITW:
		fsm_newstate(grp->fsm, MPCG_STATE_XID7INITX);
		mpc_validate_xid(mpcginfo);
		break;
	case MPCG_STATE_XID7INITZ:
	case MPCG_STATE_XID7INITX:
		if (grp->outstanding_xid7 == 0) {
			if (grp->outstanding_xid7_p2 > 0) {
				grp->outstanding_xid7 =
					grp->outstanding_xid7_p2;
				grp->outstanding_xid7_p2 = 0;
			} else
				fsm_newstate(grp->fsm, MPCG_STATE_XID7INITF);

			mpc_validate_xid(mpcginfo);
			fsm_event(grp->fsm, MPCG_EVENT_XID7DONE, dev);
			break;
		}
		mpc_validate_xid(mpcginfo);
		break;
	}

	kfree(mpcginfo);

	if (do_debug)
		ctcm_pr_debug("ctcmpc exit: %s(): cp=%i ch=0x%p id=%s\n",
			__FUNCTION__, smp_processor_id(), ch, ch->id);
	return;

}

/*
 * mpc_action helper of an MPC Group Station FSM action
 * CTCM_PROTO_MPC only
 */
static int mpc_send_qllc_discontact(struct net_device *dev)
{
	int	rc	= 0;
	__u32	new_len	= 0;
	struct sk_buff   *skb;
	struct qllc      *qllcptr;
	struct ctcm_priv *priv;
	struct mpc_group *grp;

	ctcm_pr_debug("ctcmpc enter:	%s()\n", __FUNCTION__);

	if (dev == NULL) {
		printk(KERN_INFO "%s() dev=NULL\n", __FUNCTION__);
		rc = 1;
					goto done;
	}

	priv = dev->priv;
	if (priv == NULL) {
		printk(KERN_INFO "%s() priv=NULL\n", __FUNCTION__);
		rc = 1;
					goto done;
	}

	grp = priv->mpcg;
	if (grp == NULL) {
		printk(KERN_INFO "%s() grp=NULL\n", __FUNCTION__);
		rc = 1;
					goto done;
	}
	ctcm_pr_info("ctcmpc: %s() GROUP STATE: %s\n", __FUNCTION__,
			mpcg_state_names[grp->saved_state]);

	switch (grp->saved_state) {
	/*
	 * establish conn callback function is
	 * preferred method to report failure
	 */
	case MPCG_STATE_XID0IOWAIT:
	case MPCG_STATE_XID0IOWAIX:
	case MPCG_STATE_XID7INITI:
	case MPCG_STATE_XID7INITZ:
	case MPCG_STATE_XID2INITW:
	case MPCG_STATE_XID2INITX:
	case MPCG_STATE_XID7INITW:
	case MPCG_STATE_XID7INITX:
		if (grp->estconnfunc) {
			grp->estconnfunc(grp->port_num, -1, 0);
			grp->estconnfunc = NULL;
			break;
		}
	case MPCG_STATE_FLOWC:
	case MPCG_STATE_READY:
		grp->send_qllc_disc = 2;
		new_len = sizeof(struct qllc);
		qllcptr = kzalloc(new_len, gfp_type() | GFP_DMA);
		if (qllcptr == NULL) {
			printk(KERN_INFO
			       "ctcmpc: Out of memory in %s()\n",
			       dev->name);
			rc = 1;
				goto done;
		}

		qllcptr->qllc_address = 0xcc;
		qllcptr->qllc_commands = 0x03;

		skb = __dev_alloc_skb(new_len, GFP_ATOMIC);

		if (skb == NULL) {
			printk(KERN_INFO "%s Out of memory in mpc_send_qllc\n",
			       dev->name);
			priv->stats.rx_dropped++;
			rc = 1;
			kfree(qllcptr);
				goto done;
		}

		memcpy(skb_put(skb, new_len), qllcptr, new_len);
		kfree(qllcptr);

		if (skb_headroom(skb) < 4) {
			printk(KERN_INFO "ctcmpc: %s() Unable to"
			       " build discontact for %s\n",
			       __FUNCTION__, dev->name);
			rc = 1;
			dev_kfree_skb_any(skb);
				goto done;
		}

		*((__u32 *)skb_push(skb, 4)) = priv->channel[READ]->pdu_seq;
		priv->channel[READ]->pdu_seq++;
		if (do_debug_data)
			ctcm_pr_debug("ctcmpc: %s ToDCM_pdu_seq= %08x\n",
				__FUNCTION__, priv->channel[READ]->pdu_seq);

		/* receipt of CC03 resets anticipated sequence number on
		      receiving side */
		priv->channel[READ]->pdu_seq = 0x00;
		skb_reset_mac_header(skb);
		skb->dev = dev;
		skb->protocol = htons(ETH_P_SNAP);
		skb->ip_summed = CHECKSUM_UNNECESSARY;

		ctcmpc_dumpit((char *)skb->data, (sizeof(struct qllc) + 4));

		netif_rx(skb);
		break;
	default:
		break;

	}

done:
	ctcm_pr_debug("ctcmpc exit:  %s()\n", __FUNCTION__);
	return rc;
}
/* --- This is the END my friend --- */

