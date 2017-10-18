/* $Id: isdn_ppp.c,v 1.1.2.3 2004/02/10 01:07:13 keil Exp $
 *
 * Linux ISDN subsystem, functions for synchronous PPP (linklevel).
 *
 * Copyright 1995,96 by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/isdn.h>
#include <linux/poll.h>
#include <linux/ppp-comp.h>
#include <linux/slab.h>
#ifdef CONFIG_IPPP_FILTER
#include <linux/filter.h>
#endif

#include "isdn_common.h"
#include "isdn_ppp.h"
#include "isdn_net.h"

#ifndef PPP_IPX
#define PPP_IPX 0x002b
#endif

/* Prototypes */
static int isdn_ppp_fill_rq(unsigned char *buf, int len, int proto, int slot);
static int isdn_ppp_closewait(int slot);
static void isdn_ppp_push_higher(isdn_net_dev *net_dev, isdn_net_local *lp,
				 struct sk_buff *skb, int proto);
static int isdn_ppp_if_get_unit(char *namebuf);
static int isdn_ppp_set_compressor(struct ippp_struct *is, struct isdn_ppp_comp_data *);
static struct sk_buff *isdn_ppp_decompress(struct sk_buff *,
					   struct ippp_struct *, struct ippp_struct *, int *proto);
static void isdn_ppp_receive_ccp(isdn_net_dev *net_dev, isdn_net_local *lp,
				 struct sk_buff *skb, int proto);
static struct sk_buff *isdn_ppp_compress(struct sk_buff *skb_in, int *proto,
					 struct ippp_struct *is, struct ippp_struct *master, int type);
static void isdn_ppp_send_ccp(isdn_net_dev *net_dev, isdn_net_local *lp,
			      struct sk_buff *skb);

/* New CCP stuff */
static void isdn_ppp_ccp_kickup(struct ippp_struct *is);
static void isdn_ppp_ccp_xmit_reset(struct ippp_struct *is, int proto,
				    unsigned char code, unsigned char id,
				    unsigned char *data, int len);
static struct ippp_ccp_reset *isdn_ppp_ccp_reset_alloc(struct ippp_struct *is);
static void isdn_ppp_ccp_reset_free(struct ippp_struct *is);
static void isdn_ppp_ccp_reset_free_state(struct ippp_struct *is,
					  unsigned char id);
static void isdn_ppp_ccp_timer_callback(unsigned long closure);
static struct ippp_ccp_reset_state *isdn_ppp_ccp_reset_alloc_state(struct ippp_struct *is,
								   unsigned char id);
static void isdn_ppp_ccp_reset_trans(struct ippp_struct *is,
				     struct isdn_ppp_resetparams *rp);
static void isdn_ppp_ccp_reset_ack_rcvd(struct ippp_struct *is,
					unsigned char id);



#ifdef CONFIG_ISDN_MPP
static ippp_bundle *isdn_ppp_bundle_arr = NULL;

static int isdn_ppp_mp_bundle_array_init(void);
static int isdn_ppp_mp_init(isdn_net_local *lp, ippp_bundle *add_to);
static void isdn_ppp_mp_receive(isdn_net_dev *net_dev, isdn_net_local *lp,
				struct sk_buff *skb);
static void isdn_ppp_mp_cleanup(isdn_net_local *lp);

static int isdn_ppp_bundle(struct ippp_struct *, int unit);
#endif	/* CONFIG_ISDN_MPP */

char *isdn_ppp_revision = "$Revision: 1.1.2.3 $";

static struct ippp_struct *ippp_table[ISDN_MAX_CHANNELS];

static struct isdn_ppp_compressor *ipc_head = NULL;

/*
 * frame log (debug)
 */
static void
isdn_ppp_frame_log(char *info, char *data, int len, int maxlen, int unit, int slot)
{
	int cnt,
		j,
		i;
	char buf[80];

	if (len < maxlen)
		maxlen = len;

	for (i = 0, cnt = 0; cnt < maxlen; i++) {
		for (j = 0; j < 16 && cnt < maxlen; j++, cnt++)
			sprintf(buf + j * 3, "%02x ", (unsigned char)data[cnt]);
		printk(KERN_DEBUG "[%d/%d].%s[%d]: %s\n", unit, slot, info, i, buf);
	}
}

/*
 * unbind isdn_net_local <=> ippp-device
 * note: it can happen, that we hangup/free the master before the slaves
 *       in this case we bind another lp to the master device
 */
int
isdn_ppp_free(isdn_net_local *lp)
{
	struct ippp_struct *is;

	if (lp->ppp_slot < 0 || lp->ppp_slot >= ISDN_MAX_CHANNELS) {
		printk(KERN_ERR "%s: ppp_slot(%d) out of range\n",
		       __func__, lp->ppp_slot);
		return 0;
	}

#ifdef CONFIG_ISDN_MPP
	spin_lock(&lp->netdev->pb->lock);
#endif
	isdn_net_rm_from_bundle(lp);
#ifdef CONFIG_ISDN_MPP
	if (lp->netdev->pb->ref_ct == 1)	/* last link in queue? */
		isdn_ppp_mp_cleanup(lp);

	lp->netdev->pb->ref_ct--;
	spin_unlock(&lp->netdev->pb->lock);
#endif /* CONFIG_ISDN_MPP */
	if (lp->ppp_slot < 0 || lp->ppp_slot >= ISDN_MAX_CHANNELS) {
		printk(KERN_ERR "%s: ppp_slot(%d) now invalid\n",
		       __func__, lp->ppp_slot);
		return 0;
	}
	is = ippp_table[lp->ppp_slot];
	if ((is->state & IPPP_CONNECT))
		isdn_ppp_closewait(lp->ppp_slot);	/* force wakeup on ippp device */
	else if (is->state & IPPP_ASSIGNED)
		is->state = IPPP_OPEN;	/* fallback to 'OPEN but not ASSIGNED' state */

	if (is->debug & 0x1)
		printk(KERN_DEBUG "isdn_ppp_free %d %lx %lx\n", lp->ppp_slot, (long) lp, (long) is->lp);

	is->lp = NULL;          /* link is down .. set lp to NULL */
	lp->ppp_slot = -1;      /* is this OK ?? */

	return 0;
}

/*
 * bind isdn_net_local <=> ippp-device
 *
 * This function is allways called with holding dev->lock so
 * no additional lock is needed
 */
int
isdn_ppp_bind(isdn_net_local *lp)
{
	int i;
	int unit = 0;
	struct ippp_struct *is;
	int retval;

	if (lp->pppbind < 0) {  /* device bounded to ippp device ? */
		isdn_net_dev *net_dev = dev->netdev;
		char exclusive[ISDN_MAX_CHANNELS];	/* exclusive flags */
		memset(exclusive, 0, ISDN_MAX_CHANNELS);
		while (net_dev) {	/* step through net devices to find exclusive minors */
			isdn_net_local *lp = net_dev->local;
			if (lp->pppbind >= 0)
				exclusive[lp->pppbind] = 1;
			net_dev = net_dev->next;
		}
		/*
		 * search a free device / slot
		 */
		for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
			if (ippp_table[i]->state == IPPP_OPEN && !exclusive[ippp_table[i]->minor]) {	/* OPEN, but not connected! */
				break;
			}
		}
	} else {
		for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
			if (ippp_table[i]->minor == lp->pppbind &&
			    (ippp_table[i]->state & IPPP_OPEN) == IPPP_OPEN)
				break;
		}
	}

	if (i >= ISDN_MAX_CHANNELS) {
		printk(KERN_WARNING "isdn_ppp_bind: Can't find a (free) connection to the ipppd daemon.\n");
		retval = -1;
		goto out;
	}
	/* get unit number from interface name .. ugly! */
	unit = isdn_ppp_if_get_unit(lp->netdev->dev->name);
	if (unit < 0) {
		printk(KERN_ERR "isdn_ppp_bind: illegal interface name %s.\n",
		       lp->netdev->dev->name);
		retval = -1;
		goto out;
	}

	lp->ppp_slot = i;
	is = ippp_table[i];
	is->lp = lp;
	is->unit = unit;
	is->state = IPPP_OPEN | IPPP_ASSIGNED;	/* assigned to a netdevice but not connected */
#ifdef CONFIG_ISDN_MPP
	retval = isdn_ppp_mp_init(lp, NULL);
	if (retval < 0)
		goto out;
#endif /* CONFIG_ISDN_MPP */

	retval = lp->ppp_slot;

out:
	return retval;
}

/*
 * kick the ipppd on the device
 * (wakes up daemon after B-channel connect)
 */

void
isdn_ppp_wakeup_daemon(isdn_net_local *lp)
{
	if (lp->ppp_slot < 0 || lp->ppp_slot >= ISDN_MAX_CHANNELS) {
		printk(KERN_ERR "%s: ppp_slot(%d) out of range\n",
		       __func__, lp->ppp_slot);
		return;
	}
	ippp_table[lp->ppp_slot]->state = IPPP_OPEN | IPPP_CONNECT | IPPP_NOBLOCK;
	wake_up_interruptible(&ippp_table[lp->ppp_slot]->wq);
}

/*
 * there was a hangup on the netdevice
 * force wakeup of the ippp device
 * go into 'device waits for release' state
 */
static int
isdn_ppp_closewait(int slot)
{
	struct ippp_struct *is;

	if (slot < 0 || slot >= ISDN_MAX_CHANNELS) {
		printk(KERN_ERR "%s: slot(%d) out of range\n",
		       __func__, slot);
		return 0;
	}
	is = ippp_table[slot];
	if (is->state)
		wake_up_interruptible(&is->wq);
	is->state = IPPP_CLOSEWAIT;
	return 1;
}

/*
 * isdn_ppp_find_slot / isdn_ppp_free_slot
 */

static int
isdn_ppp_get_slot(void)
{
	int i;
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		if (!ippp_table[i]->state)
			return i;
	}
	return -1;
}

/*
 * isdn_ppp_open
 */

int
isdn_ppp_open(int min, struct file *file)
{
	int slot;
	struct ippp_struct *is;

	if (min < 0 || min >= ISDN_MAX_CHANNELS)
		return -ENODEV;

	slot = isdn_ppp_get_slot();
	if (slot < 0) {
		return -EBUSY;
	}
	is = file->private_data = ippp_table[slot];

	printk(KERN_DEBUG "ippp, open, slot: %d, minor: %d, state: %04x\n",
	       slot, min, is->state);

	/* compression stuff */
	is->link_compressor   = is->compressor = NULL;
	is->link_decompressor = is->decompressor = NULL;
	is->link_comp_stat    = is->comp_stat = NULL;
	is->link_decomp_stat  = is->decomp_stat = NULL;
	is->compflags = 0;

	is->reset = isdn_ppp_ccp_reset_alloc(is);
	if (!is->reset)
		return -ENOMEM;

	is->lp = NULL;
	is->mp_seqno = 0;       /* MP sequence number */
	is->pppcfg = 0;         /* ppp configuration */
	is->mpppcfg = 0;        /* mppp configuration */
	is->last_link_seqno = -1;	/* MP: maybe set to Bundle-MIN, when joining a bundle ?? */
	is->unit = -1;          /* set, when we have our interface */
	is->mru = 1524;         /* MRU, default 1524 */
	is->maxcid = 16;        /* VJ: maxcid */
	is->tk = current;
	init_waitqueue_head(&is->wq);
	is->first = is->rq + NUM_RCV_BUFFS - 1;	/* receive queue */
	is->last = is->rq;
	is->minor = min;
#ifdef CONFIG_ISDN_PPP_VJ
	/*
	 * VJ header compression init
	 */
	is->slcomp = slhc_init(16, 16);	/* not necessary for 2. link in bundle */
	if (IS_ERR(is->slcomp)) {
		isdn_ppp_ccp_reset_free(is);
		return PTR_ERR(is->slcomp);
	}
#endif
#ifdef CONFIG_IPPP_FILTER
	is->pass_filter = NULL;
	is->active_filter = NULL;
#endif
	is->state = IPPP_OPEN;

	return 0;
}

/*
 * release ippp device
 */
