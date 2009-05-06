/*
 * Copyright (c) 2008-2009 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2009 Intel Corporation.  All rights reserved.
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

#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/netdevice.h>
#include <linux/errno.h>
#include <linux/bitops.h>
#include <net/rtnetlink.h>

#include <scsi/fc/fc_els.h>
#include <scsi/fc/fc_fs.h>
#include <scsi/fc/fc_fip.h>
#include <scsi/fc/fc_encaps.h>
#include <scsi/fc/fc_fcoe.h>

#include <scsi/libfc.h>
#include <scsi/libfcoe.h>

MODULE_AUTHOR("Open-FCoE.org");
MODULE_DESCRIPTION("FIP discovery protocol support for FCoE HBAs");
MODULE_LICENSE("GPL v2");

#define	FCOE_CTLR_MIN_FKA	500		/* min keep alive (mS) */
#define	FCOE_CTLR_DEF_FKA	FIP_DEF_FKA	/* default keep alive (mS) */

static void fcoe_ctlr_timeout(unsigned long);
static void fcoe_ctlr_link_work(struct work_struct *);
static void fcoe_ctlr_recv_work(struct work_struct *);

static u8 fcoe_all_fcfs[ETH_ALEN] = FIP_ALL_FCF_MACS;

static u32 fcoe_ctlr_debug;	/* 1 for basic, 2 for noisy debug */

