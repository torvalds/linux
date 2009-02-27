/*
 * Copyright(c) 2007 - 2008 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained at www.Open-FCoE.org
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/kthread.h>
#include <linux/crc32.h>
#include <linux/cpu.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/ctype.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsicam.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>
#include <net/rtnetlink.h>

#include <scsi/fc/fc_encaps.h>

#include <scsi/libfc.h>
#include <scsi/fc_frame.h>
#include <scsi/libfcoe.h>
#include <scsi/fc_transport_fcoe.h>

static int debug_fcoe;

#define FCOE_MAX_QUEUE_DEPTH  256
#define FCOE_LOW_QUEUE_DEPTH  32

/* destination address mode */
#define FCOE_GW_ADDR_MODE	    0x00
#define FCOE_FCOUI_ADDR_MODE	    0x01

#define FCOE_WORD_TO_BYTE  4

MODULE_AUTHOR("Open-FCoE.org");
MODULE_DESCRIPTION("FCoE");
MODULE_LICENSE("GPL");

/* fcoe host list */
LIST_HEAD(fcoe_hostlist);
DEFINE_RWLOCK(fcoe_hostlist_lock);
DEFINE_TIMER(fcoe_timer, NULL, 0, 0);
struct fcoe_percpu_s *fcoe_percpu[NR_CPUS];


/* Function Prototyes */
static int fcoe_check_wait_queue(struct fc_lport *);
static void fcoe_recv_flogi(struct fcoe_softc *, struct fc_frame *, u8 *);
#ifdef CONFIG_HOTPLUG_CPU
static int fcoe_cpu_callback(struct notifier_block *, ulong, void *);
#endif /* CONFIG_HOTPLUG_CPU */
static int fcoe_device_notification(struct notifier_block *, ulong, void *);
static void fcoe_dev_setup(void);
static void fcoe_dev_cleanup(void);

/* notification function from net device */
static struct notifier_block fcoe_notifier = {
	.notifier_call = fcoe_device_notification,
};


#ifdef CONFIG_HOTPLUG_CPU
static struct notifier_block fcoe_cpu_notifier = {
	.notifier_call = fcoe_cpu_callback,
};

/**
 * fcoe_create_percpu_data() - creates the associated cpu data
 * @cpu: index for the cpu where fcoe cpu data will be created
 *
 * create percpu stats block, from cpu add notifier
 *
 * Returns: none
 */
static void fcoe_create_percpu_data(int cpu)
{
	struct fc_lport *lp;
	struct fcoe_softc *fc;

	write_lock_bh(&fcoe_hostlist_lock);
	list_for_each_entry(fc, &fcoe_hostlist, list) {
		lp = fc->lp;
		if (lp->dev_stats[cpu] == NULL)
			lp->dev_stats[cpu] =
				kzalloc(sizeof(struct fcoe_dev_stats),
					GFP_KERNEL);
	}
	write_unlock_bh(&fcoe_hostlist_lock);
}

/**
 * fcoe_destroy_percpu_data() - destroys the associated cpu data
 * @cpu: index for the cpu where fcoe cpu data will destroyed
 *
 * destroy percpu stats block called by cpu add/remove notifier
 *
 * Retuns: none
 */
static void fcoe_destroy_percpu_data(int cpu)
{
	struct fc_lport *lp;
	struct fcoe_softc *fc;

	write_lock_bh(&fcoe_hostlist_lock);
	list_for_each_entry(fc, &fcoe_hostlist, list) {
		lp = fc->lp;
		kfree(lp->dev_stats[cpu]);
		lp->dev_stats[cpu] = NULL;
	}
	write_unlock_bh(&fcoe_hostlist_lock);
}

/**
 * fcoe_cpu_callback() - fcoe cpu hotplug event callback
 * @nfb: callback data block
 * @action: event triggering the callback
 * @hcpu: index for the cpu of this event
 *
 * this creates or destroys per cpu data for fcoe
 *
 * Returns NOTIFY_OK always.
 */