void
isdn_ppp_release(int min, struct file *file)
{
	int i;
	struct ippp_struct *is;

	if (min < 0 || min >= ISDN_MAX_CHANNELS)
		return;
	is = file->private_data;

	if (!is) {
		printk(KERN_ERR "%s: no file->private_data\n", __func__);
		return;
	}
	if (is->debug & 0x1)
		printk(KERN_DEBUG "ippp: release, minor: %d %lx\n", min, (long) is->lp);

	if (is->lp) {           /* a lp address says: this link is still up */
		isdn_net_dev *p = is->lp->netdev;

		if (!p) {
			printk(KERN_ERR "%s: no lp->netdev\n", __func__);
			return;
		}
		is->state &= ~IPPP_CONNECT;	/* -> effect: no call of wakeup */
		/*
		 * isdn_net_hangup() calls isdn_ppp_free()
		 * isdn_ppp_free() sets is->lp to NULL and lp->ppp_slot to -1
		 * removing the IPPP_CONNECT flag omits calling of isdn_ppp_wakeup_daemon()
		 */
		isdn_net_hangup(p->dev);
	}
	for (i = 0; i < NUM_RCV_BUFFS; i++) {
		kfree(is->rq[i].buf);
		is->rq[i].buf = NULL;
	}
	is->first = is->rq + NUM_RCV_BUFFS - 1;	/* receive queue */
	is->last = is->rq;

#ifdef CONFIG_ISDN_PPP_VJ
/* TODO: if this was the previous master: link the slcomp to the new master */
	slhc_free(is->slcomp);
	is->slcomp = NULL;
#endif
#ifdef CONFIG_IPPP_FILTER
	if (is->pass_filter) {
		bpf_prog_destroy(is->pass_filter);
		is->pass_filter = NULL;
	}

	if (is->active_filter) {
		bpf_prog_destroy(is->active_filter);
		is->active_filter = NULL;
	}
#endif

/* TODO: if this was the previous master: link the stuff to the new master */
	if (is->comp_stat)
		is->compressor->free(is->comp_stat);
	if (is->link_comp_stat)
		is->link_compressor->free(is->link_comp_stat);
	if (is->link_decomp_stat)
		is->link_decompressor->free(is->link_decomp_stat);
	if (is->decomp_stat)
		is->decompressor->free(is->decomp_stat);
	is->compressor   = is->link_compressor   = NULL;
	is->decompressor = is->link_decompressor = NULL;
	is->comp_stat    = is->link_comp_stat    = NULL;
	is->decomp_stat  = is->link_decomp_stat  = NULL;

	/* Clean up if necessary */
	if (is->reset)
		isdn_ppp_ccp_reset_free(is);

	/* this slot is ready for new connections */
	is->state = 0;
}

/*
 * get_arg .. ioctl helper
 */
static int
get_arg(void __user *b, void *val, int len)
{
	if (len <= 0)
		len = sizeof(void *);
	if (copy_from_user(val, b, len))
		return -EFAULT;
	return 0;
}

/*
 * set arg .. ioctl helper
 */
static int
set_arg(void __user *b, void *val, int len)
{
	if (len <= 0)
		len = sizeof(void *);
	if (copy_to_user(b, val, len))
		return -EFAULT;
	return 0;
}

#ifdef CONFIG_IPPP_FILTER
static int get_filter(void __user *arg, struct sock_filter **p)
{
	struct sock_fprog uprog;
	struct sock_filter *code = NULL;
	int len;

	if (copy_from_user(&uprog, arg, sizeof(uprog)))
		return -EFAULT;

	if (!uprog.len) {
		*p = NULL;
		return 0;
	}

	/* uprog.len is unsigned short, so no overflow here */
	len = uprog.len * sizeof(struct sock_filter);
	code = memdup_user(uprog.filter, len);
	if (IS_ERR(code))
		return PTR_ERR(code);

	*p = code;
	return uprog.len;
}
#endif /* CONFIG_IPPP_FILTER */

/*
 * ippp device ioctl
 */
int
isdn_ppp_ioctl(int min, struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned long val;
	int r, i, j;
	struct ippp_struct *is;
	isdn_net_local *lp;
	struct isdn_ppp_comp_data data;
	void __user *argp = (void __user *)arg;

	is = file->private_data;
	lp = is->lp;

	if (is->debug & 0x1)
		printk(KERN_DEBUG "isdn_ppp_ioctl: minor: %d cmd: %x state: %x\n", min, cmd, is->state);

	if (!(is->state & IPPP_OPEN))
		return -EINVAL;

	switch (cmd) {
	case PPPIOCBUNDLE:
#ifdef CONFIG_ISDN_MPP
		if (!(is->state & IPPP_CONNECT))
			return -EINVAL;
		if ((r = get_arg(argp, &val, sizeof(val))))
			return r;
		printk(KERN_DEBUG "iPPP-bundle: minor: %d, slave unit: %d, master unit: %d\n",
		       (int) min, (int) is->unit, (int) val);
		return isdn_ppp_bundle(is, val);
#else
		return -1;
#endif
		break;
	case PPPIOCGUNIT:	/* get ppp/isdn unit number */
		if ((r = set_arg(argp, &is->unit, sizeof(is->unit))))
			return r;
		break;
	case PPPIOCGIFNAME:
		if (!lp)
			return -EINVAL;
		if ((r = set_arg(argp, lp->netdev->dev->name,
				 strlen(lp->netdev->dev->name))))
			return r;
		break;
	case PPPIOCGMPFLAGS:	/* get configuration flags */
		if ((r = set_arg(argp, &is->mpppcfg, sizeof(is->mpppcfg))))
			return r;
		break;
	case PPPIOCSMPFLAGS:	/* set configuration flags */
		if ((r = get_arg(argp, &val, sizeof(val))))
			return r;
		is->mpppcfg = val;
		break;
	case PPPIOCGFLAGS:	/* get configuration flags */
		if ((r = set_arg(argp, &is->pppcfg, sizeof(is->pppcfg))))
			return r;
		break;
	case PPPIOCSFLAGS:	/* set configuration flags */
		if ((r = get_arg(argp, &val, sizeof(val)))) {
			return r;
		}
		if (val & SC_ENABLE_IP && !(is->pppcfg & SC_ENABLE_IP) && (is->state & IPPP_CONNECT)) {
			if (lp) {
				/* OK .. we are ready to send buffers */
				is->pppcfg = val; /* isdn_ppp_xmit test for SC_ENABLE_IP !!! */
				netif_wake_queue(lp->netdev->dev);
				break;
			}
		}
		is->pppcfg = val;
		break;
	case PPPIOCGIDLE:	/* get idle time information */
		if (lp) {
			struct ppp_idle pidle;
			pidle.xmit_idle = pidle.recv_idle = lp->huptimer;
			if ((r = set_arg(argp, &pidle, sizeof(struct ppp_idle))))
				return r;
		}
		break;
	case PPPIOCSMRU:	/* set receive unit size for PPP */
		if ((r = get_arg(argp, &val, sizeof(val))))
			return r;
		is->mru = val;
		break;
	case PPPIOCSMPMRU:
		break;
	case PPPIOCSMPMTU:
		break;
	case PPPIOCSMAXCID:	/* set the maximum compression slot id */
		if ((r = get_arg(argp, &val, sizeof(val))))
			return r;
		val++;
		if (is->maxcid != val) {
#ifdef CONFIG_ISDN_PPP_VJ
			struct slcompress *sltmp;
#endif
			if (is->debug & 0x1)
				printk(KERN_DEBUG "ippp, ioctl: changed MAXCID to %ld\n", val);
			is->maxcid = val;
#ifdef CONFIG_ISDN_PPP_VJ
			sltmp = slhc_init(16, val);
			if (IS_ERR(sltmp))
				return PTR_ERR(sltmp);
			if (is->slcomp)
				slhc_free(is->slcomp);
			is->slcomp = sltmp;
#endif
		}
		break;
	case PPPIOCGDEBUG:
		if ((r = set_arg(argp, &is->debug, sizeof(is->debug))))
			return r;
		break;
	case PPPIOCSDEBUG:
		if ((r = get_arg(argp, &val, sizeof(val))))
			return r;
		is->debug = val;
		break;
	case PPPIOCGCOMPRESSORS:
	{
		unsigned long protos[8] = {0,};
		struct isdn_ppp_compressor *ipc = ipc_head;
		while (ipc) {
			j = ipc->num / (sizeof(long) * 8);
			i = ipc->num % (sizeof(long) * 8);
			if (j < 8)
				protos[j] |= (1UL << i);
			ipc = ipc->next;
		}
		if ((r = set_arg(argp, protos, 8 * sizeof(long))))
			return r;
	}
	break;
	case PPPIOCSCOMPRESSOR:
		if ((r = get_arg(argp, &data, sizeof(struct isdn_ppp_comp_data))))
			return r;
		return isdn_ppp_set_compressor(is, &data);
	case PPPIOCGCALLINFO:
	{
		struct pppcallinfo pci;
		memset((char *)&pci, 0, sizeof(struct pppcallinfo));
		if (lp)
		{
			strncpy(pci.local_num, lp->msn, 63);
			if (lp->dial) {
				strncpy(pci.remote_num, lp->dial->num, 63);
			}
			pci.charge_units = lp->charge;
			if (lp->outgoing)
				pci.calltype = CALLTYPE_OUTGOING;
			else
				pci.calltype = CALLTYPE_INCOMING;
			if (lp->flags & ISDN_NET_CALLBACK)
				pci.calltype |= CALLTYPE_CALLBACK;
		}
		return set_arg(argp, &pci, sizeof(struct pppcallinfo));
	}
#ifdef CONFIG_IPPP_FILTER
	case PPPIOCSPASS:
	{
		struct sock_fprog_kern fprog;
		struct sock_filter *code;
		int err, len = get_filter(argp, &code);

		if (len < 0)
			return len;

		fprog.len = len;
		fprog.filter = code;

		if (is->pass_filter) {
			bpf_prog_destroy(is->pass_filter);
			is->pass_filter = NULL;
		}
		if (fprog.filter != NULL)
			err = bpf_prog_create(&is->pass_filter, &fprog);
		else
			err = 0;
		kfree(code);

		return err;
	}
	case PPPIOCSACTIVE:
	{
		struct sock_fprog_kern fprog;
		struct sock_filter *code;
		int err, len = get_filter(argp, &code);

		if (len < 0)
			return len;

		fprog.len = len;
		fprog.filter = code;

		if (is->active_filter) {
			bpf_prog_destroy(is->active_filter);
			is->active_filter = NULL;
		}
		if (fprog.filter != NULL)
			err = bpf_prog_create(&is->active_filter, &fprog);
		else
			err = 0;
		kfree(code);

		return err;
	}
#endif /* CONFIG_IPPP_FILTER */
	default:
		break;
	}
	return 0;
}

unsigned int
isdn_ppp_poll(struct file *file, poll_table *wait)
{
	u_int mask;
	struct ippp_buf_queue *bf, *bl;
	u_long flags;
	struct ippp_struct *is;

	is = file->private_data;

	if (is->debug & 0x2)
		printk(KERN_DEBUG "isdn_ppp_poll: minor: %d\n",
		       iminor(file_inode(file)));

	/* just registers wait_queue hook. This doesn't really wait. */
	poll_wait(file, &is->wq, wait);

	if (!(is->state & IPPP_OPEN)) {
		if (is->state == IPPP_CLOSEWAIT)
			return POLLHUP;
		printk(KERN_DEBUG "isdn_ppp: device not open\n");
		return POLLERR;
	}
	/* we're always ready to send .. */
	mask = POLLOUT | POLLWRNORM;

	spin_lock_irqsave(&is->buflock, flags);
	bl = is->last;
	bf = is->first;
	/*
	 * if IPPP_NOBLOCK is set we return even if we have nothing to read
	 */
	if (bf->next != bl || (is->state & IPPP_NOBLOCK)) {
		is->state &= ~IPPP_NOBLOCK;
		mask |= POLLIN | POLLRDNORM;
	}
	spin_unlock_irqrestore(&is->buflock, flags);
	return mask;
}

/*
 *  fill up isdn_ppp_read() queue ..
 */

static int
isdn_ppp_fill_rq(unsigned char *buf, int len, int proto, int slot)
{
	struct ippp_buf_queue *bf, *bl;
	u_long flags;
	u_char *nbuf;
	struct ippp_struct *is;

	if (slot < 0 || slot >= ISDN_MAX_CHANNELS) {
		printk(KERN_WARNING "ippp: illegal slot(%d).\n", slot);
		return 0;
	}
	is = ippp_table[slot];

	if (!(is->state & IPPP_CONNECT)) {
		printk(KERN_DEBUG "ippp: device not activated.\n");
		return 0;
	}
	nbuf = kmalloc(len + 4, GFP_ATOMIC);
	if (!nbuf) {
		printk(KERN_WARNING "ippp: Can't alloc buf\n");
		return 0;
	}
	nbuf[0] = PPP_ALLSTATIONS;
	nbuf[1] = PPP_UI;
	nbuf[2] = proto >> 8;
	nbuf[3] = proto & 0xff;
	memcpy(nbuf + 4, buf, len);

	spin_lock_irqsave(&is->buflock, flags);
	bf = is->first;
	bl = is->last;

	if (bf == bl) {
		printk(KERN_WARNING "ippp: Queue is full; discarding first buffer\n");
		bf = bf->next;
		kfree(bf->buf);
		is->first = bf;
	}
	bl->buf = (char *) nbuf;
	bl->len = len + 4;

	is->last = bl->next;
	spin_unlock_irqrestore(&is->buflock, flags);
	wake_up_interruptible(&is->wq);
	return len;
}

/*
 * read() .. non-blocking: ipppd calls it only after select()
 *           reports, that there is data
 */