#define FIP_DBG_LVL(level, fmt, args...) 				\
		do {							\
			if (fcoe_ctlr_debug >= (level))			\
				FC_DBG(fmt, ##args);			\
		} while (0)

#define FIP_DBG(fmt, args...)	FIP_DBG_LVL(1, fmt, ##args)

/*
 * Return non-zero if FCF fcoe_size has been validated.
 */
static inline int fcoe_ctlr_mtu_valid(const struct fcoe_fcf *fcf)
{
	return (fcf->flags & FIP_FL_SOL) != 0;
}

/*
 * Return non-zero if the FCF is usable.
 */
static inline int fcoe_ctlr_fcf_usable(struct fcoe_fcf *fcf)
{
	u16 flags = FIP_FL_SOL | FIP_FL_AVAIL;

	return (fcf->flags & flags) == flags;
}

/**
 * fcoe_ctlr_init() - Initialize the FCoE Controller instance.
 * @fip:	FCoE controller.
 */
void fcoe_ctlr_init(struct fcoe_ctlr *fip)
{
	fip->state = FIP_ST_LINK_WAIT;
	INIT_LIST_HEAD(&fip->fcfs);
	spin_lock_init(&fip->lock);
	fip->flogi_oxid = FC_XID_UNKNOWN;
	setup_timer(&fip->timer, fcoe_ctlr_timeout, (unsigned long)fip);
	INIT_WORK(&fip->link_work, fcoe_ctlr_link_work);
	INIT_WORK(&fip->recv_work, fcoe_ctlr_recv_work);
	skb_queue_head_init(&fip->fip_recv_list);
}
EXPORT_SYMBOL(fcoe_ctlr_init);

/**
 * fcoe_ctlr_reset_fcfs() - Reset and free all FCFs for a controller.
 * @fip:	FCoE controller.
 *
 * Called with &fcoe_ctlr lock held.
 */
static void fcoe_ctlr_reset_fcfs(struct fcoe_ctlr *fip)
{
	struct fcoe_fcf *fcf;
	struct fcoe_fcf *next;

	fip->sel_fcf = NULL;
	list_for_each_entry_safe(fcf, next, &fip->fcfs, list) {
		list_del(&fcf->list);
		kfree(fcf);
	}
	fip->fcf_count = 0;
	fip->sel_time = 0;
}

/**
 * fcoe_ctlr_destroy() - Disable and tear-down the FCoE controller.
 * @fip:	FCoE controller.
 *
 * This is called by FCoE drivers before freeing the &fcoe_ctlr.
 *
 * The receive handler will have been deleted before this to guarantee
 * that no more recv_work will be scheduled.
 *
 * The timer routine will simply return once we set FIP_ST_DISABLED.
 * This guarantees that no further timeouts or work will be scheduled.
 */
void fcoe_ctlr_destroy(struct fcoe_ctlr *fip)
{
	flush_work(&fip->recv_work);
	spin_lock_bh(&fip->lock);
	fip->state = FIP_ST_DISABLED;
	fcoe_ctlr_reset_fcfs(fip);
	spin_unlock_bh(&fip->lock);
	del_timer_sync(&fip->timer);
	flush_work(&fip->link_work);
}
EXPORT_SYMBOL(fcoe_ctlr_destroy);

/**
 * fcoe_ctlr_fcoe_size() - Return the maximum FCoE size required for VN_Port.
 * @fip:	FCoE controller.
 *
 * Returns the maximum packet size including the FCoE header and trailer,
 * but not including any Ethernet or VLAN headers.
 */
static inline u32 fcoe_ctlr_fcoe_size(struct fcoe_ctlr *fip)
{
	/*
	 * Determine the max FCoE frame size allowed, including
	 * FCoE header and trailer.
	 * Note:  lp->mfs is currently the payload size, not the frame size.
	 */
	return fip->lp->mfs + sizeof(struct fc_frame_header) +
		sizeof(struct fcoe_hdr) + sizeof(struct fcoe_crc_eof);
}

/**
 * fcoe_ctlr_solicit() - Send a solicitation.
 * @fip:	FCoE controller.
 * @fcf:	Destination FCF.  If NULL, a multicast solicitation is sent.
 */
static void fcoe_ctlr_solicit(struct fcoe_ctlr *fip, struct fcoe_fcf *fcf)
{
	struct sk_buff *skb;
	struct fip_sol {
		struct ethhdr eth;
		struct fip_header fip;
		struct {
			struct fip_mac_desc mac;
			struct fip_wwn_desc wwnn;
			struct fip_size_desc size;
		} __attribute__((packed)) desc;
	}  __attribute__((packed)) *sol;
	u32 fcoe_size;

	skb = dev_alloc_skb(sizeof(*sol));
	if (!skb)
		return;

	sol = (struct fip_sol *)skb->data;

	memset(sol, 0, sizeof(*sol));
	memcpy(sol->eth.h_dest, fcf ? fcf->fcf_mac : fcoe_all_fcfs, ETH_ALEN);
	memcpy(sol->eth.h_source, fip->ctl_src_addr, ETH_ALEN);
	sol->eth.h_proto = htons(ETH_P_FIP);

	sol->fip.fip_ver = FIP_VER_ENCAPS(FIP_VER);
	sol->fip.fip_op = htons(FIP_OP_DISC);
	sol->fip.fip_subcode = FIP_SC_SOL;
	sol->fip.fip_dl_len = htons(sizeof(sol->desc) / FIP_BPW);
	sol->fip.fip_flags = htons(FIP_FL_FPMA);

	sol->desc.mac.fd_desc.fip_dtype = FIP_DT_MAC;
	sol->desc.mac.fd_desc.fip_dlen = sizeof(sol->desc.mac) / FIP_BPW;
	memcpy(sol->desc.mac.fd_mac, fip->ctl_src_addr, ETH_ALEN);

	sol->desc.wwnn.fd_desc.fip_dtype = FIP_DT_NAME;
	sol->desc.wwnn.fd_desc.fip_dlen = sizeof(sol->desc.wwnn) / FIP_BPW;
	put_unaligned_be64(fip->lp->wwnn, &sol->desc.wwnn.fd_wwn);

	fcoe_size = fcoe_ctlr_fcoe_size(fip);
	sol->desc.size.fd_desc.fip_dtype = FIP_DT_FCOE_SIZE;
	sol->desc.size.fd_desc.fip_dlen = sizeof(sol->desc.size) / FIP_BPW;
	sol->desc.size.fd_size = htons(fcoe_size);

	skb_put(skb, sizeof(*sol));
	skb->protocol = htons(ETH_P_FIP);
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	fip->send(fip, skb);

	if (!fcf)
		fip->sol_time = jiffies;
}

/**
 * fcoe_ctlr_link_up() - Start FCoE controller.
 * @fip:	FCoE controller.
 *
 * Called from the LLD when the network link is ready.
 */
void fcoe_ctlr_link_up(struct fcoe_ctlr *fip)
{
	spin_lock_bh(&fip->lock);
	if (fip->state == FIP_ST_NON_FIP || fip->state == FIP_ST_AUTO) {
		fip->last_link = 1;
		fip->link = 1;
		spin_unlock_bh(&fip->lock);
		fc_linkup(fip->lp);
	} else if (fip->state == FIP_ST_LINK_WAIT) {
		fip->state = FIP_ST_AUTO;
		fip->last_link = 1;
		fip->link = 1;
		spin_unlock_bh(&fip->lock);
		FIP_DBG("%s", "setting AUTO mode.\n");
		fc_linkup(fip->lp);
		fcoe_ctlr_solicit(fip, NULL);
	} else
		spin_unlock_bh(&fip->lock);
}
EXPORT_SYMBOL(fcoe_ctlr_link_up);

/**
 * fcoe_ctlr_reset() - Reset FIP.
 * @fip:	FCoE controller.
 * @new_state:	FIP state to be entered.
 *
 * Returns non-zero if the link was up and now isn't.
 */
static int fcoe_ctlr_reset(struct fcoe_ctlr *fip, enum fip_state new_state)
{
	struct fc_lport *lp = fip->lp;
	int link_dropped;

	spin_lock_bh(&fip->lock);
	fcoe_ctlr_reset_fcfs(fip);
	del_timer(&fip->timer);
	fip->state = new_state;
	fip->ctlr_ka_time = 0;
	fip->port_ka_time = 0;
	fip->sol_time = 0;
	fip->flogi_oxid = FC_XID_UNKNOWN;
	fip->map_dest = 0;
	fip->last_link = 0;
	link_dropped = fip->link;
	fip->link = 0;
	spin_unlock_bh(&fip->lock);

	if (link_dropped)
		fc_linkdown(lp);

	if (new_state == FIP_ST_ENABLED) {
		fcoe_ctlr_solicit(fip, NULL);
		fc_linkup(lp);
		link_dropped = 0;
	}
	return link_dropped;
}

/**
 * fcoe_ctlr_link_down() - Stop FCoE controller.
 * @fip:	FCoE controller.
 *
 * Returns non-zero if the link was up and now isn't.
 *
 * Called from the LLD when the network link is not ready.
 * There may be multiple calls while the link is down.
 */
int fcoe_ctlr_link_down(struct fcoe_ctlr *fip)
{
	return fcoe_ctlr_reset(fip, FIP_ST_LINK_WAIT);
}
EXPORT_SYMBOL(fcoe_ctlr_link_down);

/**
 * fcoe_ctlr_send_keep_alive() - Send a keep-alive to the selected FCF.
 * @fip:	FCoE controller.
 * @ports:	0 for controller keep-alive, 1 for port keep-alive.
 * @sa:		source MAC address.
 *
 * A controller keep-alive is sent every fka_period (typically 8 seconds).
 * The source MAC is the native MAC address.
 *
 * A port keep-alive is sent every 90 seconds while logged in.
 * The source MAC is the assigned mapped source address.
 * The destination is the FCF's F-port.
 */
static void fcoe_ctlr_send_keep_alive(struct fcoe_ctlr *fip, int ports, u8 *sa)
{
	struct sk_buff *skb;
	struct fip_kal {
		struct ethhdr eth;
		struct fip_header fip;
		struct fip_mac_desc mac;
	} __attribute__((packed)) *kal;
	struct fip_vn_desc *vn;
	u32 len;
	struct fc_lport *lp;
	struct fcoe_fcf *fcf;

	fcf = fip->sel_fcf;
	lp = fip->lp;
	if (!fcf || !fc_host_port_id(lp->host))
		return;

	len = fcoe_ctlr_fcoe_size(fip) + sizeof(struct ethhdr);
	BUG_ON(len < sizeof(*kal) + sizeof(*vn));
	skb = dev_alloc_skb(len);
	if (!skb)
		return;

	kal = (struct fip_kal *)skb->data;
	memset(kal, 0, len);
	memcpy(kal->eth.h_dest, fcf->fcf_mac, ETH_ALEN);
	memcpy(kal->eth.h_source, sa, ETH_ALEN);
	kal->eth.h_proto = htons(ETH_P_FIP);

	kal->fip.fip_ver = FIP_VER_ENCAPS(FIP_VER);
	kal->fip.fip_op = htons(FIP_OP_CTRL);
	kal->fip.fip_subcode = FIP_SC_KEEP_ALIVE;
	kal->fip.fip_dl_len = htons((sizeof(kal->mac) +
				    ports * sizeof(*vn)) / FIP_BPW);
	kal->fip.fip_flags = htons(FIP_FL_FPMA);

	kal->mac.fd_desc.fip_dtype = FIP_DT_MAC;
	kal->mac.fd_desc.fip_dlen = sizeof(kal->mac) / FIP_BPW;
	memcpy(kal->mac.fd_mac, fip->ctl_src_addr, ETH_ALEN);

	if (ports) {
		vn = (struct fip_vn_desc *)(kal + 1);
		vn->fd_desc.fip_dtype = FIP_DT_VN_ID;
		vn->fd_desc.fip_dlen = sizeof(*vn) / FIP_BPW;
		memcpy(vn->fd_mac, fip->data_src_addr, ETH_ALEN);
		hton24(vn->fd_fc_id, fc_host_port_id(lp->host));
		put_unaligned_be64(lp->wwpn, &vn->fd_wwpn);
	}

	skb_put(skb, len);
	skb->protocol = htons(ETH_P_FIP);
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	fip->send(fip, skb);
}

/**
 * fcoe_ctlr_encaps() - Encapsulate an ELS frame for FIP, without sending it.
 * @fip:	FCoE controller.
 * @dtype:	FIP descriptor type for the frame.
 * @skb:	FCoE ELS frame including FC header but no FCoE headers.
 *
 * Returns non-zero error code on failure.
 *
 * The caller must check that the length is a multiple of 4.
 *
 * The @skb must have enough headroom (28 bytes) and tailroom (8 bytes).
 * Headroom includes the FIP encapsulation description, FIP header, and
 * Ethernet header.  The tailroom is for the FIP MAC descriptor.
 */
static int fcoe_ctlr_encaps(struct fcoe_ctlr *fip,
			    u8 dtype, struct sk_buff *skb)
{
	struct fip_encaps_head {
		struct ethhdr eth;
		struct fip_header fip;
		struct fip_encaps encaps;
	} __attribute__((packed)) *cap;
	struct fip_mac_desc *mac;
	struct fcoe_fcf *fcf;
	size_t dlen;

	fcf = fip->sel_fcf;
	if (!fcf)
		return -ENODEV;
	dlen = sizeof(struct fip_encaps) + skb->len;	/* len before push */
	cap = (struct fip_encaps_head *)skb_push(skb, sizeof(*cap));

	memset(cap, 0, sizeof(*cap));
	memcpy(cap->eth.h_dest, fcf->fcf_mac, ETH_ALEN);
	memcpy(cap->eth.h_source, fip->ctl_src_addr, ETH_ALEN);
	cap->eth.h_proto = htons(ETH_P_FIP);

	cap->fip.fip_ver = FIP_VER_ENCAPS(FIP_VER);
	cap->fip.fip_op = htons(FIP_OP_LS);
	cap->fip.fip_subcode = FIP_SC_REQ;
	cap->fip.fip_dl_len = htons((dlen + sizeof(*mac)) / FIP_BPW);
	cap->fip.fip_flags = htons(FIP_FL_FPMA);

	cap->encaps.fd_desc.fip_dtype = dtype;
	cap->encaps.fd_desc.fip_dlen = dlen / FIP_BPW;

	mac = (struct fip_mac_desc *)skb_put(skb, sizeof(*mac));
	memset(mac, 0, sizeof(mac));
	mac->fd_desc.fip_dtype = FIP_DT_MAC;
	mac->fd_desc.fip_dlen = sizeof(*mac) / FIP_BPW;
	if (dtype != ELS_FLOGI)
		memcpy(mac->fd_mac, fip->data_src_addr, ETH_ALEN);

	skb->protocol = htons(ETH_P_FIP);
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	return 0;
}

/**
 * fcoe_ctlr_els_send() - Send an ELS frame encapsulated by FIP if appropriate.
 * @fip:	FCoE controller.
 * @skb:	FCoE ELS frame including FC header but no FCoE headers.
 *
 * Returns a non-zero error code if the frame should not be sent.
 * Returns zero if the caller should send the frame with FCoE encapsulation.
 *
 * The caller must check that the length is a multiple of 4.
 * The SKB must have enough headroom (28 bytes) and tailroom (8 bytes).
 */
int fcoe_ctlr_els_send(struct fcoe_ctlr *fip, struct sk_buff *skb)
{
	struct fc_frame_header *fh;
	u16 old_xid;
	u8 op;

	fh = (struct fc_frame_header *)skb->data;
	op = *(u8 *)(fh + 1);

	if (op == ELS_FLOGI) {
		old_xid = fip->flogi_oxid;
		fip->flogi_oxid = ntohs(fh->fh_ox_id);
		if (fip->state == FIP_ST_AUTO) {
			if (old_xid == FC_XID_UNKNOWN)
				fip->flogi_count = 0;
			fip->flogi_count++;
			if (fip->flogi_count < 3)
				goto drop;
			fip->map_dest = 1;
			return 0;
		}
		if (fip->state == FIP_ST_NON_FIP)
			fip->map_dest = 1;
	}

	if (fip->state == FIP_ST_NON_FIP)
		return 0;

	switch (op) {
	case ELS_FLOGI:
		op = FIP_DT_FLOGI;
		break;
	case ELS_FDISC:
		if (ntoh24(fh->fh_s_id))
			return 0;
		op = FIP_DT_FDISC;
		break;
	case ELS_LOGO:
		if (fip->state != FIP_ST_ENABLED)
			return 0;
		if (ntoh24(fh->fh_d_id) != FC_FID_FLOGI)
			return 0;
		op = FIP_DT_LOGO;
		break;
	case ELS_LS_ACC:
		if (fip->flogi_oxid == FC_XID_UNKNOWN)
			return 0;
		if (!ntoh24(fh->fh_s_id))
			return 0;
		if (fip->state == FIP_ST_AUTO)
			return 0;
		/*
		 * Here we must've gotten an SID by accepting an FLOGI
		 * from a point-to-point connection.  Switch to using
		 * the source mac based on the SID.  The destination
		 * MAC in this case would have been set by receving the
		 * FLOGI.
		 */
		fip->flogi_oxid = FC_XID_UNKNOWN;
		fc_fcoe_set_mac(fip->data_src_addr, fh->fh_s_id);
		return 0;
	default:
		if (fip->state != FIP_ST_ENABLED)
			goto drop;
		return 0;
	}
	if (fcoe_ctlr_encaps(fip, op, skb))
		goto drop;
	fip->send(fip, skb);
	return -EINPROGRESS;
drop:
	kfree_skb(skb);
	return -EINVAL;
}
EXPORT_SYMBOL(fcoe_ctlr_els_send);

/*
 * fcoe_ctlr_age_fcfs() - Reset and free all old FCFs for a controller.
 * @fip:	FCoE controller.
 *
 * Called with lock held.
 *
 * An FCF is considered old if we have missed three advertisements.
 * That is, there have been no valid advertisement from it for three
 * times its keep-alive period including fuzz.
 *
 * In addition, determine the time when an FCF selection can occur.
 */
static void fcoe_ctlr_age_fcfs(struct fcoe_ctlr *fip)
{
	struct fcoe_fcf *fcf;
	struct fcoe_fcf *next;
	unsigned long sel_time = 0;

	list_for_each_entry_safe(fcf, next, &fip->fcfs, list) {
		if (time_after(jiffies, fcf->time + fcf->fka_period * 3 +
			       msecs_to_jiffies(FIP_FCF_FUZZ * 3))) {
			if (fip->sel_fcf == fcf)
				fip->sel_fcf = NULL;
			list_del(&fcf->list);
			WARN_ON(!fip->fcf_count);
			fip->fcf_count--;
			kfree(fcf);
		} else if (fcoe_ctlr_mtu_valid(fcf) &&
			   (!sel_time || time_before(sel_time, fcf->time))) {
			sel_time = fcf->time;
		}
	}
	if (sel_time) {
		sel_time += msecs_to_jiffies(FCOE_CTLR_START_DELAY);
		fip->sel_time = sel_time;
		if (time_before(sel_time, fip->timer.expires))
			mod_timer(&fip->timer, sel_time);
	} else {
		fip->sel_time = 0;
	}
}

/**
 * fcoe_ctlr_parse_adv() - Decode a FIP advertisement into a new FCF entry.
 * @skb:	received FIP advertisement frame
 * @fcf:	resulting FCF entry.
 *
 * Returns zero on a valid parsed advertisement,
 * otherwise returns non zero value.
 */
static int fcoe_ctlr_parse_adv(struct sk_buff *skb, struct fcoe_fcf *fcf)
{
	struct fip_header *fiph;
	struct fip_desc *desc = NULL;
	struct fip_wwn_desc *wwn;
	struct fip_fab_desc *fab;
	struct fip_fka_desc *fka;
	unsigned long t;
	size_t rlen;
	size_t dlen;

	memset(fcf, 0, sizeof(*fcf));
	fcf->fka_period = msecs_to_jiffies(FCOE_CTLR_DEF_FKA);

	fiph = (struct fip_header *)skb->data;
	fcf->flags = ntohs(fiph->fip_flags);

	rlen = ntohs(fiph->fip_dl_len) * 4;
	if (rlen + sizeof(*fiph) > skb->len)
		return -EINVAL;

	desc = (struct fip_desc *)(fiph + 1);
	while (rlen > 0) {
		dlen = desc->fip_dlen * FIP_BPW;
		if (dlen < sizeof(*desc) || dlen > rlen)
			return -EINVAL;
		switch (desc->fip_dtype) {
		case FIP_DT_PRI:
			if (dlen != sizeof(struct fip_pri_desc))
				goto len_err;
			fcf->pri = ((struct fip_pri_desc *)desc)->fd_pri;
			break;
		case FIP_DT_MAC:
			if (dlen != sizeof(struct fip_mac_desc))
				goto len_err;
			memcpy(fcf->fcf_mac,
			       ((struct fip_mac_desc *)desc)->fd_mac,
			       ETH_ALEN);
			if (!is_valid_ether_addr(fcf->fcf_mac)) {
				FIP_DBG("invalid MAC addr in FIP adv\n");
				return -EINVAL;
			}
			break;
		case FIP_DT_NAME:
			if (dlen != sizeof(struct fip_wwn_desc))
				goto len_err;
			wwn = (struct fip_wwn_desc *)desc;
			fcf->switch_name = get_unaligned_be64(&wwn->fd_wwn);
			break;
		case FIP_DT_FAB:
			if (dlen != sizeof(struct fip_fab_desc))
				goto len_err;
			fab = (struct fip_fab_desc *)desc;
			fcf->fabric_name = get_unaligned_be64(&fab->fd_wwn);
			fcf->vfid = ntohs(fab->fd_vfid);
			fcf->fc_map = ntoh24(fab->fd_map);
			break;
		case FIP_DT_FKA:
			if (dlen != sizeof(struct fip_fka_desc))
				goto len_err;
			fka = (struct fip_fka_desc *)desc;
			t = ntohl(fka->fd_fka_period);
			if (t >= FCOE_CTLR_MIN_FKA)
				fcf->fka_period = msecs_to_jiffies(t);
			break;
		case FIP_DT_MAP_OUI:
		case FIP_DT_FCOE_SIZE:
		case FIP_DT_FLOGI:
		case FIP_DT_FDISC:
		case FIP_DT_LOGO:
		case FIP_DT_ELP:
		default:
			FIP_DBG("unexpected descriptor type %x in FIP adv\n",
				desc->fip_dtype);
			/* standard says ignore unknown descriptors >= 128 */
			if (desc->fip_dtype < FIP_DT_VENDOR_BASE)
				return -EINVAL;
			continue;
		}
		desc = (struct fip_desc *)((char *)desc + dlen);
		rlen -= dlen;
	}
	if (!fcf->fc_map || (fcf->fc_map & 0x10000))
		return -EINVAL;
	if (!fcf->switch_name || !fcf->fabric_name)
		return -EINVAL;
	return 0;

len_err:
	FIP_DBG("FIP length error in descriptor type %x len %zu\n",
		desc->fip_dtype, dlen);
	return -EINVAL;
}

/**
 * fcoe_ctlr_recv_adv() - Handle an incoming advertisement.
 * @fip:	FCoE controller.
 * @skb:	Received FIP packet.
 */
static void fcoe_ctlr_recv_adv(struct fcoe_ctlr *fip, struct sk_buff *skb)
{
	struct fcoe_fcf *fcf;
	struct fcoe_fcf new;
	struct fcoe_fcf *found;
	unsigned long sol_tov = msecs_to_jiffies(FCOE_CTRL_SOL_TOV);
	int first = 0;
	int mtu_valid;

	if (fcoe_ctlr_parse_adv(skb, &new))
		return;

	spin_lock_bh(&fip->lock);
	first = list_empty(&fip->fcfs);
	found = NULL;
	list_for_each_entry(fcf, &fip->fcfs, list) {
		if (fcf->switch_name == new.switch_name &&
		    fcf->fabric_name == new.fabric_name &&
		    fcf->fc_map == new.fc_map &&
		    compare_ether_addr(fcf->fcf_mac, new.fcf_mac) == 0) {
			found = fcf;
			break;
		}
	}
	if (!found) {
		if (fip->fcf_count >= FCOE_CTLR_FCF_LIMIT)
			goto out;

		fcf = kmalloc(sizeof(*fcf), GFP_ATOMIC);
		if (!fcf)
			goto out;

		fip->fcf_count++;
		memcpy(fcf, &new, sizeof(new));
		list_add(&fcf->list, &fip->fcfs);
	} else {
		/*
		 * Flags in advertisements are ignored once the FCF is
		 * selected.  Flags in unsolicited advertisements are
		 * ignored after a usable solicited advertisement
		 * has been received.
		 */
		if (fcf == fip->sel_fcf) {
			fip->ctlr_ka_time -= fcf->fka_period;
			fip->ctlr_ka_time += new.fka_period;
			if (time_before(fip->ctlr_ka_time, fip->timer.expires))
				mod_timer(&fip->timer, fip->ctlr_ka_time);
		} else if (!fcoe_ctlr_fcf_usable(fcf))
			fcf->flags = new.flags;
		fcf->fka_period = new.fka_period;
		memcpy(fcf->fcf_mac, new.fcf_mac, ETH_ALEN);
	}
	mtu_valid = fcoe_ctlr_mtu_valid(fcf);
	fcf->time = jiffies;
	FIP_DBG_LVL(found ? 2 : 1, "%s FCF for fab %llx map %x val %d\n",
		    found ? "old" : "new",
		    fcf->fabric_name, fcf->fc_map, mtu_valid);

	/*
	 * If this advertisement is not solicited and our max receive size
	 * hasn't been verified, send a solicited advertisement.
	 */
	if (!mtu_valid)
		fcoe_ctlr_solicit(fip, fcf);

	/*
	 * If its been a while since we did a solicit, and this is
	 * the first advertisement we've received, do a multicast
	 * solicitation to gather as many advertisements as we can
	 * before selection occurs.
	 */
	if (first && time_after(jiffies, fip->sol_time + sol_tov))
		fcoe_ctlr_solicit(fip, NULL);

	/*
	 * If this is the first validated FCF, note the time and
	 * set a timer to trigger selection.
	 */
	if (mtu_valid && !fip->sel_time && fcoe_ctlr_fcf_usable(fcf)) {
		fip->sel_time = jiffies +
				msecs_to_jiffies(FCOE_CTLR_START_DELAY);
		if (!timer_pending(&fip->timer) ||
		    time_before(fip->sel_time, fip->timer.expires))
			mod_timer(&fip->timer, fip->sel_time);
	}
out:
	spin_unlock_bh(&fip->lock);
}

/**
 * fcoe_ctlr_recv_els() - Handle an incoming FIP-encapsulated ELS frame.
 * @fip:	FCoE controller.
 * @skb:	Received FIP packet.
 */
static void fcoe_ctlr_recv_els(struct fcoe_ctlr *fip, struct sk_buff *skb)
{
	struct fc_lport *lp = fip->lp;
	struct fip_header *fiph;
	struct fc_frame *fp;
	struct fc_frame_header *fh = NULL;
	struct fip_desc *desc;
	struct fip_encaps *els;
	struct fcoe_dev_stats *stats;
	enum fip_desc_type els_dtype = 0;
	u8 els_op;
	u8 sub;
	u8 granted_mac[ETH_ALEN] = { 0 };
	size_t els_len = 0;
	size_t rlen;
	size_t dlen;

	fiph = (struct fip_header *)skb->data;
	sub = fiph->fip_subcode;
	if (sub != FIP_SC_REQ && sub != FIP_SC_REP)
		goto drop;

	rlen = ntohs(fiph->fip_dl_len) * 4;
	if (rlen + sizeof(*fiph) > skb->len)
		goto drop;

	desc = (struct fip_desc *)(fiph + 1);
	while (rlen > 0) {
		dlen = desc->fip_dlen * FIP_BPW;
		if (dlen < sizeof(*desc) || dlen > rlen)
			goto drop;
		switch (desc->fip_dtype) {
		case FIP_DT_MAC:
			if (dlen != sizeof(struct fip_mac_desc))
				goto len_err;
			memcpy(granted_mac,
			       ((struct fip_mac_desc *)desc)->fd_mac,
			       ETH_ALEN);
			if (!is_valid_ether_addr(granted_mac)) {
				FIP_DBG("invalid MAC addrs in FIP ELS\n");
				goto drop;
			}
			break;
		case FIP_DT_FLOGI:
		case FIP_DT_FDISC:
		case FIP_DT_LOGO:
		case FIP_DT_ELP:
			if (fh)
				goto drop;
			if (dlen < sizeof(*els) + sizeof(*fh) + 1)
				goto len_err;
			els_len = dlen - sizeof(*els);
			els = (struct fip_encaps *)desc;
			fh = (struct fc_frame_header *)(els + 1);
			els_dtype = desc->fip_dtype;
			break;
		default:
			FIP_DBG("unexpected descriptor type %x "
				"in FIP adv\n", desc->fip_dtype);
			/* standard says ignore unknown descriptors >= 128 */
			if (desc->fip_dtype < FIP_DT_VENDOR_BASE)
				goto drop;
			continue;
		}
		desc = (struct fip_desc *)((char *)desc + dlen);
		rlen -= dlen;
	}

	if (!fh)
		goto drop;
	els_op = *(u8 *)(fh + 1);

	if (els_dtype == FIP_DT_FLOGI && sub == FIP_SC_REP &&
	    fip->flogi_oxid == ntohs(fh->fh_ox_id) &&
	    els_op == ELS_LS_ACC && is_valid_ether_addr(granted_mac)) {
		fip->flogi_oxid = FC_XID_UNKNOWN;
		fip->update_mac(fip, fip->data_src_addr, granted_mac);
		memcpy(fip->data_src_addr, granted_mac, ETH_ALEN);
	}

	/*
	 * Convert skb into an fc_frame containing only the ELS.
	 */
	skb_pull(skb, (u8 *)fh - skb->data);
	skb_trim(skb, els_len);
	fp = (struct fc_frame *)skb;
	fc_frame_init(fp);
	fr_sof(fp) = FC_SOF_I3;
	fr_eof(fp) = FC_EOF_T;
	fr_dev(fp) = lp;

	stats = fc_lport_get_stats(lp);
	stats->RxFrames++;
	stats->RxWords += skb->len / FIP_BPW;

	fc_exch_recv(lp, lp->emp, fp);
	return;

len_err:
	FIP_DBG("FIP length error in descriptor type %x len %zu\n",
		desc->fip_dtype, dlen);
drop:
	kfree_skb(skb);
}

/**
 * fcoe_ctlr_recv_els() - Handle an incoming link reset frame.
 * @fip:	FCoE controller.
 * @fh:		Received FIP header.
 *
 * There may be multiple VN_Port descriptors.
 * The overall length has already been checked.
 */
static void fcoe_ctlr_recv_clr_vlink(struct fcoe_ctlr *fip,
				      struct fip_header *fh)
{
	struct fip_desc *desc;
	struct fip_mac_desc *mp;
	struct fip_wwn_desc *wp;
	struct fip_vn_desc *vp;
	size_t rlen;
	size_t dlen;
	struct fcoe_fcf *fcf = fip->sel_fcf;
	struct fc_lport *lp = fip->lp;
	u32	desc_mask;

	FIP_DBG("Clear Virtual Link received\n");
	if (!fcf)
		return;
	if (!fcf || !fc_host_port_id(lp->host))
		return;

	/*
	 * mask of required descriptors.  Validating each one clears its bit.
	 */
	desc_mask = BIT(FIP_DT_MAC) | BIT(FIP_DT_NAME) | BIT(FIP_DT_VN_ID);

	rlen = ntohs(fh->fip_dl_len) * FIP_BPW;
	desc = (struct fip_desc *)(fh + 1);
	while (rlen >= sizeof(*desc)) {
		dlen = desc->fip_dlen * FIP_BPW;
		if (dlen > rlen)
			return;
		switch (desc->fip_dtype) {
		case FIP_DT_MAC:
			mp = (struct fip_mac_desc *)desc;
			if (dlen < sizeof(*mp))
				return;
			if (compare_ether_addr(mp->fd_mac, fcf->fcf_mac))
				return;
			desc_mask &= ~BIT(FIP_DT_MAC);
			break;
		case FIP_DT_NAME:
			wp = (struct fip_wwn_desc *)desc;
			if (dlen < sizeof(*wp))
				return;
			if (get_unaligned_be64(&wp->fd_wwn) != fcf->switch_name)
				return;
			desc_mask &= ~BIT(FIP_DT_NAME);
			break;
		case FIP_DT_VN_ID:
			vp = (struct fip_vn_desc *)desc;
			if (dlen < sizeof(*vp))
				return;
			if (compare_ether_addr(vp->fd_mac,
			    fip->data_src_addr) == 0 &&
			    get_unaligned_be64(&vp->fd_wwpn) == lp->wwpn &&
			    ntoh24(vp->fd_fc_id) == fc_host_port_id(lp->host))
				desc_mask &= ~BIT(FIP_DT_VN_ID);
			break;
		default:
			/* standard says ignore unknown descriptors >= 128 */
			if (desc->fip_dtype < FIP_DT_VENDOR_BASE)
				return;
			break;
		}
		desc = (struct fip_desc *)((char *)desc + dlen);
		rlen -= dlen;
	}

	/*
	 * reset only if all required descriptors were present and valid.
	 */
	if (desc_mask) {
		FIP_DBG("missing descriptors mask %x\n", desc_mask);
	} else {
		FIP_DBG("performing Clear Virtual Link\n");
		fcoe_ctlr_reset(fip, FIP_ST_ENABLED);
	}
}

/**
 * fcoe_ctlr_recv() - Receive a FIP frame.
 * @fip:	FCoE controller.
 * @skb:	Received FIP packet.
 *
 * This is called from NET_RX_SOFTIRQ.
 */
void fcoe_ctlr_recv(struct fcoe_ctlr *fip, struct sk_buff *skb)
{
	spin_lock_bh(&fip->fip_recv_list.lock);
	__skb_queue_tail(&fip->fip_recv_list, skb);
	spin_unlock_bh(&fip->fip_recv_list.lock);
	schedule_work(&fip->recv_work);
}
EXPORT_SYMBOL(fcoe_ctlr_recv);

/**
 * fcoe_ctlr_recv_handler() - Receive a FIP frame.
 * @fip:	FCoE controller.
 * @skb:	Received FIP packet.
 *
 * Returns non-zero if the frame is dropped.
 */
static int fcoe_ctlr_recv_handler(struct fcoe_ctlr *fip, struct sk_buff *skb)
{
	struct fip_header *fiph;
	struct ethhdr *eh;
	enum fip_state state;
	u16 op;
	u8 sub;

	if (skb_linearize(skb))
		goto drop;
	if (skb->len < sizeof(*fiph))
		goto drop;
	eh = eth_hdr(skb);
	if (compare_ether_addr(eh->h_dest, fip->ctl_src_addr) &&
	    compare_ether_addr(eh->h_dest, FIP_ALL_ENODE_MACS))
		goto drop;
	fiph = (struct fip_header *)skb->data;
	op = ntohs(fiph->fip_op);
	sub = fiph->fip_subcode;

	FIP_DBG_LVL(2, "ver %x op %x/%x dl %x fl %x\n",
		    FIP_VER_DECAPS(fiph->fip_ver), op, sub,
		    ntohs(fiph->fip_dl_len), ntohs(fiph->fip_flags));

	if (FIP_VER_DECAPS(fiph->fip_ver) != FIP_VER)
		goto drop;
	if (ntohs(fiph->fip_dl_len) * FIP_BPW + sizeof(*fiph) > skb->len)
		goto drop;

	spin_lock_bh(&fip->lock);
	state = fip->state;
	if (state == FIP_ST_AUTO) {
		fip->map_dest = 0;
		fip->state = FIP_ST_ENABLED;
		state = FIP_ST_ENABLED;
		FIP_DBG("using FIP mode\n");
	}
	spin_unlock_bh(&fip->lock);
	if (state != FIP_ST_ENABLED)
		goto drop;

	if (op == FIP_OP_LS) {
		fcoe_ctlr_recv_els(fip, skb);	/* consumes skb */
		return 0;
	}
	if (op == FIP_OP_DISC && sub == FIP_SC_ADV)
		fcoe_ctlr_recv_adv(fip, skb);
	else if (op == FIP_OP_CTRL && sub == FIP_SC_CLR_VLINK)
		fcoe_ctlr_recv_clr_vlink(fip, fiph);
	kfree_skb(skb);
	return 0;
drop:
	kfree_skb(skb);
	return -1;
}

/**
 * fcoe_ctlr_select() - Select the best FCF, if possible.
 * @fip:	FCoE controller.
 *
 * If there are conflicting advertisements, no FCF can be chosen.
 *
 * Called with lock held.
 */
static void fcoe_ctlr_select(struct fcoe_ctlr *fip)
{
	struct fcoe_fcf *fcf;
	struct fcoe_fcf *best = NULL;

	list_for_each_entry(fcf, &fip->fcfs, list) {
		FIP_DBG("consider FCF for fab %llx VFID %d map %x val %d\n",
			fcf->fabric_name, fcf->vfid,
			fcf->fc_map, fcoe_ctlr_mtu_valid(fcf));
		if (!fcoe_ctlr_fcf_usable(fcf)) {
			FIP_DBG("FCF for fab %llx map %x %svalid %savailable\n",
				fcf->fabric_name, fcf->fc_map,
				(fcf->flags & FIP_FL_SOL) ? "" : "in",
				(fcf->flags & FIP_FL_AVAIL) ? "" : "un");
			continue;
		}
		if (!best) {
			best = fcf;
			continue;
		}
		if (fcf->fabric_name != best->fabric_name ||
		    fcf->vfid != best->vfid ||
		    fcf->fc_map != best->fc_map) {
			FIP_DBG("conflicting fabric, VFID, or FC-MAP\n");
			return;
		}
		if (fcf->pri < best->pri)
			best = fcf;
	}
	fip->sel_fcf = best;
}

/**
 * fcoe_ctlr_timeout() - FIP timer function.
 * @arg:	&fcoe_ctlr pointer.
 *
 * Ages FCFs.  Triggers FCF selection if possible.  Sends keep-alives.
 */
static void fcoe_ctlr_timeout(unsigned long arg)
{
	struct fcoe_ctlr *fip = (struct fcoe_ctlr *)arg;
	struct fcoe_fcf *sel;
	struct fcoe_fcf *fcf;
	unsigned long next_timer = jiffies + msecs_to_jiffies(FIP_VN_KA_PERIOD);
	DECLARE_MAC_BUF(buf);
	u8 send_ctlr_ka;
	u8 send_port_ka;

	spin_lock_bh(&fip->lock);
	if (fip->state == FIP_ST_DISABLED) {
		spin_unlock_bh(&fip->lock);
		return;
	}

	fcf = fip->sel_fcf;
	fcoe_ctlr_age_fcfs(fip);

	sel = fip->sel_fcf;
	if (!sel && fip->sel_time && time_after_eq(jiffies, fip->sel_time)) {
		fcoe_ctlr_select(fip);
		sel = fip->sel_fcf;
		fip->sel_time = 0;
	}

	if (sel != fcf) {
		fcf = sel;		/* the old FCF may have been freed */
		if (sel) {
			printk(KERN_INFO "host%d: FIP selected "
			       "Fibre-Channel Forwarder MAC %s\n",
			       fip->lp->host->host_no,
			       print_mac(buf, sel->fcf_mac));
			memcpy(fip->dest_addr, sel->fcf_mac, ETH_ALEN);
			fip->port_ka_time = jiffies +
					    msecs_to_jiffies(FIP_VN_KA_PERIOD);
			fip->ctlr_ka_time = jiffies + sel->fka_period;
			fip->link = 1;
		} else {
			printk(KERN_NOTICE "host%d: "
			       "FIP Fibre-Channel Forwarder timed out.  "
			       "Starting FCF discovery.\n",
			       fip->lp->host->host_no);
			fip->link = 0;
		}
		schedule_work(&fip->link_work);
	}

	send_ctlr_ka = 0;
	send_port_ka = 0;
	if (sel) {
		if (time_after_eq(jiffies, fip->ctlr_ka_time)) {
			fip->ctlr_ka_time = jiffies + sel->fka_period;
			send_ctlr_ka = 1;
		}
		if (time_after(next_timer, fip->ctlr_ka_time))
			next_timer = fip->ctlr_ka_time;

		if (time_after_eq(jiffies, fip->port_ka_time)) {
			fip->port_ka_time += jiffies +
					msecs_to_jiffies(FIP_VN_KA_PERIOD);
			send_port_ka = 1;
		}
		if (time_after(next_timer, fip->port_ka_time))
			next_timer = fip->port_ka_time;
		mod_timer(&fip->timer, next_timer);
	} else if (fip->sel_time) {
		next_timer = fip->sel_time +
				msecs_to_jiffies(FCOE_CTLR_START_DELAY);
		mod_timer(&fip->timer, next_timer);
	}
	spin_unlock_bh(&fip->lock);

	if (send_ctlr_ka)
		fcoe_ctlr_send_keep_alive(fip, 0, fip->ctl_src_addr);
	if (send_port_ka)
		fcoe_ctlr_send_keep_alive(fip, 1, fip->data_src_addr);
}

/**
 * fcoe_ctlr_link_work() - worker thread function for link changes.
 * @work:	pointer to link_work member inside &fcoe_ctlr.
 *
 * See if the link status has changed and if so, report it.
 *
 * This is here because fc_linkup() and fc_linkdown() must not
 * be called from the timer directly, since they use a mutex.
 */
static void fcoe_ctlr_link_work(struct work_struct *work)
{
	struct fcoe_ctlr *fip;
	int link;
	int last_link;

	fip = container_of(work, struct fcoe_ctlr, link_work);
	spin_lock_bh(&fip->lock);
	last_link = fip->last_link;
	link = fip->link;
	fip->last_link = link;
	spin_unlock_bh(&fip->lock);

	if (last_link != link) {
		if (link)
			fc_linkup(fip->lp);
		else
			fcoe_ctlr_reset(fip, FIP_ST_LINK_WAIT);
	}
}

/**
 * fcoe_ctlr_recv_work() - Worker thread function for receiving FIP frames.
 * @recv_work:	pointer to recv_work member inside &fcoe_ctlr.
 */
static void fcoe_ctlr_recv_work(struct work_struct *recv_work)
{
	struct fcoe_ctlr *fip;
	struct sk_buff *skb;

	fip = container_of(recv_work, struct fcoe_ctlr, recv_work);
	spin_lock_bh(&fip->fip_recv_list.lock);
	while ((skb = __skb_dequeue(&fip->fip_recv_list))) {
		spin_unlock_bh(&fip->fip_recv_list.lock);
		fcoe_ctlr_recv_handler(fip, skb);
		spin_lock_bh(&fip->fip_recv_list.lock);
	}
	spin_unlock_bh(&fip->fip_recv_list.lock);
}

/**
 * fcoe_ctlr_recv_flogi() - snoop Pre-FIP receipt of FLOGI response or request.
 * @fip:	FCoE controller.
 * @fp:		FC frame.
 * @sa:		Ethernet source MAC address from received FCoE frame.
 *
 * Snoop potential response to FLOGI or even incoming FLOGI.
 *
 * The caller has checked that we are waiting for login as indicated
 * by fip->flogi_oxid != FC_XID_UNKNOWN.
 *
 * The caller is responsible for freeing the frame.
 *
 * Return non-zero if the frame should not be delivered to libfc.
 */
int fcoe_ctlr_recv_flogi(struct fcoe_ctlr *fip, struct fc_frame *fp, u8 *sa)
{
	struct fc_frame_header *fh;
	u8 op;
	u8 mac[ETH_ALEN];

	fh = fc_frame_header_get(fp);
	if (fh->fh_type != FC_TYPE_ELS)
		return 0;

	op = fc_frame_payload_op(fp);
	if (op == ELS_LS_ACC && fh->fh_r_ctl == FC_RCTL_ELS_REP &&
	    fip->flogi_oxid == ntohs(fh->fh_ox_id)) {

		spin_lock_bh(&fip->lock);
		if (fip->state != FIP_ST_AUTO && fip->state != FIP_ST_NON_FIP) {
			spin_unlock_bh(&fip->lock);
			return -EINVAL;
		}
		fip->state = FIP_ST_NON_FIP;
		FIP_DBG("received FLOGI LS_ACC using non-FIP mode\n");

		/*
		 * FLOGI accepted.
		 * If the src mac addr is FC_OUI-based, then we mark the
		 * address_mode flag to use FC_OUI-based Ethernet DA.
		 * Otherwise we use the FCoE gateway addr
		 */
		if (!compare_ether_addr(sa, (u8[6])FC_FCOE_FLOGI_MAC)) {
			fip->map_dest = 1;
		} else {
			memcpy(fip->dest_addr, sa, ETH_ALEN);
			fip->map_dest = 0;
		}
		fip->flogi_oxid = FC_XID_UNKNOWN;
		memcpy(mac, fip->data_src_addr, ETH_ALEN);
		fc_fcoe_set_mac(fip->data_src_addr, fh->fh_d_id);
		spin_unlock_bh(&fip->lock);

		fip->update_mac(fip, mac, fip->data_src_addr);
	} else if (op == ELS_FLOGI && fh->fh_r_ctl == FC_RCTL_ELS_REQ && sa) {
		/*
		 * Save source MAC for point-to-point responses.
		 */
		spin_lock_bh(&fip->lock);
		if (fip->state == FIP_ST_AUTO || fip->state == FIP_ST_NON_FIP) {
			memcpy(fip->dest_addr, sa, ETH_ALEN);
			fip->map_dest = 0;
			if (fip->state == FIP_ST_NON_FIP)
				FIP_DBG("received FLOGI REQ, "
						"using non-FIP mode\n");
			fip->state = FIP_ST_NON_FIP;
		}
		spin_unlock_bh(&fip->lock);
	}
	return 0;
}
EXPORT_SYMBOL(fcoe_ctlr_recv_flogi);

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