static int fcoe_cpu_callback(struct notifier_block *nfb, unsigned long action,
			     void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	switch (action) {
	case CPU_ONLINE:
		fcoe_create_percpu_data(cpu);
		break;
	case CPU_DEAD:
		fcoe_destroy_percpu_data(cpu);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}
#endif /* CONFIG_HOTPLUG_CPU */

/**
 * fcoe_rcv() - this is the fcoe receive function called by NET_RX_SOFTIRQ
 * @skb: the receive skb
 * @dev: associated net device
 * @ptype: context
 * @odldev: last device
 *
 * this function will receive the packet and build fc frame and pass it up
 *
 * Returns: 0 for success
 */
int fcoe_rcv(struct sk_buff *skb, struct net_device *dev,
	     struct packet_type *ptype, struct net_device *olddev)
{
	struct fc_lport *lp;
	struct fcoe_rcv_info *fr;
	struct fcoe_softc *fc;
	struct fcoe_dev_stats *stats;
	struct fc_frame_header *fh;
	unsigned short oxid;
	int cpu_idx;
	struct fcoe_percpu_s *fps;

	fc = container_of(ptype, struct fcoe_softc, fcoe_packet_type);
	lp = fc->lp;
	if (unlikely(lp == NULL)) {
		FC_DBG("cannot find hba structure");
		goto err2;
	}

	if (unlikely(debug_fcoe)) {
		FC_DBG("skb_info: len:%d data_len:%d head:%p data:%p tail:%p "
		       "end:%p sum:%d dev:%s", skb->len, skb->data_len,
		       skb->head, skb->data, skb_tail_pointer(skb),
		       skb_end_pointer(skb), skb->csum,
		       skb->dev ? skb->dev->name : "<NULL>");

	}

	/* check for FCOE packet type */
	if (unlikely(eth_hdr(skb)->h_proto != htons(ETH_P_FCOE))) {
		FC_DBG("wrong FC type frame");
		goto err;
	}

	/*
	 * Check for minimum frame length, and make sure required FCoE
	 * and FC headers are pulled into the linear data area.
	 */
	if (unlikely((skb->len < FCOE_MIN_FRAME) ||
	    !pskb_may_pull(skb, FCOE_HEADER_LEN)))
		goto err;

	skb_set_transport_header(skb, sizeof(struct fcoe_hdr));
	fh = (struct fc_frame_header *) skb_transport_header(skb);

	oxid = ntohs(fh->fh_ox_id);

	fr = fcoe_dev_from_skb(skb);
	fr->fr_dev = lp;
	fr->ptype = ptype;
	cpu_idx = 0;
#ifdef CONFIG_SMP
	/*
	 * The incoming frame exchange id(oxid) is ANDed with num of online
	 * cpu bits to get cpu_idx and then this cpu_idx is used for selecting
	 * a per cpu kernel thread from fcoe_percpu. In case the cpu is
	 * offline or no kernel thread for derived cpu_idx then cpu_idx is
	 * initialize to first online cpu index.
	 */
	cpu_idx = oxid & (num_online_cpus() - 1);
	if (!fcoe_percpu[cpu_idx] || !cpu_online(cpu_idx))
		cpu_idx = first_cpu(cpu_online_map);
#endif
	fps = fcoe_percpu[cpu_idx];

	spin_lock_bh(&fps->fcoe_rx_list.lock);
	__skb_queue_tail(&fps->fcoe_rx_list, skb);
	if (fps->fcoe_rx_list.qlen == 1)
		wake_up_process(fps->thread);

	spin_unlock_bh(&fps->fcoe_rx_list.lock);

	return 0;
err:
#ifdef CONFIG_SMP
	stats = lp->dev_stats[smp_processor_id()];
#else
	stats = lp->dev_stats[0];
#endif
	if (stats)
		stats->ErrorFrames++;

err2:
	kfree_skb(skb);
	return -1;
}
EXPORT_SYMBOL_GPL(fcoe_rcv);

/**
 * fcoe_start_io() - pass to netdev to start xmit for fcoe
 * @skb: the skb to be xmitted
 *
 * Returns: 0 for success
 */
static inline int fcoe_start_io(struct sk_buff *skb)
{
	int rc;

	skb_get(skb);
	rc = dev_queue_xmit(skb);
	if (rc != 0)
		return rc;
	kfree_skb(skb);
	return 0;
}

/**
 * fcoe_get_paged_crc_eof() - in case we need alloc a page for crc_eof
 * @skb: the skb to be xmitted
 * @tlen: total len
 *
 * Returns: 0 for success
 */
static int fcoe_get_paged_crc_eof(struct sk_buff *skb, int tlen)
{
	struct fcoe_percpu_s *fps;
	struct page *page;
	int cpu_idx;

	cpu_idx = get_cpu();
	fps = fcoe_percpu[cpu_idx];
	page = fps->crc_eof_page;
	if (!page) {
		page = alloc_page(GFP_ATOMIC);
		if (!page) {
			put_cpu();
			return -ENOMEM;
		}
		fps->crc_eof_page = page;
		WARN_ON(fps->crc_eof_offset != 0);
	}

	get_page(page);
	skb_fill_page_desc(skb, skb_shinfo(skb)->nr_frags, page,
			   fps->crc_eof_offset, tlen);
	skb->len += tlen;
	skb->data_len += tlen;
	skb->truesize += tlen;
	fps->crc_eof_offset += sizeof(struct fcoe_crc_eof);

	if (fps->crc_eof_offset >= PAGE_SIZE) {
		fps->crc_eof_page = NULL;
		fps->crc_eof_offset = 0;
		put_page(page);
	}
	put_cpu();
	return 0;
}

/**
 * fcoe_fc_crc() - calculates FC CRC in this fcoe skb
 * @fp: the fc_frame containg data to be checksummed
 *
 * This uses crc32() to calculate the crc for fc frame
 * Return   : 32 bit crc
 */
u32 fcoe_fc_crc(struct fc_frame *fp)
{
	struct sk_buff *skb = fp_skb(fp);
	struct skb_frag_struct *frag;
	unsigned char *data;
	unsigned long off, len, clen;
	u32 crc;
	unsigned i;

	crc = crc32(~0, skb->data, skb_headlen(skb));

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		frag = &skb_shinfo(skb)->frags[i];
		off = frag->page_offset;
		len = frag->size;
		while (len > 0) {
			clen = min(len, PAGE_SIZE - (off & ~PAGE_MASK));
			data = kmap_atomic(frag->page + (off >> PAGE_SHIFT),
					   KM_SKB_DATA_SOFTIRQ);
			crc = crc32(crc, data + (off & ~PAGE_MASK), clen);
			kunmap_atomic(data, KM_SKB_DATA_SOFTIRQ);
			off += clen;
			len -= clen;
		}
	}
	return crc;
}
EXPORT_SYMBOL_GPL(fcoe_fc_crc);

/**
 * fcoe_xmit() - FCoE frame transmit function
 * @lp:	the associated local port
 * @fp: the fc_frame to be transmitted
 *
 * Return   : 0 for success
 */