int
isdn_ppp_read(int min, struct file *file, char __user *buf, int count)
{
	struct ippp_struct *is;
	struct ippp_buf_queue *b;
	u_long flags;
	u_char *save_buf;

	is = file->private_data;

	if (!(is->state & IPPP_OPEN))
		return 0;

	spin_lock_irqsave(&is->buflock, flags);
	b = is->first->next;
	save_buf = b->buf;
	if (!save_buf) {
		spin_unlock_irqrestore(&is->buflock, flags);
		return -EAGAIN;
	}
	if (b->len < count)
		count = b->len;
	b->buf = NULL;
	is->first = b;

	spin_unlock_irqrestore(&is->buflock, flags);
	if (copy_to_user(buf, save_buf, count))
		count = -EFAULT;
	kfree(save_buf);

	return count;
}

/*
 * ipppd wanna write a packet to the card .. non-blocking
 */

int
isdn_ppp_write(int min, struct file *file, const char __user *buf, int count)
{
	isdn_net_local *lp;
	struct ippp_struct *is;
	int proto;
	unsigned char protobuf[4];

	is = file->private_data;

	if (!(is->state & IPPP_CONNECT))
		return 0;

	lp = is->lp;

	/* -> push it directly to the lowlevel interface */

	if (!lp)
		printk(KERN_DEBUG "isdn_ppp_write: lp == NULL\n");
	else {
		/*
		 * Don't reset huptimer for
		 * LCP packets. (Echo requests).
		 */
		if (copy_from_user(protobuf, buf, 4))
			return -EFAULT;
		proto = PPP_PROTOCOL(protobuf);
		if (proto != PPP_LCP)
			lp->huptimer = 0;

		if (lp->isdn_device < 0 || lp->isdn_channel < 0)
			return 0;

		if ((dev->drv[lp->isdn_device]->flags & DRV_FLAG_RUNNING) &&
		    lp->dialstate == 0 &&
		    (lp->flags & ISDN_NET_CONNECTED)) {
			unsigned short hl;
			struct sk_buff *skb;
			/*
			 * we need to reserve enough space in front of
			 * sk_buff. old call to dev_alloc_skb only reserved
			 * 16 bytes, now we are looking what the driver want
			 */
			hl = dev->drv[lp->isdn_device]->interface->hl_hdrlen;
			skb = alloc_skb(hl + count, GFP_ATOMIC);
			if (!skb) {
				printk(KERN_WARNING "isdn_ppp_write: out of memory!\n");
				return count;
			}
			skb_reserve(skb, hl);
			if (copy_from_user(skb_put(skb, count), buf, count))
			{
				kfree_skb(skb);
				return -EFAULT;
			}
			if (is->debug & 0x40) {
				printk(KERN_DEBUG "ppp xmit: len %d\n", (int) skb->len);
				isdn_ppp_frame_log("xmit", skb->data, skb->len, 32, is->unit, lp->ppp_slot);
			}

			isdn_ppp_send_ccp(lp->netdev, lp, skb); /* keeps CCP/compression states in sync */

			isdn_net_write_super(lp, skb);
		}
	}
	return count;
}

/*
 * init memory, structures etc.
 */

int
isdn_ppp_init(void)
{
	int i,
		j;

#ifdef CONFIG_ISDN_MPP
	if (isdn_ppp_mp_bundle_array_init() < 0)
		return -ENOMEM;
#endif /* CONFIG_ISDN_MPP */

	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		if (!(ippp_table[i] = kzalloc(sizeof(struct ippp_struct), GFP_KERNEL))) {
			printk(KERN_WARNING "isdn_ppp_init: Could not alloc ippp_table\n");
			for (j = 0; j < i; j++)
				kfree(ippp_table[j]);
			return -1;
		}
		spin_lock_init(&ippp_table[i]->buflock);
		ippp_table[i]->state = 0;
		ippp_table[i]->first = ippp_table[i]->rq + NUM_RCV_BUFFS - 1;
		ippp_table[i]->last = ippp_table[i]->rq;

		for (j = 0; j < NUM_RCV_BUFFS; j++) {
			ippp_table[i]->rq[j].buf = NULL;
			ippp_table[i]->rq[j].last = ippp_table[i]->rq +
				(NUM_RCV_BUFFS + j - 1) % NUM_RCV_BUFFS;
			ippp_table[i]->rq[j].next = ippp_table[i]->rq + (j + 1) % NUM_RCV_BUFFS;
		}
	}
	return 0;
}

void
isdn_ppp_cleanup(void)
{
	int i;

	for (i = 0; i < ISDN_MAX_CHANNELS; i++)
		kfree(ippp_table[i]);

#ifdef CONFIG_ISDN_MPP
	kfree(isdn_ppp_bundle_arr);
#endif /* CONFIG_ISDN_MPP */

}

/*
 * check for address/control field and skip if allowed
 * retval != 0 -> discard packet silently
 */
static int isdn_ppp_skip_ac(struct ippp_struct *is, struct sk_buff *skb)
{
	if (skb->len < 1)
		return -1;

	if (skb->data[0] == 0xff) {
		if (skb->len < 2)
			return -1;

		if (skb->data[1] != 0x03)
			return -1;

		// skip address/control (AC) field
		skb_pull(skb, 2);
	} else {
		if (is->pppcfg & SC_REJ_COMP_AC)
			// if AC compression was not negotiated, but used, discard packet
			return -1;
	}
	return 0;
}

/*
 * get the PPP protocol header and pull skb
 * retval < 0 -> discard packet silently
 */
static int isdn_ppp_strip_proto(struct sk_buff *skb)
{
	int proto;

	if (skb->len < 1)
		return -1;

	if (skb->data[0] & 0x1) {
		// protocol field is compressed
		proto = skb->data[0];
		skb_pull(skb, 1);
	} else {
		if (skb->len < 2)
			return -1;
		proto = ((int) skb->data[0] << 8) + skb->data[1];
		skb_pull(skb, 2);
	}
	return proto;
}


/*
 * handler for incoming packets on a syncPPP interface
 */
void isdn_ppp_receive(isdn_net_dev *net_dev, isdn_net_local *lp, struct sk_buff *skb)
{
	struct ippp_struct *is;
	int slot;
	int proto;

	BUG_ON(net_dev->local->master); // we're called with the master device always

	slot = lp->ppp_slot;
	if (slot < 0 || slot >= ISDN_MAX_CHANNELS) {
		printk(KERN_ERR "isdn_ppp_receive: lp->ppp_slot(%d)\n",
		       lp->ppp_slot);
		kfree_skb(skb);
		return;
	}
	is = ippp_table[slot];

	if (is->debug & 0x4) {
		printk(KERN_DEBUG "ippp_receive: is:%08lx lp:%08lx slot:%d unit:%d len:%d\n",
		       (long)is, (long)lp, lp->ppp_slot, is->unit, (int)skb->len);
		isdn_ppp_frame_log("receive", skb->data, skb->len, 32, is->unit, lp->ppp_slot);
	}

	if (isdn_ppp_skip_ac(is, skb) < 0) {
		kfree_skb(skb);
		return;
	}
	proto = isdn_ppp_strip_proto(skb);
	if (proto < 0) {
		kfree_skb(skb);
		return;
	}

#ifdef CONFIG_ISDN_MPP
	if (is->compflags & SC_LINK_DECOMP_ON) {
		skb = isdn_ppp_decompress(skb, is, NULL, &proto);
		if (!skb) // decompression error
			return;
	}

	if (!(is->mpppcfg & SC_REJ_MP_PROT)) { // we agreed to receive MPPP
		if (proto == PPP_MP) {
			isdn_ppp_mp_receive(net_dev, lp, skb);
			return;
		}
	}
#endif
	isdn_ppp_push_higher(net_dev, lp, skb, proto);
}

/*
 * we receive a reassembled frame, MPPP has been taken care of before.
 * address/control and protocol have been stripped from the skb
 * note: net_dev has to be master net_dev
 */
static void
isdn_ppp_push_higher(isdn_net_dev *net_dev, isdn_net_local *lp, struct sk_buff *skb, int proto)
{
	struct net_device *dev = net_dev->dev;
	struct ippp_struct *is, *mis;
	isdn_net_local *mlp = NULL;
	int slot;

	slot = lp->ppp_slot;
	if (slot < 0 || slot >= ISDN_MAX_CHANNELS) {
		printk(KERN_ERR "isdn_ppp_push_higher: lp->ppp_slot(%d)\n",
		       lp->ppp_slot);
		goto drop_packet;
	}
	is = ippp_table[slot];

	if (lp->master) { // FIXME?
		mlp = ISDN_MASTER_PRIV(lp);
		slot = mlp->ppp_slot;
		if (slot < 0 || slot >= ISDN_MAX_CHANNELS) {
			printk(KERN_ERR "isdn_ppp_push_higher: master->ppp_slot(%d)\n",
			       lp->ppp_slot);
			goto drop_packet;
		}
	}
	mis = ippp_table[slot];

	if (is->debug & 0x10) {
		printk(KERN_DEBUG "push, skb %d %04x\n", (int) skb->len, proto);
		isdn_ppp_frame_log("rpush", skb->data, skb->len, 32, is->unit, lp->ppp_slot);
	}
	if (mis->compflags & SC_DECOMP_ON) {
		skb = isdn_ppp_decompress(skb, is, mis, &proto);
		if (!skb) // decompression error
			return;
	}
	switch (proto) {
	case PPP_IPX:  /* untested */
		if (is->debug & 0x20)
			printk(KERN_DEBUG "isdn_ppp: IPX\n");
		skb->protocol = htons(ETH_P_IPX);
		break;
	case PPP_IP:
		if (is->debug & 0x20)
			printk(KERN_DEBUG "isdn_ppp: IP\n");
		skb->protocol = htons(ETH_P_IP);
		break;
	case PPP_COMP:
	case PPP_COMPFRAG:
		printk(KERN_INFO "isdn_ppp: unexpected compressed frame dropped\n");
		goto drop_packet;
#ifdef CONFIG_ISDN_PPP_VJ
	case PPP_VJC_UNCOMP:
		if (is->debug & 0x20)
			printk(KERN_DEBUG "isdn_ppp: VJC_UNCOMP\n");
		if (net_dev->local->ppp_slot < 0) {
			printk(KERN_ERR "%s: net_dev->local->ppp_slot(%d) out of range\n",
			       __func__, net_dev->local->ppp_slot);
			goto drop_packet;
		}
		if (slhc_remember(ippp_table[net_dev->local->ppp_slot]->slcomp, skb->data, skb->len) <= 0) {
			printk(KERN_WARNING "isdn_ppp: received illegal VJC_UNCOMP frame!\n");
			goto drop_packet;
		}
		skb->protocol = htons(ETH_P_IP);
		break;
	case PPP_VJC_COMP:
		if (is->debug & 0x20)
			printk(KERN_DEBUG "isdn_ppp: VJC_COMP\n");
		{
			struct sk_buff *skb_old = skb;
			int pkt_len;
			skb = dev_alloc_skb(skb_old->len + 128);

			if (!skb) {
				printk(KERN_WARNING "%s: Memory squeeze, dropping packet.\n", dev->name);
				skb = skb_old;
				goto drop_packet;
			}
			skb_put(skb, skb_old->len + 128);
			skb_copy_from_linear_data(skb_old, skb->data,
						  skb_old->len);
			if (net_dev->local->ppp_slot < 0) {
				printk(KERN_ERR "%s: net_dev->local->ppp_slot(%d) out of range\n",
				       __func__, net_dev->local->ppp_slot);
				goto drop_packet;
			}
			pkt_len = slhc_uncompress(ippp_table[net_dev->local->ppp_slot]->slcomp,
						  skb->data, skb_old->len);
			kfree_skb(skb_old);
			if (pkt_len < 0)
				goto drop_packet;

			skb_trim(skb, pkt_len);
			skb->protocol = htons(ETH_P_IP);
		}
		break;
#endif
	case PPP_CCP:
	case PPP_CCPFRAG:
		isdn_ppp_receive_ccp(net_dev, lp, skb, proto);
		/* Dont pop up ResetReq/Ack stuff to the daemon any
		   longer - the job is done already */
		if (skb->data[0] == CCP_RESETREQ ||
		    skb->data[0] == CCP_RESETACK)
			break;
		/* fall through */
	default:
		isdn_ppp_fill_rq(skb->data, skb->len, proto, lp->ppp_slot);	/* push data to pppd device */
		kfree_skb(skb);
		return;
	}

#ifdef CONFIG_IPPP_FILTER
	/* check if the packet passes the pass and active filters
	 * the filter instructions are constructed assuming
	 * a four-byte PPP header on each packet (which is still present) */
	skb_push(skb, 4);

	{
		u_int16_t *p = (u_int16_t *) skb->data;

		*p = 0;	/* indicate inbound */
	}

	if (is->pass_filter
	    && BPF_PROG_RUN(is->pass_filter, skb) == 0) {
		if (is->debug & 0x2)
			printk(KERN_DEBUG "IPPP: inbound frame filtered.\n");
		kfree_skb(skb);
		return;
	}
	if (!(is->active_filter
	      && BPF_PROG_RUN(is->active_filter, skb) == 0)) {
		if (is->debug & 0x2)
			printk(KERN_DEBUG "IPPP: link-active filter: resetting huptimer.\n");
		lp->huptimer = 0;
		if (mlp)
			mlp->huptimer = 0;
	}
	skb_pull(skb, 4);
#else /* CONFIG_IPPP_FILTER */
	lp->huptimer = 0;
	if (mlp)
		mlp->huptimer = 0;
#endif /* CONFIG_IPPP_FILTER */
	skb->dev = dev;
	skb_reset_mac_header(skb);
	netif_rx(skb);
	/* net_dev->local->stats.rx_packets++; done in isdn_net.c */
	return;

drop_packet:
	net_dev->local->stats.rx_dropped++;
	kfree_skb(skb);
}

/*
 * isdn_ppp_skb_push ..
 * checks whether we have enough space at the beginning of the skb
 * and allocs a new SKB if necessary
 */
static unsigned char *isdn_ppp_skb_push(struct sk_buff **skb_p, int len)
{
	struct sk_buff *skb = *skb_p;

	if (skb_headroom(skb) < len) {
		struct sk_buff *nskb = skb_realloc_headroom(skb, len);

		if (!nskb) {
			printk(KERN_ERR "isdn_ppp_skb_push: can't realloc headroom!\n");
			dev_kfree_skb(skb);
			return NULL;
		}
		printk(KERN_DEBUG "isdn_ppp_skb_push:under %d %d\n", skb_headroom(skb), len);
		dev_kfree_skb(skb);
		*skb_p = nskb;
		return skb_push(nskb, len);
	}
	return skb_push(skb, len);
}

/*
 * send ppp frame .. we expect a PIDCOMPressable proto --
 *  (here: currently always PPP_IP,PPP_VJC_COMP,PPP_VJC_UNCOMP)
 *
 * VJ compression may change skb pointer!!! .. requeue with old
 * skb isn't allowed!!
 */

int
isdn_ppp_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	isdn_net_local *lp, *mlp;
	isdn_net_dev *nd;
	unsigned int proto = PPP_IP;     /* 0x21 */
	struct ippp_struct *ipt, *ipts;
	int slot, retval = NETDEV_TX_OK;

	mlp = netdev_priv(netdev);
	nd = mlp->netdev;       /* get master lp */

	slot = mlp->ppp_slot;
	if (slot < 0 || slot >= ISDN_MAX_CHANNELS) {
		printk(KERN_ERR "isdn_ppp_xmit: lp->ppp_slot(%d)\n",
		       mlp->ppp_slot);
		kfree_skb(skb);
		goto out;
	}
	ipts = ippp_table[slot];

	if (!(ipts->pppcfg & SC_ENABLE_IP)) {	/* PPP connected ? */
		if (ipts->debug & 0x1)
			printk(KERN_INFO "%s: IP frame delayed.\n", netdev->name);
		retval = NETDEV_TX_BUSY;
		goto out;
	}

	switch (ntohs(skb->protocol)) {
	case ETH_P_IP:
		proto = PPP_IP;
		break;
	case ETH_P_IPX:
		proto = PPP_IPX;	/* untested */
		break;
	default:
		printk(KERN_ERR "isdn_ppp: skipped unsupported protocol: %#x.\n",
		       skb->protocol);
		dev_kfree_skb(skb);
		goto out;
	}

	lp = isdn_net_get_locked_lp(nd);
	if (!lp) {
		printk(KERN_WARNING "%s: all channels busy - requeuing!\n", netdev->name);
		retval = NETDEV_TX_BUSY;
		goto out;
	}
	/* we have our lp locked from now on */

	slot = lp->ppp_slot;
	if (slot < 0 || slot >= ISDN_MAX_CHANNELS) {
		printk(KERN_ERR "isdn_ppp_xmit: lp->ppp_slot(%d)\n",
		       lp->ppp_slot);
		kfree_skb(skb);
		goto unlock;
	}
	ipt = ippp_table[slot];

	/*
	 * after this line .. requeueing in the device queue is no longer allowed!!!
	 */

	/* Pull off the fake header we stuck on earlier to keep
	 * the fragmentation code happy.
	 */
	skb_pull(skb, IPPP_MAX_HEADER);

#ifdef CONFIG_IPPP_FILTER
	/* check if we should pass this packet
	 * the filter instructions are constructed assuming
	 * a four-byte PPP header on each packet */
	*(u8 *)skb_push(skb, 4) = 1; /* indicate outbound */

	{
		__be16 *p = (__be16 *)skb->data;

		p++;
		*p = htons(proto);
	}

	if (ipt->pass_filter
	    && BPF_PROG_RUN(ipt->pass_filter, skb) == 0) {
		if (ipt->debug & 0x4)
			printk(KERN_DEBUG "IPPP: outbound frame filtered.\n");
		kfree_skb(skb);
		goto unlock;
	}
	if (!(ipt->active_filter
	      && BPF_PROG_RUN(ipt->active_filter, skb) == 0)) {
		if (ipt->debug & 0x4)
			printk(KERN_DEBUG "IPPP: link-active filter: resetting huptimer.\n");
		lp->huptimer = 0;
	}
	skb_pull(skb, 4);
#else /* CONFIG_IPPP_FILTER */
	lp->huptimer = 0;
#endif /* CONFIG_IPPP_FILTER */

	if (ipt->debug & 0x4)
		printk(KERN_DEBUG "xmit skb, len %d\n", (int) skb->len);
	if (ipts->debug & 0x40)
		isdn_ppp_frame_log("xmit0", skb->data, skb->len, 32, ipts->unit, lp->ppp_slot);

#ifdef CONFIG_ISDN_PPP_VJ
	if (proto == PPP_IP && ipts->pppcfg & SC_COMP_TCP) {	/* ipts here? probably yes, but check this again */
		struct sk_buff *new_skb;
		unsigned short hl;
		/*
		 * we need to reserve enough space in front of
		 * sk_buff. old call to dev_alloc_skb only reserved
		 * 16 bytes, now we are looking what the driver want.
		 */
		hl = dev->drv[lp->isdn_device]->interface->hl_hdrlen + IPPP_MAX_HEADER;
		/*
		 * Note: hl might still be insufficient because the method
		 * above does not account for a possibible MPPP slave channel
		 * which had larger HL header space requirements than the
		 * master.
		 */
		new_skb = alloc_skb(hl + skb->len, GFP_ATOMIC);
		if (new_skb) {
			u_char *buf;
			int pktlen;

			skb_reserve(new_skb, hl);
			new_skb->dev = skb->dev;
			skb_put(new_skb, skb->len);
			buf = skb->data;

			pktlen = slhc_compress(ipts->slcomp, skb->data, skb->len, new_skb->data,
					       &buf, !(ipts->pppcfg & SC_NO_TCP_CCID));

			if (buf != skb->data) {
				if (new_skb->data != buf)
					printk(KERN_ERR "isdn_ppp: FATAL error after slhc_compress!!\n");
				dev_kfree_skb(skb);
				skb = new_skb;
			} else {
				dev_kfree_skb(new_skb);
			}

			skb_trim(skb, pktlen);
			if (skb->data[0] & SL_TYPE_COMPRESSED_TCP) {	/* cslip? style -> PPP */
				proto = PPP_VJC_COMP;
				skb->data[0] ^= SL_TYPE_COMPRESSED_TCP;
			} else {
				if (skb->data[0] >= SL_TYPE_UNCOMPRESSED_TCP)
					proto = PPP_VJC_UNCOMP;
				skb->data[0] = (skb->data[0] & 0x0f) | 0x40;
			}
		}
	}
#endif

	/*
	 * normal (single link) or bundle compression
	 */
	if (ipts->compflags & SC_COMP_ON) {
		/* We send compressed only if both down- und upstream
		   compression is negotiated, that means, CCP is up */
		if (ipts->compflags & SC_DECOMP_ON) {
			skb = isdn_ppp_compress(skb, &proto, ipt, ipts, 0);
		} else {
			printk(KERN_DEBUG "isdn_ppp: CCP not yet up - sending as-is\n");
		}
	}

	if (ipt->debug & 0x24)
		printk(KERN_DEBUG "xmit2 skb, len %d, proto %04x\n", (int) skb->len, proto);

#ifdef CONFIG_ISDN_MPP
	if (ipt->mpppcfg & SC_MP_PROT) {
		/* we get mp_seqno from static isdn_net_local */
		long mp_seqno = ipts->mp_seqno;
		ipts->mp_seqno++;
		if (ipt->mpppcfg & SC_OUT_SHORT_SEQ) {
			unsigned char *data = isdn_ppp_skb_push(&skb, 3);
			if (!data)
				goto unlock;
			mp_seqno &= 0xfff;
			data[0] = MP_BEGIN_FRAG | MP_END_FRAG | ((mp_seqno >> 8) & 0xf);	/* (B)egin & (E)ndbit .. */
			data[1] = mp_seqno & 0xff;
			data[2] = proto;	/* PID compression */
		} else {
			unsigned char *data = isdn_ppp_skb_push(&skb, 5);
			if (!data)
				goto unlock;
			data[0] = MP_BEGIN_FRAG | MP_END_FRAG;	/* (B)egin & (E)ndbit .. */
			data[1] = (mp_seqno >> 16) & 0xff;	/* sequence number: 24bit */
			data[2] = (mp_seqno >> 8) & 0xff;
			data[3] = (mp_seqno >> 0) & 0xff;
			data[4] = proto;	/* PID compression */
		}
		proto = PPP_MP; /* MP Protocol, 0x003d */
	}
#endif

	/*
	 * 'link in bundle' compression  ...
	 */
	if (ipt->compflags & SC_LINK_COMP_ON)
		skb = isdn_ppp_compress(skb, &proto, ipt, ipts, 1);

	if ((ipt->pppcfg & SC_COMP_PROT) && (proto <= 0xff)) {
		unsigned char *data = isdn_ppp_skb_push(&skb, 1);
		if (!data)
			goto unlock;
		data[0] = proto & 0xff;
	}
	else {
		unsigned char *data = isdn_ppp_skb_push(&skb, 2);
		if (!data)
			goto unlock;
		data[0] = (proto >> 8) & 0xff;
		data[1] = proto & 0xff;
	}
	if (!(ipt->pppcfg & SC_COMP_AC)) {
		unsigned char *data = isdn_ppp_skb_push(&skb, 2);
		if (!data)
			goto unlock;
		data[0] = 0xff;    /* All Stations */
		data[1] = 0x03;    /* Unnumbered information */
	}

	/* tx-stats are now updated via BSENT-callback */

	if (ipts->debug & 0x40) {
		printk(KERN_DEBUG "skb xmit: len: %d\n", (int) skb->len);
		isdn_ppp_frame_log("xmit", skb->data, skb->len, 32, ipt->unit, lp->ppp_slot);
	}

	isdn_net_writebuf_skb(lp, skb);

unlock:
	spin_unlock_bh(&lp->xmit_lock);
out:
	return retval;
}

#ifdef CONFIG_IPPP_FILTER
/*
 * check if this packet may trigger auto-dial.
 */

int isdn_ppp_autodial_filter(struct sk_buff *skb, isdn_net_local *lp)
{
	struct ippp_struct *is = ippp_table[lp->ppp_slot];
	u_int16_t proto;
	int drop = 0;

	switch (ntohs(skb->protocol)) {
	case ETH_P_IP:
		proto = PPP_IP;
		break;
	case ETH_P_IPX:
		proto = PPP_IPX;
		break;
	default:
		printk(KERN_ERR "isdn_ppp_autodial_filter: unsupported protocol 0x%x.\n",
		       skb->protocol);
		return 1;
	}

	/* the filter instructions are constructed assuming
	 * a four-byte PPP header on each packet. we have to
	 * temporarily remove part of the fake header stuck on
	 * earlier.
	 */
	*(u8 *)skb_pull(skb, IPPP_MAX_HEADER - 4) = 1; /* indicate outbound */

	{
		__be16 *p = (__be16 *)skb->data;

		p++;
		*p = htons(proto);
	}

	drop |= is->pass_filter
		&& BPF_PROG_RUN(is->pass_filter, skb) == 0;
	drop |= is->active_filter
		&& BPF_PROG_RUN(is->active_filter, skb) == 0;

	skb_push(skb, IPPP_MAX_HEADER - 4);
	return drop;
}
#endif
#ifdef CONFIG_ISDN_MPP

/* this is _not_ rfc1990 header, but something we convert both short and long
 * headers to for convinience's sake:
 *	byte 0 is flags as in rfc1990
 *	bytes 1...4 is 24-bit seqence number converted to host byte order
 */
#define MP_HEADER_LEN	5

#define MP_LONGSEQ_MASK		0x00ffffff
#define MP_SHORTSEQ_MASK	0x00000fff
#define MP_LONGSEQ_MAX		MP_LONGSEQ_MASK
#define MP_SHORTSEQ_MAX		MP_SHORTSEQ_MASK
#define MP_LONGSEQ_MAXBIT	((MP_LONGSEQ_MASK + 1) >> 1)
#define MP_SHORTSEQ_MAXBIT	((MP_SHORTSEQ_MASK + 1) >> 1)

/* sequence-wrap safe comparisons (for long sequence)*/
#define MP_LT(a, b)	((a - b) & MP_LONGSEQ_MAXBIT)
#define MP_LE(a, b)	!((b - a) & MP_LONGSEQ_MAXBIT)
#define MP_GT(a, b)	((b - a) & MP_LONGSEQ_MAXBIT)
#define MP_GE(a, b)	!((a - b) & MP_LONGSEQ_MAXBIT)