int fcoe_xmit(struct fc_lport *lp, struct fc_frame *fp)
{
	int wlen, rc = 0;
	u32 crc;
	struct ethhdr *eh;
	struct fcoe_crc_eof *cp;
	struct sk_buff *skb;
	struct fcoe_dev_stats *stats;
	struct fc_frame_header *fh;
	unsigned int hlen;		/* header length implies the version */
	unsigned int tlen;		/* trailer length */
	unsigned int elen;		/* eth header, may include vlan */
	int flogi_in_progress = 0;
	struct fcoe_softc *fc;
	u8 sof, eof;
	struct fcoe_hdr *hp;

	WARN_ON((fr_len(fp) % sizeof(u32)) != 0);

	fc = lport_priv(lp);
	/*
	 * if it is a flogi then we need to learn gw-addr
	 * and my own fcid
	 */
	fh = fc_frame_header_get(fp);
	if (unlikely(fh->fh_r_ctl == FC_RCTL_ELS_REQ)) {
		if (fc_frame_payload_op(fp) == ELS_FLOGI) {
			fc->flogi_oxid = ntohs(fh->fh_ox_id);
			fc->address_mode = FCOE_FCOUI_ADDR_MODE;
			fc->flogi_progress = 1;
			flogi_in_progress = 1;
		} else if (fc->flogi_progress && ntoh24(fh->fh_s_id) != 0) {
			/*
			 * Here we must've gotten an SID by accepting an FLOGI
			 * from a point-to-point connection.  Switch to using
			 * the source mac based on the SID.  The destination
			 * MAC in this case would have been set by receving the
			 * FLOGI.
			 */
			fc_fcoe_set_mac(fc->data_src_addr, fh->fh_s_id);
			fc->flogi_progress = 0;
		}
	}

	skb = fp_skb(fp);
	sof = fr_sof(fp);
	eof = fr_eof(fp);

	elen = (fc->real_dev->priv_flags & IFF_802_1Q_VLAN) ?
		sizeof(struct vlan_ethhdr) : sizeof(struct ethhdr);
	hlen = sizeof(struct fcoe_hdr);
	tlen = sizeof(struct fcoe_crc_eof);
	wlen = (skb->len - tlen + sizeof(crc)) / FCOE_WORD_TO_BYTE;

	/* crc offload */
	if (likely(lp->crc_offload)) {
		skb->ip_summed = CHECKSUM_COMPLETE;
		skb->csum_start = skb_headroom(skb);
		skb->csum_offset = skb->len;
		crc = 0;
	} else {
		skb->ip_summed = CHECKSUM_NONE;
		crc = fcoe_fc_crc(fp);
	}

	/* copy fc crc and eof to the skb buff */
	if (skb_is_nonlinear(skb)) {
		skb_frag_t *frag;
		if (fcoe_get_paged_crc_eof(skb, tlen)) {
			kfree_skb(skb);
			return -ENOMEM;
		}
		frag = &skb_shinfo(skb)->frags[skb_shinfo(skb)->nr_frags - 1];
		cp = kmap_atomic(frag->page, KM_SKB_DATA_SOFTIRQ)
			+ frag->page_offset;
	} else {
		cp = (struct fcoe_crc_eof *)skb_put(skb, tlen);
	}

	memset(cp, 0, sizeof(*cp));
	cp->fcoe_eof = eof;
	cp->fcoe_crc32 = cpu_to_le32(~crc);

	if (skb_is_nonlinear(skb)) {
		kunmap_atomic(cp, KM_SKB_DATA_SOFTIRQ);
		cp = NULL;
	}

	/* adjust skb netowrk/transport offsets to match mac/fcoe/fc */
	skb_push(skb, elen + hlen);
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	skb->mac_len = elen;
	skb->protocol = htons(ETH_P_802_3);
	skb->dev = fc->real_dev;

	/* fill up mac and fcoe headers */
	eh = eth_hdr(skb);
	eh->h_proto = htons(ETH_P_FCOE);
	if (fc->address_mode == FCOE_FCOUI_ADDR_MODE)
		fc_fcoe_set_mac(eh->h_dest, fh->fh_d_id);
	else
		/* insert GW address */
		memcpy(eh->h_dest, fc->dest_addr, ETH_ALEN);

	if (unlikely(flogi_in_progress))
		memcpy(eh->h_source, fc->ctl_src_addr, ETH_ALEN);
	else
		memcpy(eh->h_source, fc->data_src_addr, ETH_ALEN);

	hp = (struct fcoe_hdr *)(eh + 1);
	memset(hp, 0, sizeof(*hp));
	if (FC_FCOE_VER)
		FC_FCOE_ENCAPS_VER(hp, FC_FCOE_VER);
	hp->fcoe_sof = sof;

	/* update tx stats: regardless if LLD fails */
	stats = lp->dev_stats[smp_processor_id()];
	if (stats) {
		stats->TxFrames++;
		stats->TxWords += wlen;
	}

	/* send down to lld */
	fr_dev(fp) = lp;
	if (fc->fcoe_pending_queue.qlen)
		rc = fcoe_check_wait_queue(lp);

	if (rc == 0)
		rc = fcoe_start_io(skb);

	if (rc) {
		spin_lock_bh(&fc->fcoe_pending_queue.lock);
		__skb_queue_tail(&fc->fcoe_pending_queue, skb);
		spin_unlock_bh(&fc->fcoe_pending_queue.lock);
		if (fc->fcoe_pending_queue.qlen > FCOE_MAX_QUEUE_DEPTH)
			lp->qfull = 1;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fcoe_xmit);

/**
 * fcoe_percpu_receive_thread() - recv thread per cpu
 * @arg: ptr to the fcoe per cpu struct
 *
 * Return: 0 for success
 */
int fcoe_percpu_receive_thread(void *arg)
{
	struct fcoe_percpu_s *p = arg;
	u32 fr_len;
	struct fc_lport *lp;
	struct fcoe_rcv_info *fr;
	struct fcoe_dev_stats *stats;
	struct fc_frame_header *fh;
	struct sk_buff *skb;
	struct fcoe_crc_eof crc_eof;
	struct fc_frame *fp;
	u8 *mac = NULL;
	struct fcoe_softc *fc;
	struct fcoe_hdr *hp;

	set_user_nice(current, -20);

	while (!kthread_should_stop()) {

		spin_lock_bh(&p->fcoe_rx_list.lock);
		while ((skb = __skb_dequeue(&p->fcoe_rx_list)) == NULL) {
			set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_bh(&p->fcoe_rx_list.lock);
			schedule();
			set_current_state(TASK_RUNNING);
			if (kthread_should_stop())
				return 0;
			spin_lock_bh(&p->fcoe_rx_list.lock);
		}
		spin_unlock_bh(&p->fcoe_rx_list.lock);
		fr = fcoe_dev_from_skb(skb);
		lp = fr->fr_dev;
		if (unlikely(lp == NULL)) {
			FC_DBG("invalid HBA Structure");
			kfree_skb(skb);
			continue;
		}

		stats = lp->dev_stats[smp_processor_id()];

		if (unlikely(debug_fcoe)) {
			FC_DBG("skb_info: len:%d data_len:%d head:%p data:%p "
			       "tail:%p end:%p sum:%d dev:%s",
			       skb->len, skb->data_len,
			       skb->head, skb->data, skb_tail_pointer(skb),
			       skb_end_pointer(skb), skb->csum,
			       skb->dev ? skb->dev->name : "<NULL>");
		}

		/*
		 * Save source MAC address before discarding header.
		 */
		fc = lport_priv(lp);
		if (unlikely(fc->flogi_progress))
			mac = eth_hdr(skb)->h_source;

		if (skb_is_nonlinear(skb))
			skb_linearize(skb);	/* not ideal */

		/*
		 * Frame length checks and setting up the header pointers
		 * was done in fcoe_rcv already.
		 */
		hp = (struct fcoe_hdr *) skb_network_header(skb);
		fh = (struct fc_frame_header *) skb_transport_header(skb);

		if (unlikely(FC_FCOE_DECAPS_VER(hp) != FC_FCOE_VER)) {
			if (stats) {
				if (stats->ErrorFrames < 5)
					FC_DBG("unknown FCoE version %x",
					       FC_FCOE_DECAPS_VER(hp));
				stats->ErrorFrames++;
			}
			kfree_skb(skb);
			continue;
		}

		skb_pull(skb, sizeof(struct fcoe_hdr));
		fr_len = skb->len - sizeof(struct fcoe_crc_eof);

		if (stats) {
			stats->RxFrames++;
			stats->RxWords += fr_len / FCOE_WORD_TO_BYTE;
		}

		fp = (struct fc_frame *)skb;
		fc_frame_init(fp);
		fr_dev(fp) = lp;
		fr_sof(fp) = hp->fcoe_sof;

		/* Copy out the CRC and EOF trailer for access */
		if (skb_copy_bits(skb, fr_len, &crc_eof, sizeof(crc_eof))) {
			kfree_skb(skb);
			continue;
		}
		fr_eof(fp) = crc_eof.fcoe_eof;
		fr_crc(fp) = crc_eof.fcoe_crc32;
		if (pskb_trim(skb, fr_len)) {
			kfree_skb(skb);
			continue;
		}

		/*
		 * We only check CRC if no offload is available and if it is
		 * it's solicited data, in which case, the FCP layer would
		 * check it during the copy.
		 */
		if (lp->crc_offload)
			fr_flags(fp) &= ~FCPHF_CRC_UNCHECKED;
		else
			fr_flags(fp) |= FCPHF_CRC_UNCHECKED;

		fh = fc_frame_header_get(fp);
		if (fh->fh_r_ctl == FC_RCTL_DD_SOL_DATA &&
		    fh->fh_type == FC_TYPE_FCP) {
			fc_exch_recv(lp, lp->emp, fp);
			continue;
		}
		if (fr_flags(fp) & FCPHF_CRC_UNCHECKED) {
			if (le32_to_cpu(fr_crc(fp)) !=
			    ~crc32(~0, skb->data, fr_len)) {
				if (debug_fcoe || stats->InvalidCRCCount < 5)
					printk(KERN_WARNING "fcoe: dropping "
					       "frame with CRC error\n");
				stats->InvalidCRCCount++;
				stats->ErrorFrames++;
				fc_frame_free(fp);
				continue;
			}
			fr_flags(fp) &= ~FCPHF_CRC_UNCHECKED;
		}
		/* non flogi and non data exchanges are handled here */
		if (unlikely(fc->flogi_progress))
			fcoe_recv_flogi(fc, fp, mac);
		fc_exch_recv(lp, lp->emp, fp);
	}
	return 0;
}

/**
 * fcoe_recv_flogi() - flogi receive function
 * @fc: associated fcoe_softc
 * @fp: the recieved frame
 * @sa: the source address of this flogi
 *
 * This is responsible to parse the flogi response and sets the corresponding
 * mac address for the initiator, eitehr OUI based or GW based.
 *
 * Returns: none
 */
static void fcoe_recv_flogi(struct fcoe_softc *fc, struct fc_frame *fp, u8 *sa)
{
	struct fc_frame_header *fh;
	u8 op;

	fh = fc_frame_header_get(fp);
	if (fh->fh_type != FC_TYPE_ELS)
		return;
	op = fc_frame_payload_op(fp);
	if (op == ELS_LS_ACC && fh->fh_r_ctl == FC_RCTL_ELS_REP &&
	    fc->flogi_oxid == ntohs(fh->fh_ox_id)) {
		/*
		 * FLOGI accepted.
		 * If the src mac addr is FC_OUI-based, then we mark the
		 * address_mode flag to use FC_OUI-based Ethernet DA.
		 * Otherwise we use the FCoE gateway addr
		 */
		if (!compare_ether_addr(sa, (u8[6]) FC_FCOE_FLOGI_MAC)) {
			fc->address_mode = FCOE_FCOUI_ADDR_MODE;
		} else {
			memcpy(fc->dest_addr, sa, ETH_ALEN);
			fc->address_mode = FCOE_GW_ADDR_MODE;
		}

		/*
		 * Remove any previously-set unicast MAC filter.
		 * Add secondary FCoE MAC address filter for our OUI.
		 */
		rtnl_lock();
		if (compare_ether_addr(fc->data_src_addr, (u8[6]) { 0 }))
			dev_unicast_delete(fc->real_dev, fc->data_src_addr,
					   ETH_ALEN);
		fc_fcoe_set_mac(fc->data_src_addr, fh->fh_d_id);
		dev_unicast_add(fc->real_dev, fc->data_src_addr, ETH_ALEN);
		rtnl_unlock();

		fc->flogi_progress = 0;
	} else if (op == ELS_FLOGI && fh->fh_r_ctl == FC_RCTL_ELS_REQ && sa) {
		/*
		 * Save source MAC for point-to-point responses.
		 */
		memcpy(fc->dest_addr, sa, ETH_ALEN);
		fc->address_mode = FCOE_GW_ADDR_MODE;
	}
}

/**
 * fcoe_watchdog() - fcoe timer callback
 * @vp:
 *
 * This checks the pending queue length for fcoe and set lport qfull
 * if the FCOE_MAX_QUEUE_DEPTH is reached. This is done for all fc_lport on the
 * fcoe_hostlist.
 *
 * Returns: 0 for success
 */
void fcoe_watchdog(ulong vp)
{
	struct fcoe_softc *fc;

	read_lock(&fcoe_hostlist_lock);
	list_for_each_entry(fc, &fcoe_hostlist, list) {
		if (fc->lp)
			fcoe_check_wait_queue(fc->lp);
	}
	read_unlock(&fcoe_hostlist_lock);

	fcoe_timer.expires = jiffies + (1 * HZ);
	add_timer(&fcoe_timer);
}


/**
 * fcoe_check_wait_queue() - put the skb into fcoe pending xmit queue
 * @lp: the fc_port for this skb
 * @skb: the associated skb to be xmitted
 *
 * This empties the wait_queue, dequeue the head of the wait_queue queue
 * and calls fcoe_start_io() for each packet, if all skb have been
 * transmitted, return qlen or -1 if a error occurs, then restore
 * wait_queue and  try again later.
 *
 * The wait_queue is used when the skb transmit fails. skb will go
 * in the wait_queue which will be emptied by the time function OR
 * by the next skb transmit.
 *
 * Returns: 0 for success
 */
static int fcoe_check_wait_queue(struct fc_lport *lp)
{
	struct fcoe_softc *fc = lport_priv(lp);
	struct sk_buff *skb;
	int rc = -1;

	spin_lock_bh(&fc->fcoe_pending_queue.lock);
	if (fc->fcoe_pending_queue_active)
		goto out;
	fc->fcoe_pending_queue_active = 1;

	while (fc->fcoe_pending_queue.qlen) {
		/* keep qlen > 0 until fcoe_start_io succeeds */
		fc->fcoe_pending_queue.qlen++;
		skb = __skb_dequeue(&fc->fcoe_pending_queue);

		spin_unlock_bh(&fc->fcoe_pending_queue.lock);
		rc = fcoe_start_io(skb);
		spin_lock_bh(&fc->fcoe_pending_queue.lock);

		if (rc) {
			__skb_queue_head(&fc->fcoe_pending_queue, skb);
			/* undo temporary increment above */
			fc->fcoe_pending_queue.qlen--;
			break;
		}
		/* undo temporary increment above */
		fc->fcoe_pending_queue.qlen--;
	}

	if (fc->fcoe_pending_queue.qlen < FCOE_LOW_QUEUE_DEPTH)
		lp->qfull = 0;
	fc->fcoe_pending_queue_active = 0;
	rc = fc->fcoe_pending_queue.qlen;
out:
	spin_unlock_bh(&fc->fcoe_pending_queue.lock);
	return rc;
}

/**
 * fcoe_dev_setup() - setup link change notification interface
 */
static void fcoe_dev_setup()
{
	/*
	 * here setup a interface specific wd time to
	 * monitor the link state
	 */
	register_netdevice_notifier(&fcoe_notifier);
}

/**
 * fcoe_dev_setup() - cleanup link change notification interface
 */
static void fcoe_dev_cleanup(void)
{
	unregister_netdevice_notifier(&fcoe_notifier);
}

/**
 * fcoe_device_notification() - netdev event notification callback
 * @notifier: context of the notification
 * @event: type of event
 * @ptr: fixed array for output parsed ifname
 *
 * This function is called by the ethernet driver in case of link change event
 *
 * Returns: 0 for success
 */
static int fcoe_device_notification(struct notifier_block *notifier,
				    ulong event, void *ptr)
{
	struct fc_lport *lp = NULL;
	struct net_device *real_dev = ptr;
	struct fcoe_softc *fc;
	struct fcoe_dev_stats *stats;
	u32 new_link_up;
	u32 mfs;
	int rc = NOTIFY_OK;

	read_lock(&fcoe_hostlist_lock);
	list_for_each_entry(fc, &fcoe_hostlist, list) {
		if (fc->real_dev == real_dev) {
			lp = fc->lp;
			break;
		}
	}
	read_unlock(&fcoe_hostlist_lock);
	if (lp == NULL) {
		rc = NOTIFY_DONE;
		goto out;
	}

	new_link_up = lp->link_up;
	switch (event) {
	case NETDEV_DOWN:
	case NETDEV_GOING_DOWN:
		new_link_up = 0;
		break;
	case NETDEV_UP:
	case NETDEV_CHANGE:
		new_link_up = !fcoe_link_ok(lp);
		break;
	case NETDEV_CHANGEMTU:
		mfs = fc->real_dev->mtu -
			(sizeof(struct fcoe_hdr) +
			 sizeof(struct fcoe_crc_eof));
		if (mfs >= FC_MIN_MAX_FRAME)
			fc_set_mfs(lp, mfs);
		new_link_up = !fcoe_link_ok(lp);
		break;
	case NETDEV_REGISTER:
		break;
	default:
		FC_DBG("unknown event %ld call", event);
	}
	if (lp->link_up != new_link_up) {
		if (new_link_up)
			fc_linkup(lp);
		else {
			stats = lp->dev_stats[smp_processor_id()];
			if (stats)
				stats->LinkFailureCount++;
			fc_linkdown(lp);
			fcoe_clean_pending_queue(lp);
		}
	}
out:
	return rc;
}

/**
 * fcoe_if_to_netdev() - parse a name buffer to get netdev
 * @ifname: fixed array for output parsed ifname
 * @buffer: incoming buffer to be copied
 *
 * Returns: NULL or ptr to netdeive
 */
static struct net_device *fcoe_if_to_netdev(const char *buffer)
{
	char *cp;
	char ifname[IFNAMSIZ + 2];

	if (buffer) {
		strlcpy(ifname, buffer, IFNAMSIZ);
		cp = ifname + strlen(ifname);
		while (--cp >= ifname && *cp == '\n')
			*cp = '\0';
		return dev_get_by_name(&init_net, ifname);
	}
	return NULL;
}

/**
 * fcoe_netdev_to_module_owner() - finds out the nic drive moddule of the netdev
 * @netdev: the target netdev
 *
 * Returns: ptr to the struct module, NULL for failure
 */
static struct module *
fcoe_netdev_to_module_owner(const struct net_device *netdev)
{
	struct device *dev;

	if (!netdev)
		return NULL;

	dev = netdev->dev.parent;
	if (!dev)
		return NULL;

	if (!dev->driver)
		return NULL;

	return dev->driver->owner;
}

/**
 * fcoe_ethdrv_get() - Hold the Ethernet driver
 * @netdev: the target netdev
 *
 * Holds the Ethernet driver module by try_module_get() for
 * the corresponding netdev.
 *
 * Returns: 0 for succsss
 */
static int fcoe_ethdrv_get(const struct net_device *netdev)
{
	struct module *owner;

	owner = fcoe_netdev_to_module_owner(netdev);
	if (owner) {
		printk(KERN_DEBUG "fcoe:hold driver module %s for %s\n",
		       module_name(owner), netdev->name);
		return  try_module_get(owner);
	}
	return -ENODEV;
}

/**
 * fcoe_ethdrv_put() - Release the Ethernet driver
 * @netdev: the target netdev
 *
 * Releases the Ethernet driver module by module_put for
 * the corresponding netdev.
 *
 * Returns: 0 for succsss
 */
static int fcoe_ethdrv_put(const struct net_device *netdev)
{
	struct module *owner;

	owner = fcoe_netdev_to_module_owner(netdev);
	if (owner) {
		printk(KERN_DEBUG "fcoe:release driver module %s for %s\n",
		       module_name(owner), netdev->name);
		module_put(owner);
		return 0;
	}
	return -ENODEV;
}

/**
 * fcoe_destroy() - handles the destroy from sysfs
 * @buffer: expcted to be a eth if name
 * @kp: associated kernel param
 *
 * Returns: 0 for success
 */
static int fcoe_destroy(const char *buffer, struct kernel_param *kp)
{
	int rc;
	struct net_device *netdev;

	netdev = fcoe_if_to_netdev(buffer);
	if (!netdev) {
		rc = -ENODEV;
		goto out_nodev;
	}
	/* look for existing lport */
	if (!fcoe_hostlist_lookup(netdev)) {
		rc = -ENODEV;
		goto out_putdev;
	}
	/* pass to transport */
	rc = fcoe_transport_release(netdev);
	if (rc) {
		printk(KERN_ERR "fcoe: fcoe_transport_release(%s) failed\n",
		       netdev->name);
		rc = -EIO;
		goto out_putdev;
	}
	fcoe_ethdrv_put(netdev);
	rc = 0;
out_putdev:
	dev_put(netdev);
out_nodev:
	return rc;
}

/**
 * fcoe_create() - Handles the create call from sysfs
 * @buffer: expcted to be a eth if name
 * @kp: associated kernel param
 *
 * Returns: 0 for success
 */
static int fcoe_create(const char *buffer, struct kernel_param *kp)
{
	int rc;
	struct net_device *netdev;

	netdev = fcoe_if_to_netdev(buffer);
	if (!netdev) {
		rc = -ENODEV;
		goto out_nodev;
	}
	/* look for existing lport */
	if (fcoe_hostlist_lookup(netdev)) {
		rc = -EEXIST;
		goto out_putdev;
	}
	fcoe_ethdrv_get(netdev);

	/* pass to transport */
	rc = fcoe_transport_attach(netdev);
	if (rc) {
		printk(KERN_ERR "fcoe: fcoe_transport_attach(%s) failed\n",
		       netdev->name);
		fcoe_ethdrv_put(netdev);
		rc = -EIO;
		goto out_putdev;
	}
	rc = 0;
out_putdev:
	dev_put(netdev);
out_nodev:
	return rc;
}

module_param_call(create, fcoe_create, NULL, NULL, S_IWUSR);
__MODULE_PARM_TYPE(create, "string");
MODULE_PARM_DESC(create, "Create fcoe port using net device passed in.");
module_param_call(destroy, fcoe_destroy, NULL, NULL, S_IWUSR);
__MODULE_PARM_TYPE(destroy, "string");
MODULE_PARM_DESC(destroy, "Destroy fcoe port");

/**
 * fcoe_link_ok() - Check if link is ok for the fc_lport
 * @lp: ptr to the fc_lport
 *
 * Any permanently-disqualifying conditions have been previously checked.
 * This also updates the speed setting, which may change with link for 100/1000.
 *
 * This function should probably be checking for PAUSE support at some point
 * in the future. Currently Per-priority-pause is not determinable using
 * ethtool, so we shouldn't be restrictive until that problem is resolved.
 *
 * Returns: 0 if link is OK for use by FCoE.
 *
 */
int fcoe_link_ok(struct fc_lport *lp)
{
	struct fcoe_softc *fc = lport_priv(lp);
	struct net_device *dev = fc->real_dev;
	struct ethtool_cmd ecmd = { ETHTOOL_GSET };
	int rc = 0;

	if ((dev->flags & IFF_UP) && netif_carrier_ok(dev)) {
		dev = fc->phys_dev;
		if (dev->ethtool_ops->get_settings) {
			dev->ethtool_ops->get_settings(dev, &ecmd);
			lp->link_supported_speeds &=
				~(FC_PORTSPEED_1GBIT | FC_PORTSPEED_10GBIT);
			if (ecmd.supported & (SUPPORTED_1000baseT_Half |
					      SUPPORTED_1000baseT_Full))
				lp->link_supported_speeds |= FC_PORTSPEED_1GBIT;
			if (ecmd.supported & SUPPORTED_10000baseT_Full)
				lp->link_supported_speeds |=
					FC_PORTSPEED_10GBIT;
			if (ecmd.speed == SPEED_1000)
				lp->link_speed = FC_PORTSPEED_1GBIT;
			if (ecmd.speed == SPEED_10000)
				lp->link_speed = FC_PORTSPEED_10GBIT;
		}
	} else
		rc = -1;

	return rc;
}
EXPORT_SYMBOL_GPL(fcoe_link_ok);

/**
 * fcoe_percpu_clean() - Clear the pending skbs for an lport
 * @lp: the fc_lport
 */
void fcoe_percpu_clean(struct fc_lport *lp)
{
	int idx;
	struct fcoe_percpu_s *pp;
	struct fcoe_rcv_info *fr;
	struct sk_buff_head *list;
	struct sk_buff *skb, *next;
	struct sk_buff *head;

	for (idx = 0; idx < NR_CPUS; idx++) {
		if (fcoe_percpu[idx]) {
			pp = fcoe_percpu[idx];
			spin_lock_bh(&pp->fcoe_rx_list.lock);
			list = &pp->fcoe_rx_list;
			head = list->next;
			for (skb = head; skb != (struct sk_buff *)list;
			     skb = next) {
				next = skb->next;
				fr = fcoe_dev_from_skb(skb);
				if (fr->fr_dev == lp) {
					__skb_unlink(skb, list);
					kfree_skb(skb);
				}
			}
			spin_unlock_bh(&pp->fcoe_rx_list.lock);
		}
	}
}
EXPORT_SYMBOL_GPL(fcoe_percpu_clean);

/**
 * fcoe_clean_pending_queue() - Dequeue a skb and free it
 * @lp: the corresponding fc_lport
 *
 * Returns: none
 */
void fcoe_clean_pending_queue(struct fc_lport *lp)
{
	struct fcoe_softc  *fc = lport_priv(lp);
	struct sk_buff *skb;

	spin_lock_bh(&fc->fcoe_pending_queue.lock);
	while ((skb = __skb_dequeue(&fc->fcoe_pending_queue)) != NULL) {
		spin_unlock_bh(&fc->fcoe_pending_queue.lock);
		kfree_skb(skb);
		spin_lock_bh(&fc->fcoe_pending_queue.lock);
	}
	spin_unlock_bh(&fc->fcoe_pending_queue.lock);
}
EXPORT_SYMBOL_GPL(fcoe_clean_pending_queue);

/**
 * libfc_host_alloc() - Allocate a Scsi_Host with room for the fc_lport
 * @sht: ptr to the scsi host templ
 * @priv_size: size of private data after fc_lport
 *
 * Returns: ptr to Scsi_Host
 * TODO: to libfc?
 */
static inline struct Scsi_Host *
libfc_host_alloc(struct scsi_host_template *sht, int priv_size)
{
	return scsi_host_alloc(sht, sizeof(struct fc_lport) + priv_size);
}

/**
 * fcoe_host_alloc() - Allocate a Scsi_Host with room for the fcoe_softc
 * @sht: ptr to the scsi host templ
 * @priv_size: size of private data after fc_lport
 *
 * Returns: ptr to Scsi_Host
 */
struct Scsi_Host *fcoe_host_alloc(struct scsi_host_template *sht, int priv_size)
{
	return libfc_host_alloc(sht, sizeof(struct fcoe_softc) + priv_size);
}
EXPORT_SYMBOL_GPL(fcoe_host_alloc);

/**
 * fcoe_reset() - Resets the fcoe
 * @shost: shost the reset is from
 *
 * Returns: always 0
 */
int fcoe_reset(struct Scsi_Host *shost)
{
	struct fc_lport *lport = shost_priv(shost);
	fc_lport_reset(lport);
	return 0;
}
EXPORT_SYMBOL_GPL(fcoe_reset);

/**
 * fcoe_wwn_from_mac() - Converts 48-bit IEEE MAC address to 64-bit FC WWN.
 * @mac: mac address
 * @scheme: check port
 * @port: port indicator for converting
 *
 * Returns: u64 fc world wide name
 */
u64 fcoe_wwn_from_mac(unsigned char mac[MAX_ADDR_LEN],
		      unsigned int scheme, unsigned int port)
{
	u64 wwn;
	u64 host_mac;

	/* The MAC is in NO, so flip only the low 48 bits */
	host_mac = ((u64) mac[0] << 40) |
		((u64) mac[1] << 32) |
		((u64) mac[2] << 24) |
		((u64) mac[3] << 16) |
		((u64) mac[4] << 8) |
		(u64) mac[5];

	WARN_ON(host_mac >= (1ULL << 48));
	wwn = host_mac | ((u64) scheme << 60);
	switch (scheme) {
	case 1:
		WARN_ON(port != 0);
		break;
	case 2:
		WARN_ON(port >= 0xfff);
		wwn |= (u64) port << 48;
		break;
	default:
		WARN_ON(1);
		break;
	}

	return wwn;
}
EXPORT_SYMBOL_GPL(fcoe_wwn_from_mac);

/**
 * fcoe_hostlist_lookup_softc() - find the corresponding lport by a given device
 * @device: this is currently ptr to net_device
 *
 * Returns: NULL or the located fcoe_softc
 */
static struct fcoe_softc *
fcoe_hostlist_lookup_softc(const struct net_device *dev)
{
	struct fcoe_softc *fc;

	read_lock(&fcoe_hostlist_lock);
	list_for_each_entry(fc, &fcoe_hostlist, list) {
		if (fc->real_dev == dev) {
			read_unlock(&fcoe_hostlist_lock);
			return fc;
		}
	}
	read_unlock(&fcoe_hostlist_lock);
	return NULL;
}

/**
 * fcoe_hostlist_lookup() - Find the corresponding lport by netdev
 * @netdev: ptr to net_device
 *
 * Returns: 0 for success
 */
struct fc_lport *fcoe_hostlist_lookup(const struct net_device *netdev)
{
	struct fcoe_softc *fc;

	fc = fcoe_hostlist_lookup_softc(netdev);

	return (fc) ? fc->lp : NULL;
}
EXPORT_SYMBOL_GPL(fcoe_hostlist_lookup);

/**
 * fcoe_hostlist_add() - Add a lport to lports list
 * @lp: ptr to the fc_lport to badded
 *
 * Returns: 0 for success
 */
int fcoe_hostlist_add(const struct fc_lport *lp)
{
	struct fcoe_softc *fc;

	fc = fcoe_hostlist_lookup_softc(fcoe_netdev(lp));
	if (!fc) {
		fc = lport_priv(lp);
		write_lock_bh(&fcoe_hostlist_lock);
		list_add_tail(&fc->list, &fcoe_hostlist);
		write_unlock_bh(&fcoe_hostlist_lock);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(fcoe_hostlist_add);

/**
 * fcoe_hostlist_remove() - remove a lport from lports list
 * @lp: ptr to the fc_lport to badded
 *
 * Returns: 0 for success
 */
int fcoe_hostlist_remove(const struct fc_lport *lp)
{
	struct fcoe_softc *fc;

	fc = fcoe_hostlist_lookup_softc(fcoe_netdev(lp));
	BUG_ON(!fc);
	write_lock_bh(&fcoe_hostlist_lock);
	list_del(&fc->list);
	write_unlock_bh(&fcoe_hostlist_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(fcoe_hostlist_remove);

/**
 * fcoe_libfc_config() - sets up libfc related properties for lport
 * @lp: ptr to the fc_lport
 * @tt: libfc function template
 *
 * Returns : 0 for success
 */
int fcoe_libfc_config(struct fc_lport *lp, struct libfc_function_template *tt)
{
	/* Set the function pointers set by the LLDD */
	memcpy(&lp->tt, tt, sizeof(*tt));
	if (fc_fcp_init(lp))
		return -ENOMEM;
	fc_exch_init(lp);
	fc_elsct_init(lp);
	fc_lport_init(lp);
	fc_rport_init(lp);
	fc_disc_init(lp);

	return 0;
}
EXPORT_SYMBOL_GPL(fcoe_libfc_config);

/**
 * fcoe_init() - fcoe module loading initialization
 *
 * Initialization routine
 * 1. Will create fc transport software structure
 * 2. initialize the link list of port information structure
 *
 * Returns 0 on success, negative on failure
 */
static int __init fcoe_init(void)
{
	int cpu;
	struct fcoe_percpu_s *p;


	INIT_LIST_HEAD(&fcoe_hostlist);
	rwlock_init(&fcoe_hostlist_lock);

#ifdef CONFIG_HOTPLUG_CPU
	register_cpu_notifier(&fcoe_cpu_notifier);
#endif /* CONFIG_HOTPLUG_CPU */

	/*
	 * initialize per CPU interrupt thread
	 */
	for_each_online_cpu(cpu) {
		p = kzalloc(sizeof(struct fcoe_percpu_s), GFP_KERNEL);
		if (p) {
			p->thread = kthread_create(fcoe_percpu_receive_thread,
						   (void *)p,
						   "fcoethread/%d", cpu);

			/*
			 * if there is no error then bind the thread to the cpu
			 * initialize the semaphore and skb queue head
			 */
			if (likely(!IS_ERR(p->thread))) {
				p->cpu = cpu;
				fcoe_percpu[cpu] = p;
				skb_queue_head_init(&p->fcoe_rx_list);
				kthread_bind(p->thread, cpu);
				wake_up_process(p->thread);
			} else {
				fcoe_percpu[cpu] = NULL;
				kfree(p);
			}
		}
	}

	/*
	 * setup link change notification
	 */
	fcoe_dev_setup();

	setup_timer(&fcoe_timer, fcoe_watchdog, 0);

	mod_timer(&fcoe_timer, jiffies + (10 * HZ));

	/* initiatlize the fcoe transport */
	fcoe_transport_init();

	fcoe_sw_init();

	return 0;
}
module_init(fcoe_init);

/**
 * fcoe_exit() - fcoe module unloading cleanup
 *
 * Returns 0 on success, negative on failure
 */
static void __exit fcoe_exit(void)
{
	u32 idx;
	struct fcoe_softc *fc, *tmp;
	struct fcoe_percpu_s *p;
	struct sk_buff *skb;

	/*
	 * Stop all call back interfaces
	 */
#ifdef CONFIG_HOTPLUG_CPU
	unregister_cpu_notifier(&fcoe_cpu_notifier);
#endif /* CONFIG_HOTPLUG_CPU */
	fcoe_dev_cleanup();

	/*
	 * stop timer
	 */
	del_timer_sync(&fcoe_timer);

	/* releases the associated fcoe transport for each lport */
	list_for_each_entry_safe(fc, tmp, &fcoe_hostlist, list)
		fcoe_transport_release(fc->real_dev);

	for (idx = 0; idx < NR_CPUS; idx++) {
		if (fcoe_percpu[idx]) {
			kthread_stop(fcoe_percpu[idx]->thread);
			p = fcoe_percpu[idx];
			spin_lock_bh(&p->fcoe_rx_list.lock);
			while ((skb = __skb_dequeue(&p->fcoe_rx_list)) != NULL)
				kfree_skb(skb);
			spin_unlock_bh(&p->fcoe_rx_list.lock);
			if (fcoe_percpu[idx]->crc_eof_page)
				put_page(fcoe_percpu[idx]->crc_eof_page);
			kfree(fcoe_percpu[idx]);
		}
	}

	/* remove sw trasnport */
	fcoe_sw_exit();

	/* detach the transport */
	fcoe_transport_exit();
}
module_exit(fcoe_exit);