#define MP_SEQ(f)	((*(u32 *)(f->data + 1)))
#define MP_FLAGS(f)	(f->data[0])

static int isdn_ppp_mp_bundle_array_init(void)
{
	int i;
	int sz = ISDN_MAX_CHANNELS * sizeof(ippp_bundle);
	if ((isdn_ppp_bundle_arr = kzalloc(sz, GFP_KERNEL)) == NULL)
		return -ENOMEM;
	for (i = 0; i < ISDN_MAX_CHANNELS; i++)
		spin_lock_init(&isdn_ppp_bundle_arr[i].lock);
	return 0;
}

static ippp_bundle *isdn_ppp_mp_bundle_alloc(void)
{
	int i;
	for (i = 0; i < ISDN_MAX_CHANNELS; i++)
		if (isdn_ppp_bundle_arr[i].ref_ct <= 0)
			return (isdn_ppp_bundle_arr + i);
	return NULL;
}

static int isdn_ppp_mp_init(isdn_net_local *lp, ippp_bundle *add_to)
{
	struct ippp_struct *is;

	if (lp->ppp_slot < 0) {
		printk(KERN_ERR "%s: lp->ppp_slot(%d) out of range\n",
		       __func__, lp->ppp_slot);
		return (-EINVAL);
	}

	is = ippp_table[lp->ppp_slot];
	if (add_to) {
		if (lp->netdev->pb)
			lp->netdev->pb->ref_ct--;
		lp->netdev->pb = add_to;
	} else {		/* first link in a bundle */
		is->mp_seqno = 0;
		if ((lp->netdev->pb = isdn_ppp_mp_bundle_alloc()) == NULL)
			return -ENOMEM;
		lp->next = lp->last = lp;	/* nobody else in a queue */
		lp->netdev->pb->frags = NULL;
		lp->netdev->pb->frames = 0;
		lp->netdev->pb->seq = UINT_MAX;
	}
	lp->netdev->pb->ref_ct++;

	is->last_link_seqno = 0;
	return 0;
}

static u32 isdn_ppp_mp_get_seq(int short_seq,
			       struct sk_buff *skb, u32 last_seq);
static struct sk_buff *isdn_ppp_mp_discard(ippp_bundle *mp,
					   struct sk_buff *from, struct sk_buff *to);
static void isdn_ppp_mp_reassembly(isdn_net_dev *net_dev, isdn_net_local *lp,
				   struct sk_buff *from, struct sk_buff *to);
static void isdn_ppp_mp_free_skb(ippp_bundle *mp, struct sk_buff *skb);
static void isdn_ppp_mp_print_recv_pkt(int slot, struct sk_buff *skb);

static void isdn_ppp_mp_receive(isdn_net_dev *net_dev, isdn_net_local *lp,
				struct sk_buff *skb)
{
	struct ippp_struct *is;
	isdn_net_local *lpq;
	ippp_bundle *mp;
	isdn_mppp_stats *stats;
	struct sk_buff *newfrag, *frag, *start, *nextf;
	u32 newseq, minseq, thisseq;
	unsigned long flags;
	int slot;

	spin_lock_irqsave(&net_dev->pb->lock, flags);
	mp = net_dev->pb;
	stats = &mp->stats;
	slot = lp->ppp_slot;
	if (slot < 0 || slot >= ISDN_MAX_CHANNELS) {
		printk(KERN_ERR "%s: lp->ppp_slot(%d)\n",
		       __func__, lp->ppp_slot);
		stats->frame_drops++;
		dev_kfree_skb(skb);
		spin_unlock_irqrestore(&mp->lock, flags);
		return;
	}
	is = ippp_table[slot];
	if (++mp->frames > stats->max_queue_len)
		stats->max_queue_len = mp->frames;

	if (is->debug & 0x8)
		isdn_ppp_mp_print_recv_pkt(lp->ppp_slot, skb);

	newseq = isdn_ppp_mp_get_seq(is->mpppcfg & SC_IN_SHORT_SEQ,
				     skb, is->last_link_seqno);


	/* if this packet seq # is less than last already processed one,
	 * toss it right away, but check for sequence start case first
	 */
	if (mp->seq > MP_LONGSEQ_MAX && (newseq & MP_LONGSEQ_MAXBIT)) {
		mp->seq = newseq;	/* the first packet: required for
					 * rfc1990 non-compliant clients --
					 * prevents constant packet toss */
	} else if (MP_LT(newseq, mp->seq)) {
		stats->frame_drops++;
		isdn_ppp_mp_free_skb(mp, skb);
		spin_unlock_irqrestore(&mp->lock, flags);
		return;
	}

	/* find the minimum received sequence number over all links */
	is->last_link_seqno = minseq = newseq;
	for (lpq = net_dev->queue;;) {
		slot = lpq->ppp_slot;
		if (slot < 0 || slot >= ISDN_MAX_CHANNELS) {
			printk(KERN_ERR "%s: lpq->ppp_slot(%d)\n",
			       __func__, lpq->ppp_slot);
		} else {
			u32 lls = ippp_table[slot]->last_link_seqno;
			if (MP_LT(lls, minseq))
				minseq = lls;
		}
		if ((lpq = lpq->next) == net_dev->queue)
			break;
	}
	if (MP_LT(minseq, mp->seq))
		minseq = mp->seq;	/* can't go beyond already processed
					 * packets */
	newfrag = skb;

	/* if this new fragment is before the first one, then enqueue it now. */
	if ((frag = mp->frags) == NULL || MP_LT(newseq, MP_SEQ(frag))) {
		newfrag->next = frag;
		mp->frags = frag = newfrag;
		newfrag = NULL;
	}

	start = MP_FLAGS(frag) & MP_BEGIN_FRAG &&
		MP_SEQ(frag) == mp->seq ? frag : NULL;

	/*
	 * main fragment traversing loop
	 *
	 * try to accomplish several tasks:
	 * - insert new fragment into the proper sequence slot (once that's done
	 *   newfrag will be set to NULL)
	 * - reassemble any complete fragment sequence (non-null 'start'
	 *   indicates there is a contiguous sequence present)
	 * - discard any incomplete sequences that are below minseq -- due
	 *   to the fact that sender always increment sequence number, if there
	 *   is an incomplete sequence below minseq, no new fragments would
	 *   come to complete such sequence and it should be discarded
	 *
	 * loop completes when we accomplished the following tasks:
	 * - new fragment is inserted in the proper sequence ('newfrag' is
	 *   set to NULL)
	 * - we hit a gap in the sequence, so no reassembly/processing is
	 *   possible ('start' would be set to NULL)
	 *
	 * algorithm for this code is derived from code in the book
	 * 'PPP Design And Debugging' by James Carlson (Addison-Wesley)
	 */
	while (start != NULL || newfrag != NULL) {

		thisseq = MP_SEQ(frag);
		nextf = frag->next;

		/* drop any duplicate fragments */
		if (newfrag != NULL && thisseq == newseq) {
			isdn_ppp_mp_free_skb(mp, newfrag);
			newfrag = NULL;
		}

		/* insert new fragment before next element if possible. */
		if (newfrag != NULL && (nextf == NULL ||
					MP_LT(newseq, MP_SEQ(nextf)))) {
			newfrag->next = nextf;
			frag->next = nextf = newfrag;
			newfrag = NULL;
		}

		if (start != NULL) {
			/* check for misplaced start */
			if (start != frag && (MP_FLAGS(frag) & MP_BEGIN_FRAG)) {
				printk(KERN_WARNING"isdn_mppp(seq %d): new "
				       "BEGIN flag with no prior END", thisseq);
				stats->seqerrs++;
				stats->frame_drops++;
				start = isdn_ppp_mp_discard(mp, start, frag);
				nextf = frag->next;
			}
		} else if (MP_LE(thisseq, minseq)) {
			if (MP_FLAGS(frag) & MP_BEGIN_FRAG)
				start = frag;
			else {
				if (MP_FLAGS(frag) & MP_END_FRAG)
					stats->frame_drops++;
				if (mp->frags == frag)
					mp->frags = nextf;
				isdn_ppp_mp_free_skb(mp, frag);
				frag = nextf;
				continue;
			}
		}

		/* if start is non-null and we have end fragment, then
		 * we have full reassembly sequence -- reassemble
		 * and process packet now
		 */
		if (start != NULL && (MP_FLAGS(frag) & MP_END_FRAG)) {
			minseq = mp->seq = (thisseq + 1) & MP_LONGSEQ_MASK;
			/* Reassemble the packet then dispatch it */
			isdn_ppp_mp_reassembly(net_dev, lp, start, nextf);

			start = NULL;
			frag = NULL;

			mp->frags = nextf;
		}

		/* check if need to update start pointer: if we just
		 * reassembled the packet and sequence is contiguous
		 * then next fragment should be the start of new reassembly
		 * if sequence is contiguous, but we haven't reassembled yet,
		 * keep going.
		 * if sequence is not contiguous, either clear everything
		 * below low watermark and set start to the next frag or
		 * clear start ptr.
		 */
		if (nextf != NULL &&
		    ((thisseq + 1) & MP_LONGSEQ_MASK) == MP_SEQ(nextf)) {
			/* if we just reassembled and the next one is here,
			 * then start another reassembly. */

			if (frag == NULL) {
				if (MP_FLAGS(nextf) & MP_BEGIN_FRAG)
					start = nextf;
				else
				{
					printk(KERN_WARNING"isdn_mppp(seq %d):"
					       " END flag with no following "
					       "BEGIN", thisseq);
					stats->seqerrs++;
				}
			}

		} else {
			if (nextf != NULL && frag != NULL &&
			    MP_LT(thisseq, minseq)) {
				/* we've got a break in the sequence
				 * and we not at the end yet
				 * and we did not just reassembled
				 *(if we did, there wouldn't be anything before)
				 * and we below the low watermark
				 * discard all the frames below low watermark
				 * and start over */
				stats->frame_drops++;
				mp->frags = isdn_ppp_mp_discard(mp, start, nextf);
			}
			/* break in the sequence, no reassembly */
			start = NULL;
		}

		frag = nextf;
	}	/* while -- main loop */

	if (mp->frags == NULL)
		mp->frags = frag;

	/* rather straighforward way to deal with (not very) possible
	 * queue overflow */
	if (mp->frames > MP_MAX_QUEUE_LEN) {
		stats->overflows++;
		while (mp->frames > MP_MAX_QUEUE_LEN) {
			frag = mp->frags->next;
			isdn_ppp_mp_free_skb(mp, mp->frags);
			mp->frags = frag;
		}
	}
	spin_unlock_irqrestore(&mp->lock, flags);
}

static void isdn_ppp_mp_cleanup(isdn_net_local *lp)
{
	struct sk_buff *frag = lp->netdev->pb->frags;
	struct sk_buff *nextfrag;
	while (frag) {
		nextfrag = frag->next;
		isdn_ppp_mp_free_skb(lp->netdev->pb, frag);
		frag = nextfrag;
	}
	lp->netdev->pb->frags = NULL;
}

static u32 isdn_ppp_mp_get_seq(int short_seq,
			       struct sk_buff *skb, u32 last_seq)
{
	u32 seq;
	int flags = skb->data[0] & (MP_BEGIN_FRAG | MP_END_FRAG);

	if (!short_seq)
	{
		seq = ntohl(*(__be32 *)skb->data) & MP_LONGSEQ_MASK;
		skb_push(skb, 1);
	}
	else
	{
		/* convert 12-bit short seq number to 24-bit long one
		 */
		seq = ntohs(*(__be16 *)skb->data) & MP_SHORTSEQ_MASK;

		/* check for seqence wrap */
		if (!(seq &  MP_SHORTSEQ_MAXBIT) &&
		    (last_seq &  MP_SHORTSEQ_MAXBIT) &&
		    (unsigned long)last_seq <= MP_LONGSEQ_MAX)
			seq |= (last_seq + MP_SHORTSEQ_MAX + 1) &
				(~MP_SHORTSEQ_MASK & MP_LONGSEQ_MASK);
		else
			seq |= last_seq & (~MP_SHORTSEQ_MASK & MP_LONGSEQ_MASK);

		skb_push(skb, 3);	/* put converted seqence back in skb */
	}
	*(u32 *)(skb->data + 1) = seq;	/* put seqence back in _host_ byte
					 * order */
	skb->data[0] = flags;	        /* restore flags */
	return seq;
}

struct sk_buff *isdn_ppp_mp_discard(ippp_bundle *mp,
				    struct sk_buff *from, struct sk_buff *to)
{
	if (from)
		while (from != to) {
			struct sk_buff *next = from->next;
			isdn_ppp_mp_free_skb(mp, from);
			from = next;
		}
	return from;
}

void isdn_ppp_mp_reassembly(isdn_net_dev *net_dev, isdn_net_local *lp,
			    struct sk_buff *from, struct sk_buff *to)
{
	ippp_bundle *mp = net_dev->pb;
	int proto;
	struct sk_buff *skb;
	unsigned int tot_len;

	if (lp->ppp_slot < 0 || lp->ppp_slot >= ISDN_MAX_CHANNELS) {
		printk(KERN_ERR "%s: lp->ppp_slot(%d) out of range\n",
		       __func__, lp->ppp_slot);
		return;
	}
	if (MP_FLAGS(from) == (MP_BEGIN_FRAG | MP_END_FRAG)) {
		if (ippp_table[lp->ppp_slot]->debug & 0x40)
			printk(KERN_DEBUG "isdn_mppp: reassembly: frame %d, "
			       "len %d\n", MP_SEQ(from), from->len);
		skb = from;
		skb_pull(skb, MP_HEADER_LEN);
		mp->frames--;
	} else {
		struct sk_buff *frag;
		int n;

		for (tot_len = n = 0, frag = from; frag != to; frag = frag->next, n++)
			tot_len += frag->len - MP_HEADER_LEN;

		if (ippp_table[lp->ppp_slot]->debug & 0x40)
			printk(KERN_DEBUG"isdn_mppp: reassembling frames %d "
			       "to %d, len %d\n", MP_SEQ(from),
			       (MP_SEQ(from) + n - 1) & MP_LONGSEQ_MASK, tot_len);
		if ((skb = dev_alloc_skb(tot_len)) == NULL) {
			printk(KERN_ERR "isdn_mppp: cannot allocate sk buff "
			       "of size %d\n", tot_len);
			isdn_ppp_mp_discard(mp, from, to);
			return;
		}

		while (from != to) {
			unsigned int len = from->len - MP_HEADER_LEN;

			skb_copy_from_linear_data_offset(from, MP_HEADER_LEN,
							 skb_put(skb, len),
							 len);
			frag = from->next;
			isdn_ppp_mp_free_skb(mp, from);
			from = frag;
		}
	}
	proto = isdn_ppp_strip_proto(skb);
	isdn_ppp_push_higher(net_dev, lp, skb, proto);
}

static void isdn_ppp_mp_free_skb(ippp_bundle *mp, struct sk_buff *skb)
{
	dev_kfree_skb(skb);
	mp->frames--;
}

static void isdn_ppp_mp_print_recv_pkt(int slot, struct sk_buff *skb)
{
	printk(KERN_DEBUG "mp_recv: %d/%d -> %02x %02x %02x %02x %02x %02x\n",
	       slot, (int) skb->len,
	       (int) skb->data[0], (int) skb->data[1], (int) skb->data[2],
	       (int) skb->data[3], (int) skb->data[4], (int) skb->data[5]);
}

static int
isdn_ppp_bundle(struct ippp_struct *is, int unit)
{
	char ifn[IFNAMSIZ + 1];
	isdn_net_dev *p;
	isdn_net_local *lp, *nlp;
	int rc;
	unsigned long flags;

	sprintf(ifn, "ippp%d", unit);
	p = isdn_net_findif(ifn);
	if (!p) {
		printk(KERN_ERR "ippp_bundle: cannot find %s\n", ifn);
		return -EINVAL;
	}

	spin_lock_irqsave(&p->pb->lock, flags);

	nlp = is->lp;
	lp = p->queue;
	if (nlp->ppp_slot < 0 || nlp->ppp_slot >= ISDN_MAX_CHANNELS ||
	    lp->ppp_slot < 0 || lp->ppp_slot >= ISDN_MAX_CHANNELS) {
		printk(KERN_ERR "ippp_bundle: binding to invalid slot %d\n",
		       nlp->ppp_slot < 0 || nlp->ppp_slot >= ISDN_MAX_CHANNELS ?
		       nlp->ppp_slot : lp->ppp_slot);
		rc = -EINVAL;
		goto out;
	}

	isdn_net_add_to_bundle(p, nlp);

	ippp_table[nlp->ppp_slot]->unit = ippp_table[lp->ppp_slot]->unit;

	/* maybe also SC_CCP stuff */
	ippp_table[nlp->ppp_slot]->pppcfg |= ippp_table[lp->ppp_slot]->pppcfg &
		(SC_ENABLE_IP | SC_NO_TCP_CCID | SC_REJ_COMP_TCP);
	ippp_table[nlp->ppp_slot]->mpppcfg |= ippp_table[lp->ppp_slot]->mpppcfg &
		(SC_MP_PROT | SC_REJ_MP_PROT | SC_OUT_SHORT_SEQ | SC_IN_SHORT_SEQ);
	rc = isdn_ppp_mp_init(nlp, p->pb);
out:
	spin_unlock_irqrestore(&p->pb->lock, flags);
	return rc;
}

#endif /* CONFIG_ISDN_MPP */

/*
 * network device ioctl handlers
 */

static int
isdn_ppp_dev_ioctl_stats(int slot, struct ifreq *ifr, struct net_device *dev)
{
	struct ppp_stats __user *res = ifr->ifr_data;
	struct ppp_stats t;
	isdn_net_local *lp = netdev_priv(dev);

	/* build a temporary stat struct and copy it to user space */

	memset(&t, 0, sizeof(struct ppp_stats));
	if (dev->flags & IFF_UP) {
		t.p.ppp_ipackets = lp->stats.rx_packets;
		t.p.ppp_ibytes = lp->stats.rx_bytes;
		t.p.ppp_ierrors = lp->stats.rx_errors;
		t.p.ppp_opackets = lp->stats.tx_packets;
		t.p.ppp_obytes = lp->stats.tx_bytes;
		t.p.ppp_oerrors = lp->stats.tx_errors;
#ifdef CONFIG_ISDN_PPP_VJ
		if (slot >= 0 && ippp_table[slot]->slcomp) {
			struct slcompress *slcomp = ippp_table[slot]->slcomp;
			t.vj.vjs_packets = slcomp->sls_o_compressed + slcomp->sls_o_uncompressed;
			t.vj.vjs_compressed = slcomp->sls_o_compressed;
			t.vj.vjs_searches = slcomp->sls_o_searches;
			t.vj.vjs_misses = slcomp->sls_o_misses;
			t.vj.vjs_errorin = slcomp->sls_i_error;
			t.vj.vjs_tossed = slcomp->sls_i_tossed;
			t.vj.vjs_uncompressedin = slcomp->sls_i_uncompressed;
			t.vj.vjs_compressedin = slcomp->sls_i_compressed;
		}
#endif
	}
	if (copy_to_user(res, &t, sizeof(struct ppp_stats)))
		return -EFAULT;
	return 0;
}

int
isdn_ppp_dev_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	int error = 0;
	int len;
	isdn_net_local *lp = netdev_priv(dev);


	if (lp->p_encap != ISDN_NET_ENCAP_SYNCPPP)
		return -EINVAL;

	switch (cmd) {
#define PPP_VERSION "2.3.7"
	case SIOCGPPPVER:
		len = strlen(PPP_VERSION) + 1;
		if (copy_to_user(ifr->ifr_data, PPP_VERSION, len))
			error = -EFAULT;
		break;

	case SIOCGPPPSTATS:
		error = isdn_ppp_dev_ioctl_stats(lp->ppp_slot, ifr, dev);
		break;
	default:
		error = -EINVAL;
		break;
	}
	return error;
}

static int
isdn_ppp_if_get_unit(char *name)
{
	int len,
		i,
		unit = 0,
		deci;

	len = strlen(name);

	if (strncmp("ippp", name, 4) || len > 8)
		return -1;

	for (i = 0, deci = 1; i < len; i++, deci *= 10) {
		char a = name[len - i - 1];
		if (a >= '0' && a <= '9')
			unit += (a - '0') * deci;
		else
			break;
	}
	if (!i || len - i != 4)
		unit = -1;

	return unit;
}


int
isdn_ppp_dial_slave(char *name)
{
#ifdef CONFIG_ISDN_MPP
	isdn_net_dev *ndev;
	isdn_net_local *lp;
	struct net_device *sdev;

	if (!(ndev = isdn_net_findif(name)))
		return 1;
	lp = ndev->local;
	if (!(lp->flags & ISDN_NET_CONNECTED))
		return 5;

	sdev = lp->slave;
	while (sdev) {
		isdn_net_local *mlp = netdev_priv(sdev);
		if (!(mlp->flags & ISDN_NET_CONNECTED))
			break;
		sdev = mlp->slave;
	}
	if (!sdev)
		return 2;

	isdn_net_dial_req(netdev_priv(sdev));
	return 0;
#else
	return -1;
#endif
}

int
isdn_ppp_hangup_slave(char *name)
{
#ifdef CONFIG_ISDN_MPP
	isdn_net_dev *ndev;
	isdn_net_local *lp;
	struct net_device *sdev;

	if (!(ndev = isdn_net_findif(name)))
		return 1;
	lp = ndev->local;
	if (!(lp->flags & ISDN_NET_CONNECTED))
		return 5;

	sdev = lp->slave;
	while (sdev) {
		isdn_net_local *mlp = netdev_priv(sdev);

		if (mlp->slave) { /* find last connected link in chain */
			isdn_net_local *nlp = ISDN_SLAVE_PRIV(mlp);

			if (!(nlp->flags & ISDN_NET_CONNECTED))
				break;
		} else if (mlp->flags & ISDN_NET_CONNECTED)
			break;

		sdev = mlp->slave;
	}
	if (!sdev)
		return 2;

	isdn_net_hangup(sdev);
	return 0;
#else
	return -1;
#endif
}

/*
 * PPP compression stuff
 */


/* Push an empty CCP Data Frame up to the daemon to wake it up and let it
   generate a CCP Reset-Request or tear down CCP altogether */

static void isdn_ppp_ccp_kickup(struct ippp_struct *is)
{
	isdn_ppp_fill_rq(NULL, 0, PPP_COMP, is->lp->ppp_slot);
}

/* In-kernel handling of CCP Reset-Request and Reset-Ack is necessary,
   but absolutely nontrivial. The most abstruse problem we are facing is
   that the generation, reception and all the handling of timeouts and
   resends including proper request id management should be entirely left
   to the (de)compressor, but indeed is not covered by the current API to
   the (de)compressor. The API is a prototype version from PPP where only
   some (de)compressors have yet been implemented and all of them are
   rather simple in their reset handling. Especially, their is only one
   outstanding ResetAck at a time with all of them and ResetReq/-Acks do
   not have parameters. For this very special case it was sufficient to
   just return an error code from the decompressor and have a single
   reset() entry to communicate all the necessary information between
   the framework and the (de)compressor. Bad enough, LZS is different
   (and any other compressor may be different, too). It has multiple
   histories (eventually) and needs to Reset each of them independently
   and thus uses multiple outstanding Acks and history numbers as an
   additional parameter to Reqs/Acks.
   All that makes it harder to port the reset state engine into the
   kernel because it is not just the same simple one as in (i)pppd but
   it must be able to pass additional parameters and have multiple out-
   standing Acks. We are trying to achieve the impossible by handling
   reset transactions independent by their id. The id MUST change when
   the data portion changes, thus any (de)compressor who uses more than
   one resettable state must provide and recognize individual ids for
   each individual reset transaction. The framework itself does _only_
   differentiate them by id, because it has no other semantics like the
   (de)compressor might.
   This looks like a major redesign of the interface would be nice,
   but I don't have an idea how to do it better. */

/* Send a CCP Reset-Request or Reset-Ack directly from the kernel. This is
   getting that lengthy because there is no simple "send-this-frame-out"
   function above but every wrapper does a bit different. Hope I guess
   correct in this hack... */

static void isdn_ppp_ccp_xmit_reset(struct ippp_struct *is, int proto,
				    unsigned char code, unsigned char id,
				    unsigned char *data, int len)
{
	struct sk_buff *skb;
	unsigned char *p;
	int hl;
	int cnt = 0;
	isdn_net_local *lp = is->lp;

	/* Alloc large enough skb */
	hl = dev->drv[lp->isdn_device]->interface->hl_hdrlen;
	skb = alloc_skb(len + hl + 16, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_WARNING
		       "ippp: CCP cannot send reset - out of memory\n");
		return;
	}
	skb_reserve(skb, hl);

	/* We may need to stuff an address and control field first */
	if (!(is->pppcfg & SC_COMP_AC)) {
		p = skb_put(skb, 2);
		*p++ = 0xff;
		*p++ = 0x03;
	}

	/* Stuff proto, code, id and length */
	p = skb_put(skb, 6);
	*p++ = (proto >> 8);
	*p++ = (proto & 0xff);
	*p++ = code;
	*p++ = id;
	cnt = 4 + len;
	*p++ = (cnt >> 8);
	*p++ = (cnt & 0xff);

	/* Now stuff remaining bytes */
	if (len) {
		skb_put_data(skb, data, len);
	}

	/* skb is now ready for xmit */
	printk(KERN_DEBUG "Sending CCP Frame:\n");
	isdn_ppp_frame_log("ccp-xmit", skb->data, skb->len, 32, is->unit, lp->ppp_slot);

	isdn_net_write_super(lp, skb);
}

/* Allocate the reset state vector */
static struct ippp_ccp_reset *isdn_ppp_ccp_reset_alloc(struct ippp_struct *is)
{
	struct ippp_ccp_reset *r;
	r = kzalloc(sizeof(struct ippp_ccp_reset), GFP_KERNEL);
	if (!r) {
		printk(KERN_ERR "ippp_ccp: failed to allocate reset data"
		       " structure - no mem\n");
		return NULL;
	}
	printk(KERN_DEBUG "ippp_ccp: allocated reset data structure %p\n", r);
	is->reset = r;
	return r;
}

/* Destroy the reset state vector. Kill all pending timers first. */
static void isdn_ppp_ccp_reset_free(struct ippp_struct *is)
{
	unsigned int id;

	printk(KERN_DEBUG "ippp_ccp: freeing reset data structure %p\n",
	       is->reset);
	for (id = 0; id < 256; id++) {
		if (is->reset->rs[id]) {
			isdn_ppp_ccp_reset_free_state(is, (unsigned char)id);
		}
	}
	kfree(is->reset);
	is->reset = NULL;
}

/* Free a given state and clear everything up for later reallocation */
static void isdn_ppp_ccp_reset_free_state(struct ippp_struct *is,
					  unsigned char id)
{
	struct ippp_ccp_reset_state *rs;

	if (is->reset->rs[id]) {
		printk(KERN_DEBUG "ippp_ccp: freeing state for id %d\n", id);
		rs = is->reset->rs[id];
		/* Make sure the kernel will not call back later */
		if (rs->ta)
			del_timer(&rs->timer);
		is->reset->rs[id] = NULL;
		kfree(rs);
	} else {
		printk(KERN_WARNING "ippp_ccp: id %d is not allocated\n", id);
	}
}

/* The timer callback function which is called when a ResetReq has timed out,
   aka has never been answered by a ResetAck */
static void isdn_ppp_ccp_timer_callback(unsigned long closure)
{
	struct ippp_ccp_reset_state *rs =
		(struct ippp_ccp_reset_state *)closure;

	if (!rs) {
		printk(KERN_ERR "ippp_ccp: timer cb with zero closure.\n");
		return;
	}
	if (rs->ta && rs->state == CCPResetSentReq) {
		/* We are correct here */
		if (!rs->expra) {
			/* Hmm, there is no Ack really expected. We can clean
			   up the state now, it will be reallocated if the
			   decompressor insists on another reset */
			rs->ta = 0;
			isdn_ppp_ccp_reset_free_state(rs->is, rs->id);
			return;
		}
		printk(KERN_DEBUG "ippp_ccp: CCP Reset timed out for id %d\n",
		       rs->id);
		/* Push it again */
		isdn_ppp_ccp_xmit_reset(rs->is, PPP_CCP, CCP_RESETREQ, rs->id,
					rs->data, rs->dlen);
		/* Restart timer */
		rs->timer.expires = jiffies + HZ * 5;
		add_timer(&rs->timer);
	} else {
		printk(KERN_WARNING "ippp_ccp: timer cb in wrong state %d\n",
		       rs->state);
	}
}

/* Allocate a new reset transaction state */
static struct ippp_ccp_reset_state *isdn_ppp_ccp_reset_alloc_state(struct ippp_struct *is,
								   unsigned char id)
{
	struct ippp_ccp_reset_state *rs;
	if (is->reset->rs[id]) {
		printk(KERN_WARNING "ippp_ccp: old state exists for id %d\n",
		       id);
		return NULL;
	} else {
		rs = kzalloc(sizeof(struct ippp_ccp_reset_state), GFP_ATOMIC);
		if (!rs)
			return NULL;
		rs->state = CCPResetIdle;
		rs->is = is;
		rs->id = id;
		setup_timer(&rs->timer, isdn_ppp_ccp_timer_callback,
			    (unsigned long)rs);
		is->reset->rs[id] = rs;
	}
	return rs;
}


/* A decompressor wants a reset with a set of parameters - do what is
   necessary to fulfill it */
static void isdn_ppp_ccp_reset_trans(struct ippp_struct *is,
				     struct isdn_ppp_resetparams *rp)
{
	struct ippp_ccp_reset_state *rs;

	if (rp->valid) {
		/* The decompressor defines parameters by itself */
		if (rp->rsend) {
			/* And he wants us to send a request */
			if (!(rp->idval)) {
				printk(KERN_ERR "ippp_ccp: decompressor must"
				       " specify reset id\n");
				return;
			}
			if (is->reset->rs[rp->id]) {
				/* There is already a transaction in existence
				   for this id. May be still waiting for a
				   Ack or may be wrong. */
				rs = is->reset->rs[rp->id];
				if (rs->state == CCPResetSentReq && rs->ta) {
					printk(KERN_DEBUG "ippp_ccp: reset"
					       " trans still in progress"
					       " for id %d\n", rp->id);
				} else {
					printk(KERN_WARNING "ippp_ccp: reset"
					       " trans in wrong state %d for"
					       " id %d\n", rs->state, rp->id);
				}
			} else {
				/* Ok, this is a new transaction */
				printk(KERN_DEBUG "ippp_ccp: new trans for id"
				       " %d to be started\n", rp->id);
				rs = isdn_ppp_ccp_reset_alloc_state(is, rp->id);
				if (!rs) {
					printk(KERN_ERR "ippp_ccp: out of mem"
					       " allocing ccp trans\n");
					return;
				}
				rs->state = CCPResetSentReq;
				rs->expra = rp->expra;
				if (rp->dtval) {
					rs->dlen = rp->dlen;
					memcpy(rs->data, rp->data, rp->dlen);
				}
				/* HACK TODO - add link comp here */
				isdn_ppp_ccp_xmit_reset(is, PPP_CCP,
							CCP_RESETREQ, rs->id,
							rs->data, rs->dlen);
				/* Start the timer */
				rs->timer.expires = jiffies + 5 * HZ;
				add_timer(&rs->timer);
				rs->ta = 1;
			}
		} else {
			printk(KERN_DEBUG "ippp_ccp: no reset sent\n");
		}
	} else {
		/* The reset params are invalid. The decompressor does not
		   care about them, so we just send the minimal requests
		   and increase ids only when an Ack is received for a
		   given id */
		if (is->reset->rs[is->reset->lastid]) {
			/* There is already a transaction in existence
			   for this id. May be still waiting for a
			   Ack or may be wrong. */
			rs = is->reset->rs[is->reset->lastid];
			if (rs->state == CCPResetSentReq && rs->ta) {
				printk(KERN_DEBUG "ippp_ccp: reset"
				       " trans still in progress"
				       " for id %d\n", rp->id);
			} else {
				printk(KERN_WARNING "ippp_ccp: reset"
				       " trans in wrong state %d for"
				       " id %d\n", rs->state, rp->id);
			}
		} else {
			printk(KERN_DEBUG "ippp_ccp: new trans for id"
			       " %d to be started\n", is->reset->lastid);
			rs = isdn_ppp_ccp_reset_alloc_state(is,
							    is->reset->lastid);
			if (!rs) {
				printk(KERN_ERR "ippp_ccp: out of mem"
				       " allocing ccp trans\n");
				return;
			}
			rs->state = CCPResetSentReq;
			/* We always expect an Ack if the decompressor doesn't
			   know	better */
			rs->expra = 1;
			rs->dlen = 0;
			/* HACK TODO - add link comp here */
			isdn_ppp_ccp_xmit_reset(is, PPP_CCP, CCP_RESETREQ,
						rs->id, NULL, 0);
			/* Start the timer */
			rs->timer.expires = jiffies + 5 * HZ;
			add_timer(&rs->timer);
			rs->ta = 1;
		}
	}
}

/* An Ack was received for this id. This means we stop the timer and clean
   up the state prior to calling the decompressors reset routine. */
static void isdn_ppp_ccp_reset_ack_rcvd(struct ippp_struct *is,
					unsigned char id)
{
	struct ippp_ccp_reset_state *rs = is->reset->rs[id];

	if (rs) {
		if (rs->ta && rs->state == CCPResetSentReq) {
			/* Great, we are correct */
			if (!rs->expra)
				printk(KERN_DEBUG "ippp_ccp: ResetAck received"
				       " for id %d but not expected\n", id);
		} else {
			printk(KERN_INFO "ippp_ccp: ResetAck received out of"
			       "sync for id %d\n", id);
		}
		if (rs->ta) {
			rs->ta = 0;
			del_timer(&rs->timer);
		}
		isdn_ppp_ccp_reset_free_state(is, id);
	} else {
		printk(KERN_INFO "ippp_ccp: ResetAck received for unknown id"
		       " %d\n", id);
	}
	/* Make sure the simple reset stuff uses a new id next time */
	is->reset->lastid++;
}

/*
 * decompress packet
 *
 * if master = 0, we're trying to uncompress an per-link compressed packet,
 * as opposed to an compressed reconstructed-from-MPPP packet.
 * proto is updated to protocol field of uncompressed packet.
 *
 * retval: decompressed packet,
 *         same packet if uncompressed,
 *	   NULL if decompression error
 */

static struct sk_buff *isdn_ppp_decompress(struct sk_buff *skb, struct ippp_struct *is, struct ippp_struct *master,
					   int *proto)
{
	void *stat = NULL;
	struct isdn_ppp_compressor *ipc = NULL;
	struct sk_buff *skb_out;
	int len;
	struct ippp_struct *ri;
	struct isdn_ppp_resetparams rsparm;
	unsigned char rsdata[IPPP_RESET_MAXDATABYTES];

	if (!master) {
		// per-link decompression
		stat = is->link_decomp_stat;
		ipc = is->link_decompressor;
		ri = is;
	} else {
		stat = master->decomp_stat;
		ipc = master->decompressor;
		ri = master;
	}

	if (!ipc) {
		// no decompressor -> we can't decompress.
		printk(KERN_DEBUG "ippp: no decompressor defined!\n");
		return skb;
	}
	BUG_ON(!stat); // if we have a compressor, stat has been set as well

	if ((master && *proto == PPP_COMP) || (!master && *proto == PPP_COMPFRAG)) {
		// compressed packets are compressed by their protocol type

		// Set up reset params for the decompressor
		memset(&rsparm, 0, sizeof(rsparm));
		rsparm.data = rsdata;
		rsparm.maxdlen = IPPP_RESET_MAXDATABYTES;

		skb_out = dev_alloc_skb(is->mru + PPP_HDRLEN);
		if (!skb_out) {
			kfree_skb(skb);
			printk(KERN_ERR "ippp: decomp memory allocation failure\n");
			return NULL;
		}
		len = ipc->decompress(stat, skb, skb_out, &rsparm);
		kfree_skb(skb);
		if (len <= 0) {
			switch (len) {
			case DECOMP_ERROR:
				printk(KERN_INFO "ippp: decomp wants reset %s params\n",
				       rsparm.valid ? "with" : "without");

				isdn_ppp_ccp_reset_trans(ri, &rsparm);
				break;
			case DECOMP_FATALERROR:
				ri->pppcfg |= SC_DC_FERROR;
				/* Kick ipppd to recognize the error */
				isdn_ppp_ccp_kickup(ri);
				break;
			}
			kfree_skb(skb_out);
			return NULL;
		}
		*proto = isdn_ppp_strip_proto(skb_out);
		if (*proto < 0) {
			kfree_skb(skb_out);
			return NULL;
		}
		return skb_out;
	} else {
		// uncompressed packets are fed through the decompressor to
		// update the decompressor state
		ipc->incomp(stat, skb, *proto);
		return skb;
	}
}

/*
 * compress a frame
 *   type=0: normal/bundle compression
 *       =1: link compression
 * returns original skb if we haven't compressed the frame
 * and a new skb pointer if we've done it
 */
static struct sk_buff *isdn_ppp_compress(struct sk_buff *skb_in, int *proto,
					 struct ippp_struct *is, struct ippp_struct *master, int type)
{
	int ret;
	int new_proto;
	struct isdn_ppp_compressor *compressor;
	void *stat;
	struct sk_buff *skb_out;

	/* we do not compress control protocols */
	if (*proto < 0 || *proto > 0x3fff) {
		return skb_in;
	}

	if (type) { /* type=1 => Link compression */
		return skb_in;
	}
	else {
		if (!master) {
			compressor = is->compressor;
			stat = is->comp_stat;
		}
		else {
			compressor = master->compressor;
			stat = master->comp_stat;
		}
		new_proto = PPP_COMP;
	}

	if (!compressor) {
		printk(KERN_ERR "isdn_ppp: No compressor set!\n");
		return skb_in;
	}
	if (!stat) {
		printk(KERN_ERR "isdn_ppp: Compressor not initialized?\n");
		return skb_in;
	}

	/* Allow for at least 150 % expansion (for now) */
	skb_out = alloc_skb(skb_in->len + skb_in->len / 2 + 32 +
			    skb_headroom(skb_in), GFP_ATOMIC);
	if (!skb_out)
		return skb_in;
	skb_reserve(skb_out, skb_headroom(skb_in));

	ret = (compressor->compress)(stat, skb_in, skb_out, *proto);
	if (!ret) {
		dev_kfree_skb(skb_out);
		return skb_in;
	}

	dev_kfree_skb(skb_in);
	*proto = new_proto;
	return skb_out;
}

/*
 * we received a CCP frame ..
 * not a clean solution, but we MUST handle a few cases in the kernel
 */
static void isdn_ppp_receive_ccp(isdn_net_dev *net_dev, isdn_net_local *lp,
				 struct sk_buff *skb, int proto)
{
	struct ippp_struct *is;
	struct ippp_struct *mis;
	int len;
	struct isdn_ppp_resetparams rsparm;
	unsigned char rsdata[IPPP_RESET_MAXDATABYTES];

	printk(KERN_DEBUG "Received CCP frame from peer slot(%d)\n",
	       lp->ppp_slot);
	if (lp->ppp_slot < 0 || lp->ppp_slot >= ISDN_MAX_CHANNELS) {
		printk(KERN_ERR "%s: lp->ppp_slot(%d) out of range\n",
		       __func__, lp->ppp_slot);
		return;
	}
	is = ippp_table[lp->ppp_slot];
	isdn_ppp_frame_log("ccp-rcv", skb->data, skb->len, 32, is->unit, lp->ppp_slot);

	if (lp->master) {
		int slot = ISDN_MASTER_PRIV(lp)->ppp_slot;
		if (slot < 0 || slot >= ISDN_MAX_CHANNELS) {
			printk(KERN_ERR "%s: slot(%d) out of range\n",
			       __func__, slot);
			return;
		}
		mis = ippp_table[slot];
	} else
		mis = is;

	switch (skb->data[0]) {
	case CCP_CONFREQ:
		if (is->debug & 0x10)
			printk(KERN_DEBUG "Disable compression here!\n");
		if (proto == PPP_CCP)
			mis->compflags &= ~SC_COMP_ON;
		else
			is->compflags &= ~SC_LINK_COMP_ON;
		break;
	case CCP_TERMREQ:
	case CCP_TERMACK:
		if (is->debug & 0x10)
			printk(KERN_DEBUG "Disable (de)compression here!\n");
		if (proto == PPP_CCP)
			mis->compflags &= ~(SC_DECOMP_ON | SC_COMP_ON);
		else
			is->compflags &= ~(SC_LINK_DECOMP_ON | SC_LINK_COMP_ON);
		break;
	case CCP_CONFACK:
		/* if we RECEIVE an ackowledge we enable the decompressor */
		if (is->debug & 0x10)
			printk(KERN_DEBUG "Enable decompression here!\n");
		if (proto == PPP_CCP) {
			if (!mis->decompressor)
				break;
			mis->compflags |= SC_DECOMP_ON;
		} else {
			if (!is->decompressor)
				break;
			is->compflags |= SC_LINK_DECOMP_ON;
		}
		break;

	case CCP_RESETACK:
		printk(KERN_DEBUG "Received ResetAck from peer\n");
		len = (skb->data[2] << 8) | skb->data[3];
		len -= 4;

		if (proto == PPP_CCP) {
			/* If a reset Ack was outstanding for this id, then
			   clean up the state engine */
			isdn_ppp_ccp_reset_ack_rcvd(mis, skb->data[1]);
			if (mis->decompressor && mis->decomp_stat)
				mis->decompressor->
					reset(mis->decomp_stat,
					      skb->data[0],
					      skb->data[1],
					      len ? &skb->data[4] : NULL,
					      len, NULL);
			/* TODO: This is not easy to decide here */
			mis->compflags &= ~SC_DECOMP_DISCARD;
		}
		else {
			isdn_ppp_ccp_reset_ack_rcvd(is, skb->data[1]);
			if (is->link_decompressor && is->link_decomp_stat)
				is->link_decompressor->
					reset(is->link_decomp_stat,
					      skb->data[0],
					      skb->data[1],
					      len ? &skb->data[4] : NULL,
					      len, NULL);
			/* TODO: neither here */
			is->compflags &= ~SC_LINK_DECOMP_DISCARD;
		}
		break;

	case CCP_RESETREQ:
		printk(KERN_DEBUG "Received ResetReq from peer\n");
		/* Receiving a ResetReq means we must reset our compressor */
		/* Set up reset params for the reset entry */
		memset(&rsparm, 0, sizeof(rsparm));
		rsparm.data = rsdata;
		rsparm.maxdlen = IPPP_RESET_MAXDATABYTES;
		/* Isolate data length */
		len = (skb->data[2] << 8) | skb->data[3];
		len -= 4;
		if (proto == PPP_CCP) {
			if (mis->compressor && mis->comp_stat)
				mis->compressor->
					reset(mis->comp_stat,
					      skb->data[0],
					      skb->data[1],
					      len ? &skb->data[4] : NULL,
					      len, &rsparm);
		}
		else {
			if (is->link_compressor && is->link_comp_stat)
				is->link_compressor->
					reset(is->link_comp_stat,
					      skb->data[0],
					      skb->data[1],
					      len ? &skb->data[4] : NULL,
					      len, &rsparm);
		}
		/* Ack the Req as specified by rsparm */
		if (rsparm.valid) {
			/* Compressor reset handler decided how to answer */
			if (rsparm.rsend) {
				/* We should send a Frame */
				isdn_ppp_ccp_xmit_reset(is, proto, CCP_RESETACK,
							rsparm.idval ? rsparm.id
							: skb->data[1],
							rsparm.dtval ?
							rsparm.data : NULL,
							rsparm.dtval ?
							rsparm.dlen : 0);
			} else {
				printk(KERN_DEBUG "ResetAck suppressed\n");
			}
		} else {
			/* We answer with a straight reflected Ack */
			isdn_ppp_ccp_xmit_reset(is, proto, CCP_RESETACK,
						skb->data[1],
						len ? &skb->data[4] : NULL,
						len);
		}
		break;
	}
}


/*
 * Daemon sends a CCP frame ...
 */

/* TODO: Clean this up with new Reset semantics */

/* I believe the CCP handling as-is is done wrong. Compressed frames
 * should only be sent/received after CCP reaches UP state, which means
 * both sides have sent CONF_ACK. Currently, we handle both directions
 * independently, which means we may accept compressed frames too early
 * (supposedly not a problem), but may also mean we send compressed frames
 * too early, which may turn out to be a problem.
 * This part of state machine should actually be handled by (i)pppd, but
 * that's too big of a change now. --kai
 */

/* Actually, we might turn this into an advantage: deal with the RFC in
 * the old tradition of beeing generous on what we accept, but beeing
 * strict on what we send. Thus we should just
 * - accept compressed frames as soon as decompression is negotiated
 * - send compressed frames only when decomp *and* comp are negotiated
 * - drop rx compressed frames if we cannot decomp (instead of pushing them
 *   up to ipppd)
 * and I tried to modify this file according to that. --abp
 */

static void isdn_ppp_send_ccp(isdn_net_dev *net_dev, isdn_net_local *lp, struct sk_buff *skb)
{
	struct ippp_struct *mis, *is;
	int proto, slot = lp->ppp_slot;
	unsigned char *data;

	if (!skb || skb->len < 3)
		return;
	if (slot < 0 || slot >= ISDN_MAX_CHANNELS) {
		printk(KERN_ERR "%s: lp->ppp_slot(%d) out of range\n",
		       __func__, slot);
		return;
	}
	is = ippp_table[slot];
	/* Daemon may send with or without address and control field comp */
	data = skb->data;
	if (!(is->pppcfg & SC_COMP_AC) && data[0] == 0xff && data[1] == 0x03) {
		data += 2;
		if (skb->len < 5)
			return;
	}

	proto = ((int)data[0]<<8) + data[1];
	if (proto != PPP_CCP && proto != PPP_CCPFRAG)
		return;

	printk(KERN_DEBUG "Received CCP frame from daemon:\n");
	isdn_ppp_frame_log("ccp-xmit", skb->data, skb->len, 32, is->unit, lp->ppp_slot);

	if (lp->master) {
		slot = ISDN_MASTER_PRIV(lp)->ppp_slot;
		if (slot < 0 || slot >= ISDN_MAX_CHANNELS) {
			printk(KERN_ERR "%s: slot(%d) out of range\n",
			       __func__, slot);
			return;
		}
		mis = ippp_table[slot];
	} else
		mis = is;
	if (mis != is)
		printk(KERN_DEBUG "isdn_ppp: Ouch! Master CCP sends on slave slot!\n");

	switch (data[2]) {
	case CCP_CONFREQ:
		if (is->debug & 0x10)
			printk(KERN_DEBUG "Disable decompression here!\n");
		if (proto == PPP_CCP)
			is->compflags &= ~SC_DECOMP_ON;
		else
			is->compflags &= ~SC_LINK_DECOMP_ON;
		break;
	case CCP_TERMREQ:
	case CCP_TERMACK:
		if (is->debug & 0x10)
			printk(KERN_DEBUG "Disable (de)compression here!\n");
		if (proto == PPP_CCP)
			is->compflags &= ~(SC_DECOMP_ON | SC_COMP_ON);
		else
			is->compflags &= ~(SC_LINK_DECOMP_ON | SC_LINK_COMP_ON);
		break;
	case CCP_CONFACK:
		/* if we SEND an ackowledge we can/must enable the compressor */
		if (is->debug & 0x10)
			printk(KERN_DEBUG "Enable compression here!\n");
		if (proto == PPP_CCP) {
			if (!is->compressor)
				break;
			is->compflags |= SC_COMP_ON;
		} else {
			if (!is->compressor)
				break;
			is->compflags |= SC_LINK_COMP_ON;
		}
		break;
	case CCP_RESETACK:
		/* If we send a ACK we should reset our compressor */
		if (is->debug & 0x10)
			printk(KERN_DEBUG "Reset decompression state here!\n");
		printk(KERN_DEBUG "ResetAck from daemon passed by\n");
		if (proto == PPP_CCP) {
			/* link to master? */
			if (is->compressor && is->comp_stat)
				is->compressor->reset(is->comp_stat, 0, 0,
						      NULL, 0, NULL);
			is->compflags &= ~SC_COMP_DISCARD;
		}
		else {
			if (is->link_compressor && is->link_comp_stat)
				is->link_compressor->reset(is->link_comp_stat,
							   0, 0, NULL, 0, NULL);
			is->compflags &= ~SC_LINK_COMP_DISCARD;
		}
		break;
	case CCP_RESETREQ:
		/* Just let it pass by */
		printk(KERN_DEBUG "ResetReq from daemon passed by\n");
		break;
	}
}

int isdn_ppp_register_compressor(struct isdn_ppp_compressor *ipc)
{
	ipc->next = ipc_head;
	ipc->prev = NULL;
	if (ipc_head) {
		ipc_head->prev = ipc;
	}
	ipc_head = ipc;
	return 0;
}

int isdn_ppp_unregister_compressor(struct isdn_ppp_compressor *ipc)
{
	if (ipc->prev)
		ipc->prev->next = ipc->next;
	else
		ipc_head = ipc->next;
	if (ipc->next)
		ipc->next->prev = ipc->prev;
	ipc->prev = ipc->next = NULL;
	return 0;
}

static int isdn_ppp_set_compressor(struct ippp_struct *is, struct isdn_ppp_comp_data *data)
{
	struct isdn_ppp_compressor *ipc = ipc_head;
	int ret;
	void *stat;
	int num = data->num;

	if (is->debug & 0x10)
		printk(KERN_DEBUG "[%d] Set %s type %d\n", is->unit,
		       (data->flags & IPPP_COMP_FLAG_XMIT) ? "compressor" : "decompressor", num);

	/* If is has no valid reset state vector, we cannot allocate a
	   decompressor. The decompressor would cause reset transactions
	   sooner or later, and they need that vector. */

	if (!(data->flags & IPPP_COMP_FLAG_XMIT) && !is->reset) {
		printk(KERN_ERR "ippp_ccp: no reset data structure - can't"
		       " allow decompression.\n");
		return -ENOMEM;
	}

	while (ipc) {
		if (ipc->num == num) {
			stat = ipc->alloc(data);
			if (stat) {
				ret = ipc->init(stat, data, is->unit, 0);
				if (!ret) {
					printk(KERN_ERR "Can't init (de)compression!\n");
					ipc->free(stat);
					stat = NULL;
					break;
				}
			}
			else {
				printk(KERN_ERR "Can't alloc (de)compression!\n");
				break;
			}

			if (data->flags & IPPP_COMP_FLAG_XMIT) {
				if (data->flags & IPPP_COMP_FLAG_LINK) {
					if (is->link_comp_stat)
						is->link_compressor->free(is->link_comp_stat);
					is->link_comp_stat = stat;
					is->link_compressor = ipc;
				}
				else {
					if (is->comp_stat)
						is->compressor->free(is->comp_stat);
					is->comp_stat = stat;
					is->compressor = ipc;
				}
			}
			else {
				if (data->flags & IPPP_COMP_FLAG_LINK) {
					if (is->link_decomp_stat)
						is->link_decompressor->free(is->link_decomp_stat);
					is->link_decomp_stat = stat;
					is->link_decompressor = ipc;
				}
				else {
					if (is->decomp_stat)
						is->decompressor->free(is->decomp_stat);
					is->decomp_stat = stat;
					is->decompressor = ipc;
				}
			}
			return 0;
		}
		ipc = ipc->next;
	}
	return -EINVAL;
}
