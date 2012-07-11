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
#include <linux/errno.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <net/rtnetlink.h>

#include <scsi/fc/fc_els.h>
#include <scsi/fc/fc_fs.h>
#include <scsi/fc/fc_fip.h>
#include <scsi/fc/fc_encaps.h>
#include <scsi/fc/fc_fcoe.h>
#include <scsi/fc/fc_fcp.h>

#include <scsi/libfc.h>
#include <scsi/libfcoe.h>

#include "libfcoe.h"

#define	FCOE_CTLR_MIN_FKA	500		/* min keep alive (mS) */
#define	FCOE_CTLR_DEF_FKA	FIP_DEF_FKA	/* default keep alive (mS) */

static void fcoe_ctlr_timeout(unsigned long);
static void fcoe_ctlr_timer_work(struct work_struct *);
static void fcoe_ctlr_recv_work(struct work_struct *);
static int fcoe_ctlr_flogi_retry(struct fcoe_ctlr *);

static void fcoe_ctlr_vn_start(struct fcoe_ctlr *);
static int fcoe_ctlr_vn_recv(struct fcoe_ctlr *, struct sk_buff *);
static void fcoe_ctlr_vn_timeout(struct fcoe_ctlr *);
static int fcoe_ctlr_vn_lookup(struct fcoe_ctlr *, u32, u8 *);

static u8 fcoe_all_fcfs[ETH_ALEN] = FIP_ALL_FCF_MACS;
static u8 fcoe_all_enode[ETH_ALEN] = FIP_ALL_ENODE_MACS;
static u8 fcoe_all_vn2vn[ETH_ALEN] = FIP_ALL_VN2VN_MACS;
static u8 fcoe_all_p2p[ETH_ALEN] = FIP_ALL_P2P_MACS;

static const char * const fcoe_ctlr_states[] = {
	[FIP_ST_DISABLED] =	"DISABLED",
	[FIP_ST_LINK_WAIT] =	"LINK_WAIT",
	[FIP_ST_AUTO] =		"AUTO",
	[FIP_ST_NON_FIP] =	"NON_FIP",
	[FIP_ST_ENABLED] =	"ENABLED",
	[FIP_ST_VNMP_START] =	"VNMP_START",
	[FIP_ST_VNMP_PROBE1] =	"VNMP_PROBE1",
	[FIP_ST_VNMP_PROBE2] =	"VNMP_PROBE2",
	[FIP_ST_VNMP_CLAIM] =	"VNMP_CLAIM",
	[FIP_ST_VNMP_UP] =	"VNMP_UP",
};

static const char *fcoe_ctlr_state(enum fip_state state)
{
	const char *cp = "unknown";

	if (state < ARRAY_SIZE(fcoe_ctlr_states))
		cp = fcoe_ctlr_states[state];
	if (!cp)
		cp = "unknown";
	return cp;
}

/**
 * fcoe_ctlr_set_state() - Set and do debug printing for the new FIP state.
 * @fip: The FCoE controller
 * @state: The new state
 */
static void fcoe_ctlr_set_state(struct fcoe_ctlr *fip, enum fip_state state)
{
	if (state == fip->state)
		return;
	if (fip->lp)
		LIBFCOE_FIP_DBG(fip, "state %s -> %s\n",
			fcoe_ctlr_state(fip->state), fcoe_ctlr_state(state));
	fip->state = state;
}

/**
 * fcoe_ctlr_mtu_valid() - Check if a FCF's MTU is valid
 * @fcf: The FCF to check
 *
 * Return non-zero if FCF fcoe_size has been validated.
 */
static inline int fcoe_ctlr_mtu_valid(const struct fcoe_fcf *fcf)
{
	return (fcf->flags & FIP_FL_SOL) != 0;
}

/**
 * fcoe_ctlr_fcf_usable() - Check if a FCF is usable
 * @fcf: The FCF to check
 *
 * Return non-zero if the FCF is usable.
 */
static inline int fcoe_ctlr_fcf_usable(struct fcoe_fcf *fcf)
{
	u16 flags = FIP_FL_SOL | FIP_FL_AVAIL;

	return (fcf->flags & flags) == flags;
}

/**
 * fcoe_ctlr_map_dest() - Set flag and OUI for mapping destination addresses
 * @fip: The FCoE controller
 */
static void fcoe_ctlr_map_dest(struct fcoe_ctlr *fip)
{
	if (fip->mode == FIP_MODE_VN2VN)
		hton24(fip->dest_addr, FIP_VN_FC_MAP);
	else
		hton24(fip->dest_addr, FIP_DEF_FC_MAP);
	hton24(fip->dest_addr + 3, 0);
	fip->map_dest = 1;
}

/**
 * fcoe_ctlr_init() - Initialize the FCoE Controller instance
 * @fip: The FCoE controller to initialize
 */
void fcoe_ctlr_init(struct fcoe_ctlr *fip, enum fip_state mode)
{
	fcoe_ctlr_set_state(fip, FIP_ST_LINK_WAIT);
	fip->mode = mode;
	INIT_LIST_HEAD(&fip->fcfs);
	mutex_init(&fip->ctlr_mutex);
	spin_lock_init(&fip->ctlr_lock);
	fip->flogi_oxid = FC_XID_UNKNOWN;
	setup_timer(&fip->timer, fcoe_ctlr_timeout, (unsigned long)fip);
	INIT_WORK(&fip->timer_work, fcoe_ctlr_timer_work);
	INIT_WORK(&fip->recv_work, fcoe_ctlr_recv_work);
	skb_queue_head_init(&fip->fip_recv_list);
}
EXPORT_SYMBOL(fcoe_ctlr_init);

static int fcoe_sysfs_fcf_add(struct fcoe_fcf *new)
{
	struct fcoe_ctlr *fip = new->fip;
	struct fcoe_ctlr_device *ctlr_dev = fcoe_ctlr_to_ctlr_dev(fip);
	struct fcoe_fcf_device temp, *fcf_dev;
	int rc = 0;

	LIBFCOE_FIP_DBG(fip, "New FCF fab %16.16llx mac %pM\n",
			new->fabric_name, new->fcf_mac);

	mutex_lock(&ctlr_dev->lock);

	temp.fabric_name = new->fabric_name;
	temp.switch_name = new->switch_name;
	temp.fc_map = new->fc_map;
	temp.vfid = new->vfid;
	memcpy(temp.mac, new->fcf_mac, ETH_ALEN);
	temp.priority = new->pri;
	temp.fka_period = new->fka_period;
	temp.selected = 0; /* default to unselected */

	fcf_dev = fcoe_fcf_device_add(ctlr_dev, &temp);
	if (unlikely(!fcf_dev)) {
		rc = -ENOMEM;
		goto out;
	}

	/*
	 * The fcoe_sysfs layer can return a CONNECTED fcf that
	 * has a priv (fcf was never deleted) or a CONNECTED fcf
	 * that doesn't have a priv (fcf was deleted). However,
	 * libfcoe will always delete FCFs before trying to add
	 * them. This is ensured because both recv_adv and
	 * age_fcfs are protected by the the fcoe_ctlr's mutex.
	 * This means that we should never get a FCF with a
	 * non-NULL priv pointer.
	 */
	BUG_ON(fcf_dev->priv);

	fcf_dev->priv = new;
	new->fcf_dev = fcf_dev;

	list_add(&new->list, &fip->fcfs);
	fip->fcf_count++;

out:
	mutex_unlock(&ctlr_dev->lock);
	return rc;
}

static void fcoe_sysfs_fcf_del(struct fcoe_fcf *new)
{
	struct fcoe_ctlr *fip = new->fip;
	struct fcoe_ctlr_device *ctlr_dev = fcoe_ctlr_to_ctlr_dev(fip);
	struct fcoe_fcf_device *fcf_dev;

	list_del(&new->list);
	fip->fcf_count--;

	mutex_lock(&ctlr_dev->lock);

	fcf_dev = fcoe_fcf_to_fcf_dev(new);
	WARN_ON(!fcf_dev);
	new->fcf_dev = NULL;
	fcoe_fcf_device_delete(fcf_dev);
	kfree(new);

	mutex_unlock(&ctlr_dev->lock);
}

/**
 * fcoe_ctlr_reset_fcfs() - Reset and free all FCFs for a controller
 * @fip: The FCoE controller whose FCFs are to be reset
 *
 * Called with &fcoe_ctlr lock held.
 */
static void fcoe_ctlr_reset_fcfs(struct fcoe_ctlr *fip)
{
	struct fcoe_fcf *fcf;
	struct fcoe_fcf *next;

	fip->sel_fcf = NULL;
	list_for_each_entry_safe(fcf, next, &fip->fcfs, list) {
		fcoe_sysfs_fcf_del(fcf);
	}
	WARN_ON(fip->fcf_count);

	fip->sel_time = 0;
}

/**
 * fcoe_ctlr_destroy() - Disable and tear down a FCoE controller
 * @fip: The FCoE controller to tear down
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
	cancel_work_sync(&fip->recv_work);
	skb_queue_purge(&fip->fip_recv_list);

	mutex_lock(&fip->ctlr_mutex);
	fcoe_ctlr_set_state(fip, FIP_ST_DISABLED);
	fcoe_ctlr_reset_fcfs(fip);
	mutex_unlock(&fip->ctlr_mutex);
	del_timer_sync(&fip->timer);
	cancel_work_sync(&fip->timer_work);
}
EXPORT_SYMBOL(fcoe_ctlr_destroy);

/**
 * fcoe_ctlr_announce() - announce new FCF selection
 * @fip: The FCoE controller
 *
 * Also sets the destination MAC for FCoE and control packets
 *
 * Called with neither ctlr_mutex nor ctlr_lock held.
 */
static void fcoe_ctlr_announce(struct fcoe_ctlr *fip)
{
	struct fcoe_fcf *sel;
	struct fcoe_fcf *fcf;

	mutex_lock(&fip->ctlr_mutex);
	spin_lock_bh(&fip->ctlr_lock);

	kfree_skb(fip->flogi_req);
	fip->flogi_req = NULL;
	list_for_each_entry(fcf, &fip->fcfs, list)
		fcf->flogi_sent = 0;

	spin_unlock_bh(&fip->ctlr_lock);
	sel = fip->sel_fcf;

	if (sel && !compare_ether_addr(sel->fcf_mac, fip->dest_addr))
		goto unlock;
	if (!is_zero_ether_addr(fip->dest_addr)) {
		printk(KERN_NOTICE "libfcoe: host%d: "
		       "FIP Fibre-Channel Forwarder MAC %pM deselected\n",
		       fip->lp->host->host_no, fip->dest_addr);
		memset(fip->dest_addr, 0, ETH_ALEN);
	}
	if (sel) {
		printk(KERN_INFO "libfcoe: host%d: FIP selected "
		       "Fibre-Channel Forwarder MAC %pM\n",
		       fip->lp->host->host_no, sel->fcf_mac);
		memcpy(fip->dest_addr, sel->fcoe_mac, ETH_ALEN);
		fip->map_dest = 0;
	}
unlock:
	mutex_unlock(&fip->ctlr_mutex);
}

/**
 * fcoe_ctlr_fcoe_size() - Return the maximum FCoE size required for VN_Port
 * @fip: The FCoE controller to get the maximum FCoE size from
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
 * fcoe_ctlr_solicit() - Send a FIP solicitation
 * @fip: The FCoE controller to send the solicitation on
 * @fcf: The destination FCF (if NULL, a multicast solicitation is sent)
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
		} __packed desc;
	}  __packed * sol;
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
	if (fip->spma)
		sol->fip.fip_flags |= htons(FIP_FL_SPMA);

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
	skb->priority = fip->priority;
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	fip->send(fip, skb);

	if (!fcf)
		fip->sol_time = jiffies;
}

/**
 * fcoe_ctlr_link_up() - Start FCoE controller
 * @fip: The FCoE controller to start
 *
 * Called from the LLD when the network link is ready.
 */
void fcoe_ctlr_link_up(struct fcoe_ctlr *fip)
{
	mutex_lock(&fip->ctlr_mutex);
	if (fip->state == FIP_ST_NON_FIP || fip->state == FIP_ST_AUTO) {
		mutex_unlock(&fip->ctlr_mutex);
		fc_linkup(fip->lp);
	} else if (fip->state == FIP_ST_LINK_WAIT) {
		fcoe_ctlr_set_state(fip, fip->mode);
		switch (fip->mode) {
		default:
			LIBFCOE_FIP_DBG(fip, "invalid mode %d\n", fip->mode);
			/* fall-through */
		case FIP_MODE_AUTO:
			LIBFCOE_FIP_DBG(fip, "%s", "setting AUTO mode.\n");
			/* fall-through */
		case FIP_MODE_FABRIC:
		case FIP_MODE_NON_FIP:
			mutex_unlock(&fip->ctlr_mutex);
			fc_linkup(fip->lp);
			fcoe_ctlr_solicit(fip, NULL);
			break;
		case FIP_MODE_VN2VN:
			fcoe_ctlr_vn_start(fip);
			mutex_unlock(&fip->ctlr_mutex);
			fc_linkup(fip->lp);
			break;
		}
	} else
		mutex_unlock(&fip->ctlr_mutex);
}
EXPORT_SYMBOL(fcoe_ctlr_link_up);

/**
 * fcoe_ctlr_reset() - Reset a FCoE controller
 * @fip:       The FCoE controller to reset
 */
static void fcoe_ctlr_reset(struct fcoe_ctlr *fip)
{
	fcoe_ctlr_reset_fcfs(fip);
	del_timer(&fip->timer);
	fip->ctlr_ka_time = 0;
	fip->port_ka_time = 0;
	fip->sol_time = 0;
	fip->flogi_oxid = FC_XID_UNKNOWN;
	fcoe_ctlr_map_dest(fip);
}

/**
 * fcoe_ctlr_link_down() - Stop a FCoE controller
 * @fip: The FCoE controller to be stopped
 *
 * Returns non-zero if the link was up and now isn't.
 *
 * Called from the LLD when the network link is not ready.
 * There may be multiple calls while the link is down.
 */
int fcoe_ctlr_link_down(struct fcoe_ctlr *fip)
{
	int link_dropped;

	LIBFCOE_FIP_DBG(fip, "link down.\n");
	mutex_lock(&fip->ctlr_mutex);
	fcoe_ctlr_reset(fip);
	link_dropped = fip->state != FIP_ST_LINK_WAIT;
	fcoe_ctlr_set_state(fip, FIP_ST_LINK_WAIT);
	mutex_unlock(&fip->ctlr_mutex);

	if (link_dropped)
		fc_linkdown(fip->lp);
	return link_dropped;
}
EXPORT_SYMBOL(fcoe_ctlr_link_down);

/**
 * fcoe_ctlr_send_keep_alive() - Send a keep-alive to the selected FCF
 * @fip:   The FCoE controller to send the FKA on
 * @lport: libfc fc_lport to send from
 * @ports: 0 for controller keep-alive, 1 for port keep-alive
 * @sa:	   The source MAC address
 *
 * A controller keep-alive is sent every fka_period (typically 8 seconds).
 * The source MAC is the native MAC address.
 *
 * A port keep-alive is sent every 90 seconds while logged in.
 * The source MAC is the assigned mapped source address.
 * The destination is the FCF's F-port.
 */
static void fcoe_ctlr_send_keep_alive(struct fcoe_ctlr *fip,
				      struct fc_lport *lport,
				      int ports, u8 *sa)
{
	struct sk_buff *skb;
	struct fip_kal {
		struct ethhdr eth;
		struct fip_header fip;
		struct fip_mac_desc mac;
	} __packed * kal;
	struct fip_vn_desc *vn;
	u32 len;
	struct fc_lport *lp;
	struct fcoe_fcf *fcf;

	fcf = fip->sel_fcf;
	lp = fip->lp;
	if (!fcf || (ports && !lp->port_id))
		return;

	len = sizeof(*kal) + ports * sizeof(*vn);
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
	if (fip->spma)
		kal->fip.fip_flags |= htons(FIP_FL_SPMA);

	kal->mac.fd_desc.fip_dtype = FIP_DT_MAC;
	kal->mac.fd_desc.fip_dlen = sizeof(kal->mac) / FIP_BPW;
	memcpy(kal->mac.fd_mac, fip->ctl_src_addr, ETH_ALEN);
	if (ports) {
		vn = (struct fip_vn_desc *)(kal + 1);
		vn->fd_desc.fip_dtype = FIP_DT_VN_ID;
		vn->fd_desc.fip_dlen = sizeof(*vn) / FIP_BPW;
		memcpy(vn->fd_mac, fip->get_src_addr(lport), ETH_ALEN);
		hton24(vn->fd_fc_id, lport->port_id);
		put_unaligned_be64(lport->wwpn, &vn->fd_wwpn);
	}
	skb_put(skb, len);
	skb->protocol = htons(ETH_P_FIP);
	skb->priority = fip->priority;
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	fip->send(fip, skb);
}

/**
 * fcoe_ctlr_encaps() - Encapsulate an ELS frame for FIP, without sending it
 * @fip:   The FCoE controller for the ELS frame
 * @dtype: The FIP descriptor type for the frame
 * @skb:   The FCoE ELS frame including FC header but no FCoE headers
 * @d_id:  The destination port ID.
 *
 * Returns non-zero error code on failure.
 *
 * The caller must check that the length is a multiple of 4.
 *
 * The @skb must have enough headroom (28 bytes) and tailroom (8 bytes).
 * Headroom includes the FIP encapsulation description, FIP header, and
 * Ethernet header.  The tailroom is for the FIP MAC descriptor.
 */
static int fcoe_ctlr_encaps(struct fcoe_ctlr *fip, struct fc_lport *lport,
			    u8 dtype, struct sk_buff *skb, u32 d_id)
{
	struct fip_encaps_head {
		struct ethhdr eth;
		struct fip_header fip;
		struct fip_encaps encaps;
	} __packed * cap;
	struct fc_frame_header *fh;
	struct fip_mac_desc *mac;
	struct fcoe_fcf *fcf;
	size_t dlen;
	u16 fip_flags;
	u8 op;

	fh = (struct fc_frame_header *)skb->data;
	op = *(u8 *)(fh + 1);
	dlen = sizeof(struct fip_encaps) + skb->len;	/* len before push */
	cap = (struct fip_encaps_head *)skb_push(skb, sizeof(*cap));
	memset(cap, 0, sizeof(*cap));

	if (lport->point_to_multipoint) {
		if (fcoe_ctlr_vn_lookup(fip, d_id, cap->eth.h_dest))
			return -ENODEV;
		fip_flags = 0;
	} else {
		fcf = fip->sel_fcf;
		if (!fcf)
			return -ENODEV;
		fip_flags = fcf->flags;
		fip_flags &= fip->spma ? FIP_FL_SPMA | FIP_FL_FPMA :
					 FIP_FL_FPMA;
		if (!fip_flags)
			return -ENODEV;
		memcpy(cap->eth.h_dest, fcf->fcf_mac, ETH_ALEN);
	}
	memcpy(cap->eth.h_source, fip->ctl_src_addr, ETH_ALEN);
	cap->eth.h_proto = htons(ETH_P_FIP);

	cap->fip.fip_ver = FIP_VER_ENCAPS(FIP_VER);
	cap->fip.fip_op = htons(FIP_OP_LS);
	if (op == ELS_LS_ACC || op == ELS_LS_RJT)
		cap->fip.fip_subcode = FIP_SC_REP;
	else
		cap->fip.fip_subcode = FIP_SC_REQ;
	cap->fip.fip_flags = htons(fip_flags);

	cap->encaps.fd_desc.fip_dtype = dtype;
	cap->encaps.fd_desc.fip_dlen = dlen / FIP_BPW;

	if (op != ELS_LS_RJT) {
		dlen += sizeof(*mac);
		mac = (struct fip_mac_desc *)skb_put(skb, sizeof(*mac));
		memset(mac, 0, sizeof(*mac));
		mac->fd_desc.fip_dtype = FIP_DT_MAC;
		mac->fd_desc.fip_dlen = sizeof(*mac) / FIP_BPW;
		if (dtype != FIP_DT_FLOGI && dtype != FIP_DT_FDISC) {
			memcpy(mac->fd_mac, fip->get_src_addr(lport), ETH_ALEN);
		} else if (fip->mode == FIP_MODE_VN2VN) {
			hton24(mac->fd_mac, FIP_VN_FC_MAP);
			hton24(mac->fd_mac + 3, fip->port_id);
		} else if (fip_flags & FIP_FL_SPMA) {
			LIBFCOE_FIP_DBG(fip, "FLOGI/FDISC sent with SPMA\n");
			memcpy(mac->fd_mac, fip->ctl_src_addr, ETH_ALEN);
		} else {
			LIBFCOE_FIP_DBG(fip, "FLOGI/FDISC sent with FPMA\n");
			/* FPMA only FLOGI.  Must leave the MAC desc zeroed. */
		}
	}
	cap->fip.fip_dl_len = htons(dlen / FIP_BPW);

	skb->protocol = htons(ETH_P_FIP);
	skb->priority = fip->priority;
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	return 0;
}

/**
 * fcoe_ctlr_els_send() - Send an ELS frame encapsulated by FIP if appropriate.
 * @fip:	FCoE controller.
 * @lport:	libfc fc_lport to send from
 * @skb:	FCoE ELS frame including FC header but no FCoE headers.
 *
 * Returns a non-zero error code if the frame should not be sent.
 * Returns zero if the caller should send the frame with FCoE encapsulation.
 *
 * The caller must check that the length is a multiple of 4.
 * The SKB must have enough headroom (28 bytes) and tailroom (8 bytes).
 * The the skb must also be an fc_frame.
 *
 * This is called from the lower-level driver with spinlocks held,
 * so we must not take a mutex here.
 */
int fcoe_ctlr_els_send(struct fcoe_ctlr *fip, struct fc_lport *lport,
		       struct sk_buff *skb)
{
	struct fc_frame *fp;
	struct fc_frame_header *fh;
	u16 old_xid;
	u8 op;
	u8 mac[ETH_ALEN];

	fp = container_of(skb, struct fc_frame, skb);
	fh = (struct fc_frame_header *)skb->data;
	op = *(u8 *)(fh + 1);

	if (op == ELS_FLOGI && fip->mode != FIP_MODE_VN2VN) {
		old_xid = fip->flogi_oxid;
		fip->flogi_oxid = ntohs(fh->fh_ox_id);
		if (fip->state == FIP_ST_AUTO) {
			if (old_xid == FC_XID_UNKNOWN)
				fip->flogi_count = 0;
			fip->flogi_count++;
			if (fip->flogi_count < 3)
				goto drop;
			fcoe_ctlr_map_dest(fip);
			return 0;
		}
		if (fip->state == FIP_ST_NON_FIP)
			fcoe_ctlr_map_dest(fip);
	}

	if (fip->state == FIP_ST_NON_FIP)
		return 0;
	if (!fip->sel_fcf && fip->mode != FIP_MODE_VN2VN)
		goto drop;
	switch (op) {
	case ELS_FLOGI:
		op = FIP_DT_FLOGI;
		if (fip->mode == FIP_MODE_VN2VN)
			break;
		spin_lock_bh(&fip->ctlr_lock);
		kfree_skb(fip->flogi_req);
		fip->flogi_req = skb;
		fip->flogi_req_send = 1;
		spin_unlock_bh(&fip->ctlr_lock);
		schedule_work(&fip->timer_work);
		return -EINPROGRESS;
	case ELS_FDISC:
		if (ntoh24(fh->fh_s_id))
			return 0;
		op = FIP_DT_FDISC;
		break;
	case ELS_LOGO:
		if (fip->mode == FIP_MODE_VN2VN) {
			if (fip->state != FIP_ST_VNMP_UP)
				return -EINVAL;
			if (ntoh24(fh->fh_d_id) == FC_FID_FLOGI)
				return -EINVAL;
		} else {
			if (fip->state != FIP_ST_ENABLED)
				return 0;
			if (ntoh24(fh->fh_d_id) != FC_FID_FLOGI)
				return 0;
		}
		op = FIP_DT_LOGO;
		break;
	case ELS_LS_ACC:
		/*
		 * If non-FIP, we may have gotten an SID by accepting an FLOGI
		 * from a point-to-point connection.  Switch to using
		 * the source mac based on the SID.  The destination
		 * MAC in this case would have been set by receiving the
		 * FLOGI.
		 */
		if (fip->state == FIP_ST_NON_FIP) {
			if (fip->flogi_oxid == FC_XID_UNKNOWN)
				return 0;
			fip->flogi_oxid = FC_XID_UNKNOWN;
			fc_fcoe_set_mac(mac, fh->fh_d_id);
			fip->update_mac(lport, mac);
		}
		/* fall through */
	case ELS_LS_RJT:
		op = fr_encaps(fp);
		if (op)
			break;
		return 0;
	default:
		if (fip->state != FIP_ST_ENABLED &&
		    fip->state != FIP_ST_VNMP_UP)
			goto drop;
		return 0;
	}
	LIBFCOE_FIP_DBG(fip, "els_send op %u d_id %x\n",
			op, ntoh24(fh->fh_d_id));
	if (fcoe_ctlr_encaps(fip, lport, op, skb, ntoh24(fh->fh_d_id)))
		goto drop;
	fip->send(fip, skb);
	return -EINPROGRESS;
drop:
	kfree_skb(skb);
	return -EINVAL;
}
EXPORT_SYMBOL(fcoe_ctlr_els_send);

/**
 * fcoe_ctlr_age_fcfs() - Reset and free all old FCFs for a controller
 * @fip: The FCoE controller to free FCFs on
 *
 * Called with lock held and preemption disabled.
 *
 * An FCF is considered old if we have missed two advertisements.
 * That is, there have been no valid advertisement from it for 2.5
 * times its keep-alive period.
 *
 * In addition, determine the time when an FCF selection can occur.
 *
 * Also, increment the MissDiscAdvCount when no advertisement is received
 * for the corresponding FCF for 1.5 * FKA_ADV_PERIOD (FC-BB-5 LESB).
 *
 * Returns the time in jiffies for the next call.
 */
static unsigned long fcoe_ctlr_age_fcfs(struct fcoe_ctlr *fip)
{
	struct fcoe_fcf *fcf;
	struct fcoe_fcf *next;
	unsigned long next_timer = jiffies + msecs_to_jiffies(FIP_VN_KA_PERIOD);
	unsigned long deadline;
	unsigned long sel_time = 0;
	struct list_head del_list;
	struct fcoe_dev_stats *stats;

	INIT_LIST_HEAD(&del_list);

	stats = per_cpu_ptr(fip->lp->dev_stats, get_cpu());

	list_for_each_entry_safe(fcf, next, &fip->fcfs, list) {
		deadline = fcf->time + fcf->fka_period + fcf->fka_period / 2;
		if (fip->sel_fcf == fcf) {
			if (time_after(jiffies, deadline)) {
				stats->MissDiscAdvCount++;
				printk(KERN_INFO "libfcoe: host%d: "
				       "Missing Discovery Advertisement "
				       "for fab %16.16llx count %lld\n",
				       fip->lp->host->host_no, fcf->fabric_name,
				       stats->MissDiscAdvCount);
			} else if (time_after(next_timer, deadline))
				next_timer = deadline;
		}

		deadline += fcf->fka_period;
		if (time_after_eq(jiffies, deadline)) {
			if (fip->sel_fcf == fcf)
				fip->sel_fcf = NULL;
			/*
			 * Move to delete list so we can call
			 * fcoe_sysfs_fcf_del (which can sleep)
			 * after the put_cpu().
			 */
			list_del(&fcf->list);
			list_add(&fcf->list, &del_list);
			stats->VLinkFailureCount++;
		} else {
			if (time_after(next_timer, deadline))
				next_timer = deadline;
			if (fcoe_ctlr_mtu_valid(fcf) &&
			    (!sel_time || time_before(sel_time, fcf->time)))
				sel_time = fcf->time;
		}
	}
	put_cpu();

	list_for_each_entry_safe(fcf, next, &del_list, list) {
		/* Removes fcf from current list */
		fcoe_sysfs_fcf_del(fcf);
	}

	if (sel_time && !fip->sel_fcf && !fip->sel_time) {
		sel_time += msecs_to_jiffies(FCOE_CTLR_START_DELAY);
		fip->sel_time = sel_time;
	}

	return next_timer;
}

/**
 * fcoe_ctlr_parse_adv() - Decode a FIP advertisement into a new FCF entry
 * @fip: The FCoE controller receiving the advertisement
 * @skb: The received FIP advertisement frame
 * @fcf: The resulting FCF entry
 *
 * Returns zero on a valid parsed advertisement,
 * otherwise returns non zero value.
 */
static int fcoe_ctlr_parse_adv(struct fcoe_ctlr *fip,
			       struct sk_buff *skb, struct fcoe_fcf *fcf)
{
	struct fip_header *fiph;
	struct fip_desc *desc = NULL;
	struct fip_wwn_desc *wwn;
	struct fip_fab_desc *fab;
	struct fip_fka_desc *fka;
	unsigned long t;
	size_t rlen;
	size_t dlen;
	u32 desc_mask;

	memset(fcf, 0, sizeof(*fcf));
	fcf->fka_period = msecs_to_jiffies(FCOE_CTLR_DEF_FKA);

	fiph = (struct fip_header *)skb->data;
	fcf->flags = ntohs(fiph->fip_flags);

	/*
	 * mask of required descriptors. validating each one clears its bit.
	 */
	desc_mask = BIT(FIP_DT_PRI) | BIT(FIP_DT_MAC) | BIT(FIP_DT_NAME) |
			BIT(FIP_DT_FAB) | BIT(FIP_DT_FKA);

	rlen = ntohs(fiph->fip_dl_len) * 4;
	if (rlen + sizeof(*fiph) > skb->len)
		return -EINVAL;

	desc = (struct fip_desc *)(fiph + 1);
	while (rlen > 0) {
		dlen = desc->fip_dlen * FIP_BPW;
		if (dlen < sizeof(*desc) || dlen > rlen)
			return -EINVAL;
		/* Drop Adv if there are duplicate critical descriptors */
		if ((desc->fip_dtype < 32) &&
		    !(desc_mask & 1U << desc->fip_dtype)) {
			LIBFCOE_FIP_DBG(fip, "Duplicate Critical "
					"Descriptors in FIP adv\n");
			return -EINVAL;
		}
		switch (desc->fip_dtype) {
		case FIP_DT_PRI:
			if (dlen != sizeof(struct fip_pri_desc))
				goto len_err;
			fcf->pri = ((struct fip_pri_desc *)desc)->fd_pri;
			desc_mask &= ~BIT(FIP_DT_PRI);
			break;
		case FIP_DT_MAC:
			if (dlen != sizeof(struct fip_mac_desc))
				goto len_err;
			memcpy(fcf->fcf_mac,
			       ((struct fip_mac_desc *)desc)->fd_mac,
			       ETH_ALEN);
			memcpy(fcf->fcoe_mac, fcf->fcf_mac, ETH_ALEN);
			if (!is_valid_ether_addr(fcf->fcf_mac)) {
				LIBFCOE_FIP_DBG(fip,
					"Invalid MAC addr %pM in FIP adv\n",
					fcf->fcf_mac);
				return -EINVAL;
			}
			desc_mask &= ~BIT(FIP_DT_MAC);
			break;
		case FIP_DT_NAME:
			if (dlen != sizeof(struct fip_wwn_desc))
				goto len_err;
			wwn = (struct fip_wwn_desc *)desc;
			fcf->switch_name = get_unaligned_be64(&wwn->fd_wwn);
			desc_mask &= ~BIT(FIP_DT_NAME);
			break;
		case FIP_DT_FAB:
			if (dlen != sizeof(struct fip_fab_desc))
				goto len_err;
			fab = (struct fip_fab_desc *)desc;
			fcf->fabric_name = get_unaligned_be64(&fab->fd_wwn);
			fcf->vfid = ntohs(fab->fd_vfid);
			fcf->fc_map = ntoh24(fab->fd_map);
			desc_mask &= ~BIT(FIP_DT_FAB);
			break;
		case FIP_DT_FKA:
			if (dlen != sizeof(struct fip_fka_desc))
				goto len_err;
			fka = (struct fip_fka_desc *)desc;
			if (fka->fd_flags & FIP_FKA_ADV_D)
				fcf->fd_flags = 1;
			t = ntohl(fka->fd_fka_period);
			if (t >= FCOE_CTLR_MIN_FKA)
				fcf->fka_period = msecs_to_jiffies(t);
			desc_mask &= ~BIT(FIP_DT_FKA);
			break;
		case FIP_DT_MAP_OUI:
		case FIP_DT_FCOE_SIZE:
		case FIP_DT_FLOGI:
		case FIP_DT_FDISC:
		case FIP_DT_LOGO:
		case FIP_DT_ELP:
		default:
			LIBFCOE_FIP_DBG(fip, "unexpected descriptor type %x "
					"in FIP adv\n", desc->fip_dtype);
			/* standard says ignore unknown descriptors >= 128 */
			if (desc->fip_dtype < FIP_DT_VENDOR_BASE)
				return -EINVAL;
			break;
		}
		desc = (struct fip_desc *)((char *)desc + dlen);
		rlen -= dlen;
	}
	if (!fcf->fc_map || (fcf->fc_map & 0x10000))
		return -EINVAL;
	if (!fcf->switch_name)
		return -EINVAL;
	if (desc_mask) {
		LIBFCOE_FIP_DBG(fip, "adv missing descriptors mask %x\n",
				desc_mask);
		return -EINVAL;
	}
	return 0;

len_err:
	LIBFCOE_FIP_DBG(fip, "FIP length error in descriptor type %x len %zu\n",
			desc->fip_dtype, dlen);
	return -EINVAL;
}

/**
 * fcoe_ctlr_recv_adv() - Handle an incoming advertisement
 * @fip: The FCoE controller receiving the advertisement
 * @skb: The received FIP packet
 */
static void fcoe_ctlr_recv_adv(struct fcoe_ctlr *fip, struct sk_buff *skb)
{
	struct fcoe_fcf *fcf;
	struct fcoe_fcf new;
	unsigned long sol_tov = msecs_to_jiffies(FCOE_CTRL_SOL_TOV);
	int first = 0;
	int mtu_valid;
	int found = 0;
	int rc = 0;

	if (fcoe_ctlr_parse_adv(fip, skb, &new))
		return;

	mutex_lock(&fip->ctlr_mutex);
	first = list_empty(&fip->fcfs);
	list_for_each_entry(fcf, &fip->fcfs, list) {
		if (fcf->switch_name == new.switch_name &&
		    fcf->fabric_name == new.fabric_name &&
		    fcf->fc_map == new.fc_map &&
		    compare_ether_addr(fcf->fcf_mac, new.fcf_mac) == 0) {
			found = 1;
			break;
		}
	}
	if (!found) {
		if (fip->fcf_count >= FCOE_CTLR_FCF_LIMIT)
			goto out;

		fcf = kmalloc(sizeof(*fcf), GFP_ATOMIC);
		if (!fcf)
			goto out;

		memcpy(fcf, &new, sizeof(new));
		fcf->fip = fip;
		rc = fcoe_sysfs_fcf_add(fcf);
		if (rc) {
			printk(KERN_ERR "Failed to allocate sysfs instance "
			       "for FCF, fab %16.16llx mac %pM\n",
			       new.fabric_name, new.fcf_mac);
			kfree(fcf);
			goto out;
		}
	} else {
		/*
		 * Update the FCF's keep-alive descriptor flags.
		 * Other flag changes from new advertisements are
		 * ignored after a solicited advertisement is
		 * received and the FCF is selectable (usable).
		 */
		fcf->fd_flags = new.fd_flags;
		if (!fcoe_ctlr_fcf_usable(fcf))
			fcf->flags = new.flags;

		if (fcf == fip->sel_fcf && !fcf->fd_flags) {
			fip->ctlr_ka_time -= fcf->fka_period;
			fip->ctlr_ka_time += new.fka_period;
			if (time_before(fip->ctlr_ka_time, fip->timer.expires))
				mod_timer(&fip->timer, fip->ctlr_ka_time);
		}
		fcf->fka_period = new.fka_period;
		memcpy(fcf->fcf_mac, new.fcf_mac, ETH_ALEN);
	}

	mtu_valid = fcoe_ctlr_mtu_valid(fcf);
	fcf->time = jiffies;
	if (!found)
		LIBFCOE_FIP_DBG(fip, "New FCF fab %16.16llx mac %pM\n",
				fcf->fabric_name, fcf->fcf_mac);

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
	 * Put this FCF at the head of the list for priority among equals.
	 * This helps in the case of an NPV switch which insists we use
	 * the FCF that answers multicast solicitations, not the others that
	 * are sending periodic multicast advertisements.
	 */
	if (mtu_valid)
		list_move(&fcf->list, &fip->fcfs);

	/*
	 * If this is the first validated FCF, note the time and
	 * set a timer to trigger selection.
	 */
	if (mtu_valid && !fip->sel_fcf && fcoe_ctlr_fcf_usable(fcf)) {
		fip->sel_time = jiffies +
			msecs_to_jiffies(FCOE_CTLR_START_DELAY);
		if (!timer_pending(&fip->timer) ||
		    time_before(fip->sel_time, fip->timer.expires))
			mod_timer(&fip->timer, fip->sel_time);
	}

out:
	mutex_unlock(&fip->ctlr_mutex);
}

/**
 * fcoe_ctlr_recv_els() - Handle an incoming FIP encapsulated ELS frame
 * @fip: The FCoE controller which received the packet
 * @skb: The received FIP packet
 */
static void fcoe_ctlr_recv_els(struct fcoe_ctlr *fip, struct sk_buff *skb)
{
	struct fc_lport *lport = fip->lp;
	struct fip_header *fiph;
	struct fc_frame *fp = (struct fc_frame *)skb;
	struct fc_frame_header *fh = NULL;
	struct fip_desc *desc;
	struct fip_encaps *els;
	struct fcoe_dev_stats *stats;
	struct fcoe_fcf *sel;
	enum fip_desc_type els_dtype = 0;
	u8 els_op;
	u8 sub;
	u8 granted_mac[ETH_ALEN] = { 0 };
	size_t els_len = 0;
	size_t rlen;
	size_t dlen;
	u32 desc_mask = 0;
	u32 desc_cnt = 0;

	fiph = (struct fip_header *)skb->data;
	sub = fiph->fip_subcode;
	if (sub != FIP_SC_REQ && sub != FIP_SC_REP)
		goto drop;

	rlen = ntohs(fiph->fip_dl_len) * 4;
	if (rlen + sizeof(*fiph) > skb->len)
		goto drop;

	desc = (struct fip_desc *)(fiph + 1);
	while (rlen > 0) {
		desc_cnt++;
		dlen = desc->fip_dlen * FIP_BPW;
		if (dlen < sizeof(*desc) || dlen > rlen)
			goto drop;
		/* Drop ELS if there are duplicate critical descriptors */
		if (desc->fip_dtype < 32) {
			if ((desc->fip_dtype != FIP_DT_MAC) &&
			    (desc_mask & 1U << desc->fip_dtype)) {
				LIBFCOE_FIP_DBG(fip, "Duplicate Critical "
						"Descriptors in FIP ELS\n");
				goto drop;
			}
			desc_mask |= (1 << desc->fip_dtype);
		}
		switch (desc->fip_dtype) {
		case FIP_DT_MAC:
			sel = fip->sel_fcf;
			if (desc_cnt == 1) {
				LIBFCOE_FIP_DBG(fip, "FIP descriptors "
						"received out of order\n");
				goto drop;
			}
			/*
			 * Some switch implementations send two MAC descriptors,
			 * with first MAC(granted_mac) being the FPMA, and the
			 * second one(fcoe_mac) is used as destination address
			 * for sending/receiving FCoE packets. FIP traffic is
			 * sent using fip_mac. For regular switches, both
			 * fip_mac and fcoe_mac would be the same.
			 */
			if (desc_cnt == 2)
				memcpy(granted_mac,
				       ((struct fip_mac_desc *)desc)->fd_mac,
				       ETH_ALEN);

			if (dlen != sizeof(struct fip_mac_desc))
				goto len_err;

			if ((desc_cnt == 3) && (sel))
				memcpy(sel->fcoe_mac,
				       ((struct fip_mac_desc *)desc)->fd_mac,
				       ETH_ALEN);
			break;
		case FIP_DT_FLOGI:
		case FIP_DT_FDISC:
		case FIP_DT_LOGO:
		case FIP_DT_ELP:
			if (desc_cnt != 1) {
				LIBFCOE_FIP_DBG(fip, "FIP descriptors "
						"received out of order\n");
				goto drop;
			}
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
			LIBFCOE_FIP_DBG(fip, "unexpected descriptor type %x "
					"in FIP adv\n", desc->fip_dtype);
			/* standard says ignore unknown descriptors >= 128 */
			if (desc->fip_dtype < FIP_DT_VENDOR_BASE)
				goto drop;
			if (desc_cnt <= 2) {
				LIBFCOE_FIP_DBG(fip, "FIP descriptors "
						"received out of order\n");
				goto drop;
			}
			break;
		}
		desc = (struct fip_desc *)((char *)desc + dlen);
		rlen -= dlen;
	}

	if (!fh)
		goto drop;
	els_op = *(u8 *)(fh + 1);

	if ((els_dtype == FIP_DT_FLOGI || els_dtype == FIP_DT_FDISC) &&
	    sub == FIP_SC_REP && fip->mode != FIP_MODE_VN2VN) {
		if (els_op == ELS_LS_ACC) {
			if (!is_valid_ether_addr(granted_mac)) {
				LIBFCOE_FIP_DBG(fip,
					"Invalid MAC address %pM in FIP ELS\n",
					granted_mac);
				goto drop;
			}
			memcpy(fr_cb(fp)->granted_mac, granted_mac, ETH_ALEN);

			if (fip->flogi_oxid == ntohs(fh->fh_ox_id)) {
				fip->flogi_oxid = FC_XID_UNKNOWN;
				if (els_dtype == FIP_DT_FLOGI)
					fcoe_ctlr_announce(fip);
			}
		} else if (els_dtype == FIP_DT_FLOGI &&
			   !fcoe_ctlr_flogi_retry(fip))
			goto drop;	/* retrying FLOGI so drop reject */
	}

	if ((desc_cnt == 0) || ((els_op != ELS_LS_RJT) &&
	    (!(1U << FIP_DT_MAC & desc_mask)))) {
		LIBFCOE_FIP_DBG(fip, "Missing critical descriptors "
				"in FIP ELS\n");
		goto drop;
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
	fr_dev(fp) = lport;
	fr_encaps(fp) = els_dtype;

	stats = per_cpu_ptr(lport->dev_stats, get_cpu());
	stats->RxFrames++;
	stats->RxWords += skb->len / FIP_BPW;
	put_cpu();

	fc_exch_recv(lport, fp);
	return;

len_err:
	LIBFCOE_FIP_DBG(fip, "FIP length error in descriptor type %x len %zu\n",
			desc->fip_dtype, dlen);
drop:
	kfree_skb(skb);
}

/**
 * fcoe_ctlr_recv_els() - Handle an incoming link reset frame
 * @fip: The FCoE controller that received the frame
 * @fh:	 The received FIP header
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
	struct fc_lport *lport = fip->lp;
	struct fc_lport *vn_port = NULL;
	u32 desc_mask;
	int num_vlink_desc;
	int reset_phys_port = 0;
	struct fip_vn_desc **vlink_desc_arr = NULL;

	LIBFCOE_FIP_DBG(fip, "Clear Virtual Link received\n");

	if (!fcf || !lport->port_id)
		return;

	/*
	 * mask of required descriptors.  Validating each one clears its bit.
	 */
	desc_mask = BIT(FIP_DT_MAC) | BIT(FIP_DT_NAME);

	rlen = ntohs(fh->fip_dl_len) * FIP_BPW;
	desc = (struct fip_desc *)(fh + 1);

	/*
	 * Actually need to subtract 'sizeof(*mp) - sizeof(*wp)' from 'rlen'
	 * before determining max Vx_Port descriptor but a buggy FCF could have
	 * omited either or both MAC Address and Name Identifier descriptors
	 */
	num_vlink_desc = rlen / sizeof(*vp);
	if (num_vlink_desc)
		vlink_desc_arr = kmalloc(sizeof(vp) * num_vlink_desc,
					 GFP_ATOMIC);
	if (!vlink_desc_arr)
		return;
	num_vlink_desc = 0;

	while (rlen >= sizeof(*desc)) {
		dlen = desc->fip_dlen * FIP_BPW;
		if (dlen > rlen)
			goto err;
		/* Drop CVL if there are duplicate critical descriptors */
		if ((desc->fip_dtype < 32) &&
		    (desc->fip_dtype != FIP_DT_VN_ID) &&
		    !(desc_mask & 1U << desc->fip_dtype)) {
			LIBFCOE_FIP_DBG(fip, "Duplicate Critical "
					"Descriptors in FIP CVL\n");
			goto err;
		}
		switch (desc->fip_dtype) {
		case FIP_DT_MAC:
			mp = (struct fip_mac_desc *)desc;
			if (dlen < sizeof(*mp))
				goto err;
			if (compare_ether_addr(mp->fd_mac, fcf->fcf_mac))
				goto err;
			desc_mask &= ~BIT(FIP_DT_MAC);
			break;
		case FIP_DT_NAME:
			wp = (struct fip_wwn_desc *)desc;
			if (dlen < sizeof(*wp))
				goto err;
			if (get_unaligned_be64(&wp->fd_wwn) != fcf->switch_name)
				goto err;
			desc_mask &= ~BIT(FIP_DT_NAME);
			break;
		case FIP_DT_VN_ID:
			vp = (struct fip_vn_desc *)desc;
			if (dlen < sizeof(*vp))
				goto err;
			vlink_desc_arr[num_vlink_desc++] = vp;
			vn_port = fc_vport_id_lookup(lport,
						      ntoh24(vp->fd_fc_id));
			if (vn_port && (vn_port == lport)) {
				mutex_lock(&fip->ctlr_mutex);
				per_cpu_ptr(lport->dev_stats,
					    get_cpu())->VLinkFailureCount++;
				put_cpu();
				fcoe_ctlr_reset(fip);
				mutex_unlock(&fip->ctlr_mutex);
			}
			break;
		default:
			/* standard says ignore unknown descriptors >= 128 */
			if (desc->fip_dtype < FIP_DT_VENDOR_BASE)
				goto err;
			break;
		}
		desc = (struct fip_desc *)((char *)desc + dlen);
		rlen -= dlen;
	}

	/*
	 * reset only if all required descriptors were present and valid.
	 */
	if (desc_mask)
		LIBFCOE_FIP_DBG(fip, "missing descriptors mask %x\n",
				desc_mask);
	else if (!num_vlink_desc) {
		LIBFCOE_FIP_DBG(fip, "CVL: no Vx_Port descriptor found\n");
		/*
		 * No Vx_Port description. Clear all NPIV ports,
		 * followed by physical port
		 */
		mutex_lock(&fip->ctlr_mutex);
		per_cpu_ptr(lport->dev_stats,
			    get_cpu())->VLinkFailureCount++;
		put_cpu();
		fcoe_ctlr_reset(fip);
		mutex_unlock(&fip->ctlr_mutex);

		mutex_lock(&lport->lp_mutex);
		list_for_each_entry(vn_port, &lport->vports, list)
			fc_lport_reset(vn_port);
		mutex_unlock(&lport->lp_mutex);

		fc_lport_reset(fip->lp);
		fcoe_ctlr_solicit(fip, NULL);
	} else {
		int i;

		LIBFCOE_FIP_DBG(fip, "performing Clear Virtual Link\n");
		for (i = 0; i < num_vlink_desc; i++) {
			vp = vlink_desc_arr[i];
			vn_port = fc_vport_id_lookup(lport,
						     ntoh24(vp->fd_fc_id));
			if (!vn_port)
				continue;

			/*
			 * 'port_id' is already validated, check MAC address and
			 * wwpn
			 */
			if (compare_ether_addr(fip->get_src_addr(vn_port),
						vp->fd_mac) != 0 ||
				get_unaligned_be64(&vp->fd_wwpn) !=
							vn_port->wwpn)
				continue;

			if (vn_port == lport)
				/*
				 * Physical port, defer processing till all
				 * listed NPIV ports are cleared
				 */
				reset_phys_port = 1;
			else    /* NPIV port */
				fc_lport_reset(vn_port);
		}

		if (reset_phys_port) {
			fc_lport_reset(fip->lp);
			fcoe_ctlr_solicit(fip, NULL);
		}
	}

err:
	kfree(vlink_desc_arr);
}

/**
 * fcoe_ctlr_recv() - Receive a FIP packet
 * @fip: The FCoE controller that received the packet
 * @skb: The received FIP packet
 *
 * This may be called from either NET_RX_SOFTIRQ or IRQ.
 */
void fcoe_ctlr_recv(struct fcoe_ctlr *fip, struct sk_buff *skb)
{
	skb_queue_tail(&fip->fip_recv_list, skb);
	schedule_work(&fip->recv_work);
}
EXPORT_SYMBOL(fcoe_ctlr_recv);

/**
 * fcoe_ctlr_recv_handler() - Receive a FIP frame
 * @fip: The FCoE controller that received the frame
 * @skb: The received FIP frame
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
	if (fip->mode == FIP_MODE_VN2VN) {
		if (compare_ether_addr(eh->h_dest, fip->ctl_src_addr) &&
		    compare_ether_addr(eh->h_dest, fcoe_all_vn2vn) &&
		    compare_ether_addr(eh->h_dest, fcoe_all_p2p))
			goto drop;
	} else if (compare_ether_addr(eh->h_dest, fip->ctl_src_addr) &&
		   compare_ether_addr(eh->h_dest, fcoe_all_enode))
		goto drop;
	fiph = (struct fip_header *)skb->data;
	op = ntohs(fiph->fip_op);
	sub = fiph->fip_subcode;

	if (FIP_VER_DECAPS(fiph->fip_ver) != FIP_VER)
		goto drop;
	if (ntohs(fiph->fip_dl_len) * FIP_BPW + sizeof(*fiph) > skb->len)
		goto drop;

	mutex_lock(&fip->ctlr_mutex);
	state = fip->state;
	if (state == FIP_ST_AUTO) {
		fip->map_dest = 0;
		fcoe_ctlr_set_state(fip, FIP_ST_ENABLED);
		state = FIP_ST_ENABLED;
		LIBFCOE_FIP_DBG(fip, "Using FIP mode\n");
	}
	mutex_unlock(&fip->ctlr_mutex);

	if (fip->mode == FIP_MODE_VN2VN && op == FIP_OP_VN2VN)
		return fcoe_ctlr_vn_recv(fip, skb);

	if (state != FIP_ST_ENABLED && state != FIP_ST_VNMP_UP &&
	    state != FIP_ST_VNMP_CLAIM)
		goto drop;

	if (op == FIP_OP_LS) {
		fcoe_ctlr_recv_els(fip, skb);	/* consumes skb */
		return 0;
	}

	if (state != FIP_ST_ENABLED)
		goto drop;

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
 * fcoe_ctlr_select() - Select the best FCF (if possible)
 * @fip: The FCoE controller
 *
 * Returns the selected FCF, or NULL if none are usable.
 *
 * If there are conflicting advertisements, no FCF can be chosen.
 *
 * If there is already a selected FCF, this will choose a better one or
 * an equivalent one that hasn't already been sent a FLOGI.
 *
 * Called with lock held.
 */
static struct fcoe_fcf *fcoe_ctlr_select(struct fcoe_ctlr *fip)
{
	struct fcoe_fcf *fcf;
	struct fcoe_fcf *best = fip->sel_fcf;
	struct fcoe_fcf *first;

	first = list_first_entry(&fip->fcfs, struct fcoe_fcf, list);

	list_for_each_entry(fcf, &fip->fcfs, list) {
		LIBFCOE_FIP_DBG(fip, "consider FCF fab %16.16llx "
				"VFID %d mac %pM map %x val %d "
				"sent %u pri %u\n",
				fcf->fabric_name, fcf->vfid, fcf->fcf_mac,
				fcf->fc_map, fcoe_ctlr_mtu_valid(fcf),
				fcf->flogi_sent, fcf->pri);
		if (fcf->fabric_name != first->fabric_name ||
		    fcf->vfid != first->vfid ||
		    fcf->fc_map != first->fc_map) {
			LIBFCOE_FIP_DBG(fip, "Conflicting fabric, VFID, "
					"or FC-MAP\n");
			return NULL;
		}
		if (fcf->flogi_sent)
			continue;
		if (!fcoe_ctlr_fcf_usable(fcf)) {
			LIBFCOE_FIP_DBG(fip, "FCF for fab %16.16llx "
					"map %x %svalid %savailable\n",
					fcf->fabric_name, fcf->fc_map,
					(fcf->flags & FIP_FL_SOL) ? "" : "in",
					(fcf->flags & FIP_FL_AVAIL) ?
					"" : "un");
			continue;
		}
		if (!best || fcf->pri < best->pri || best->flogi_sent)
			best = fcf;
	}
	fip->sel_fcf = best;
	if (best) {
		LIBFCOE_FIP_DBG(fip, "using FCF mac %pM\n", best->fcf_mac);
		fip->port_ka_time = jiffies +
			msecs_to_jiffies(FIP_VN_KA_PERIOD);
		fip->ctlr_ka_time = jiffies + best->fka_period;
		if (time_before(fip->ctlr_ka_time, fip->timer.expires))
			mod_timer(&fip->timer, fip->ctlr_ka_time);
	}
	return best;
}

/**
 * fcoe_ctlr_flogi_send_locked() - send FIP-encapsulated FLOGI to current FCF
 * @fip: The FCoE controller
 *
 * Returns non-zero error if it could not be sent.
 *
 * Called with ctlr_mutex and ctlr_lock held.
 * Caller must verify that fip->sel_fcf is not NULL.
 */
static int fcoe_ctlr_flogi_send_locked(struct fcoe_ctlr *fip)
{
	struct sk_buff *skb;
	struct sk_buff *skb_orig;
	struct fc_frame_header *fh;
	int error;

	skb_orig = fip->flogi_req;
	if (!skb_orig)
		return -EINVAL;

	/*
	 * Clone and send the FLOGI request.  If clone fails, use original.
	 */
	skb = skb_clone(skb_orig, GFP_ATOMIC);
	if (!skb) {
		skb = skb_orig;
		fip->flogi_req = NULL;
	}
	fh = (struct fc_frame_header *)skb->data;
	error = fcoe_ctlr_encaps(fip, fip->lp, FIP_DT_FLOGI, skb,
				 ntoh24(fh->fh_d_id));
	if (error) {
		kfree_skb(skb);
		return error;
	}
	fip->send(fip, skb);
	fip->sel_fcf->flogi_sent = 1;
	return 0;
}

/**
 * fcoe_ctlr_flogi_retry() - resend FLOGI request to a new FCF if possible
 * @fip: The FCoE controller
 *
 * Returns non-zero error code if there's no FLOGI request to retry or
 * no alternate FCF available.
 */
static int fcoe_ctlr_flogi_retry(struct fcoe_ctlr *fip)
{
	struct fcoe_fcf *fcf;
	int error;

	mutex_lock(&fip->ctlr_mutex);
	spin_lock_bh(&fip->ctlr_lock);
	LIBFCOE_FIP_DBG(fip, "re-sending FLOGI - reselect\n");
	fcf = fcoe_ctlr_select(fip);
	if (!fcf || fcf->flogi_sent) {
		kfree_skb(fip->flogi_req);
		fip->flogi_req = NULL;
		error = -ENOENT;
	} else {
		fcoe_ctlr_solicit(fip, NULL);
		error = fcoe_ctlr_flogi_send_locked(fip);
	}
	spin_unlock_bh(&fip->ctlr_lock);
	mutex_unlock(&fip->ctlr_mutex);
	return error;
}


/**
 * fcoe_ctlr_flogi_send() - Handle sending of FIP FLOGI.
 * @fip: The FCoE controller that timed out
 *
 * Done here because fcoe_ctlr_els_send() can't get mutex.
 *
 * Called with ctlr_mutex held.  The caller must not hold ctlr_lock.
 */
static void fcoe_ctlr_flogi_send(struct fcoe_ctlr *fip)
{
	struct fcoe_fcf *fcf;

	spin_lock_bh(&fip->ctlr_lock);
	fcf = fip->sel_fcf;
	if (!fcf || !fip->flogi_req_send)
		goto unlock;

	LIBFCOE_FIP_DBG(fip, "sending FLOGI\n");

	/*
	 * If this FLOGI is being sent due to a timeout retry
	 * to the same FCF as before, select a different FCF if possible.
	 */
	if (fcf->flogi_sent) {
		LIBFCOE_FIP_DBG(fip, "sending FLOGI - reselect\n");
		fcf = fcoe_ctlr_select(fip);
		if (!fcf || fcf->flogi_sent) {
			LIBFCOE_FIP_DBG(fip, "sending FLOGI - clearing\n");
			list_for_each_entry(fcf, &fip->fcfs, list)
				fcf->flogi_sent = 0;
			fcf = fcoe_ctlr_select(fip);
		}
	}
	if (fcf) {
		fcoe_ctlr_flogi_send_locked(fip);
		fip->flogi_req_send = 0;
	} else /* XXX */
		LIBFCOE_FIP_DBG(fip, "No FCF selected - defer send\n");
unlock:
	spin_unlock_bh(&fip->ctlr_lock);
}

/**
 * fcoe_ctlr_timeout() - FIP timeout handler
 * @arg: The FCoE controller that timed out
 */
static void fcoe_ctlr_timeout(unsigned long arg)
{
	struct fcoe_ctlr *fip = (struct fcoe_ctlr *)arg;

	schedule_work(&fip->timer_work);
}

/**
 * fcoe_ctlr_timer_work() - Worker thread function for timer work
 * @work: Handle to a FCoE controller
 *
 * Ages FCFs.  Triggers FCF selection if possible.
 * Sends keep-alives and resets.
 */
static void fcoe_ctlr_timer_work(struct work_struct *work)
{
	struct fcoe_ctlr *fip;
	struct fc_lport *vport;
	u8 *mac;
	u8 reset = 0;
	u8 send_ctlr_ka = 0;
	u8 send_port_ka = 0;
	struct fcoe_fcf *sel;
	struct fcoe_fcf *fcf;
	unsigned long next_timer;

	fip = container_of(work, struct fcoe_ctlr, timer_work);
	if (fip->mode == FIP_MODE_VN2VN)
		return fcoe_ctlr_vn_timeout(fip);
	mutex_lock(&fip->ctlr_mutex);
	if (fip->state == FIP_ST_DISABLED) {
		mutex_unlock(&fip->ctlr_mutex);
		return;
	}

	fcf = fip->sel_fcf;
	next_timer = fcoe_ctlr_age_fcfs(fip);

	sel = fip->sel_fcf;
	if (!sel && fip->sel_time) {
		if (time_after_eq(jiffies, fip->sel_time)) {
			sel = fcoe_ctlr_select(fip);
			fip->sel_time = 0;
		} else if (time_after(next_timer, fip->sel_time))
			next_timer = fip->sel_time;
	}

	if (sel && fip->flogi_req_send)
		fcoe_ctlr_flogi_send(fip);
	else if (!sel && fcf)
		reset = 1;

	if (sel && !sel->fd_flags) {
		if (time_after_eq(jiffies, fip->ctlr_ka_time)) {
			fip->ctlr_ka_time = jiffies + sel->fka_period;
			send_ctlr_ka = 1;
		}
		if (time_after(next_timer, fip->ctlr_ka_time))
			next_timer = fip->ctlr_ka_time;

		if (time_after_eq(jiffies, fip->port_ka_time)) {
			fip->port_ka_time = jiffies +
				msecs_to_jiffies(FIP_VN_KA_PERIOD);
			send_port_ka = 1;
		}
		if (time_after(next_timer, fip->port_ka_time))
			next_timer = fip->port_ka_time;
	}
	if (!list_empty(&fip->fcfs))
		mod_timer(&fip->timer, next_timer);
	mutex_unlock(&fip->ctlr_mutex);

	if (reset) {
		fc_lport_reset(fip->lp);
		/* restart things with a solicitation */
		fcoe_ctlr_solicit(fip, NULL);
	}

	if (send_ctlr_ka)
		fcoe_ctlr_send_keep_alive(fip, NULL, 0, fip->ctl_src_addr);

	if (send_port_ka) {
		mutex_lock(&fip->lp->lp_mutex);
		mac = fip->get_src_addr(fip->lp);
		fcoe_ctlr_send_keep_alive(fip, fip->lp, 1, mac);
		list_for_each_entry(vport, &fip->lp->vports, list) {
			mac = fip->get_src_addr(vport);
			fcoe_ctlr_send_keep_alive(fip, vport, 1, mac);
		}
		mutex_unlock(&fip->lp->lp_mutex);
	}
}

/**
 * fcoe_ctlr_recv_work() - Worker thread function for receiving FIP frames
 * @recv_work: Handle to a FCoE controller
 */
static void fcoe_ctlr_recv_work(struct work_struct *recv_work)
{
	struct fcoe_ctlr *fip;
	struct sk_buff *skb;

	fip = container_of(recv_work, struct fcoe_ctlr, recv_work);
	while ((skb = skb_dequeue(&fip->fip_recv_list)))
		fcoe_ctlr_recv_handler(fip, skb);
}

/**
 * fcoe_ctlr_recv_flogi() - Snoop pre-FIP receipt of FLOGI response
 * @fip: The FCoE controller
 * @fp:	 The FC frame to snoop
 *
 * Snoop potential response to FLOGI or even incoming FLOGI.
 *
 * The caller has checked that we are waiting for login as indicated
 * by fip->flogi_oxid != FC_XID_UNKNOWN.
 *
 * The caller is responsible for freeing the frame.
 * Fill in the granted_mac address.
 *
 * Return non-zero if the frame should not be delivered to libfc.
 */
int fcoe_ctlr_recv_flogi(struct fcoe_ctlr *fip, struct fc_lport *lport,
			 struct fc_frame *fp)
{
	struct fc_frame_header *fh;
	u8 op;
	u8 *sa;

	sa = eth_hdr(&fp->skb)->h_source;
	fh = fc_frame_header_get(fp);
	if (fh->fh_type != FC_TYPE_ELS)
		return 0;

	op = fc_frame_payload_op(fp);
	if (op == ELS_LS_ACC && fh->fh_r_ctl == FC_RCTL_ELS_REP &&
	    fip->flogi_oxid == ntohs(fh->fh_ox_id)) {

		mutex_lock(&fip->ctlr_mutex);
		if (fip->state != FIP_ST_AUTO && fip->state != FIP_ST_NON_FIP) {
			mutex_unlock(&fip->ctlr_mutex);
			return -EINVAL;
		}
		fcoe_ctlr_set_state(fip, FIP_ST_NON_FIP);
		LIBFCOE_FIP_DBG(fip,
				"received FLOGI LS_ACC using non-FIP mode\n");

		/*
		 * FLOGI accepted.
		 * If the src mac addr is FC_OUI-based, then we mark the
		 * address_mode flag to use FC_OUI-based Ethernet DA.
		 * Otherwise we use the FCoE gateway addr
		 */
		if (!compare_ether_addr(sa, (u8[6])FC_FCOE_FLOGI_MAC)) {
			fcoe_ctlr_map_dest(fip);
		} else {
			memcpy(fip->dest_addr, sa, ETH_ALEN);
			fip->map_dest = 0;
		}
		fip->flogi_oxid = FC_XID_UNKNOWN;
		mutex_unlock(&fip->ctlr_mutex);
		fc_fcoe_set_mac(fr_cb(fp)->granted_mac, fh->fh_d_id);
	} else if (op == ELS_FLOGI && fh->fh_r_ctl == FC_RCTL_ELS_REQ && sa) {
		/*
		 * Save source MAC for point-to-point responses.
		 */
		mutex_lock(&fip->ctlr_mutex);
		if (fip->state == FIP_ST_AUTO || fip->state == FIP_ST_NON_FIP) {
			memcpy(fip->dest_addr, sa, ETH_ALEN);
			fip->map_dest = 0;
			if (fip->state == FIP_ST_AUTO)
				LIBFCOE_FIP_DBG(fip, "received non-FIP FLOGI. "
						"Setting non-FIP mode\n");
			fcoe_ctlr_set_state(fip, FIP_ST_NON_FIP);
		}
		mutex_unlock(&fip->ctlr_mutex);
	}
	return 0;
}
EXPORT_SYMBOL(fcoe_ctlr_recv_flogi);

/**
 * fcoe_wwn_from_mac() - Converts a 48-bit IEEE MAC address to a 64-bit FC WWN
 * @mac:    The MAC address to convert
 * @scheme: The scheme to use when converting
 * @port:   The port indicator for converting
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
 * fcoe_ctlr_rport() - return the fcoe_rport for a given fc_rport_priv
 * @rdata: libfc remote port
 */
static inline struct fcoe_rport *fcoe_ctlr_rport(struct fc_rport_priv *rdata)
{
	return (struct fcoe_rport *)(rdata + 1);
}

/**
 * fcoe_ctlr_vn_send() - Send a FIP VN2VN Probe Request or Reply.
 * @fip: The FCoE controller
 * @sub: sub-opcode for probe request, reply, or advertisement.
 * @dest: The destination Ethernet MAC address
 * @min_len: minimum size of the Ethernet payload to be sent
 */
static void fcoe_ctlr_vn_send(struct fcoe_ctlr *fip,
			      enum fip_vn2vn_subcode sub,
			      const u8 *dest, size_t min_len)
{
	struct sk_buff *skb;
	struct fip_frame {
		struct ethhdr eth;
		struct fip_header fip;
		struct fip_mac_desc mac;
		struct fip_wwn_desc wwnn;
		struct fip_vn_desc vn;
	} __packed * frame;
	struct fip_fc4_feat *ff;
	struct fip_size_desc *size;
	u32 fcp_feat;
	size_t len;
	size_t dlen;

	len = sizeof(*frame);
	dlen = 0;
	if (sub == FIP_SC_VN_CLAIM_NOTIFY || sub == FIP_SC_VN_CLAIM_REP) {
		dlen = sizeof(struct fip_fc4_feat) +
		       sizeof(struct fip_size_desc);
		len += dlen;
	}
	dlen += sizeof(frame->mac) + sizeof(frame->wwnn) + sizeof(frame->vn);
	len = max(len, min_len + sizeof(struct ethhdr));

	skb = dev_alloc_skb(len);
	if (!skb)
		return;

	frame = (struct fip_frame *)skb->data;
	memset(frame, 0, len);
	memcpy(frame->eth.h_dest, dest, ETH_ALEN);

	if (sub == FIP_SC_VN_BEACON) {
		hton24(frame->eth.h_source, FIP_VN_FC_MAP);
		hton24(frame->eth.h_source + 3, fip->port_id);
	} else {
		memcpy(frame->eth.h_source, fip->ctl_src_addr, ETH_ALEN);
	}
	frame->eth.h_proto = htons(ETH_P_FIP);

	frame->fip.fip_ver = FIP_VER_ENCAPS(FIP_VER);
	frame->fip.fip_op = htons(FIP_OP_VN2VN);
	frame->fip.fip_subcode = sub;
	frame->fip.fip_dl_len = htons(dlen / FIP_BPW);

	frame->mac.fd_desc.fip_dtype = FIP_DT_MAC;
	frame->mac.fd_desc.fip_dlen = sizeof(frame->mac) / FIP_BPW;
	memcpy(frame->mac.fd_mac, fip->ctl_src_addr, ETH_ALEN);

	frame->wwnn.fd_desc.fip_dtype = FIP_DT_NAME;
	frame->wwnn.fd_desc.fip_dlen = sizeof(frame->wwnn) / FIP_BPW;
	put_unaligned_be64(fip->lp->wwnn, &frame->wwnn.fd_wwn);

	frame->vn.fd_desc.fip_dtype = FIP_DT_VN_ID;
	frame->vn.fd_desc.fip_dlen = sizeof(frame->vn) / FIP_BPW;
	hton24(frame->vn.fd_mac, FIP_VN_FC_MAP);
	hton24(frame->vn.fd_mac + 3, fip->port_id);
	hton24(frame->vn.fd_fc_id, fip->port_id);
	put_unaligned_be64(fip->lp->wwpn, &frame->vn.fd_wwpn);

	/*
	 * For claims, add FC-4 features.
	 * TBD: Add interface to get fc-4 types and features from libfc.
	 */
	if (sub == FIP_SC_VN_CLAIM_NOTIFY || sub == FIP_SC_VN_CLAIM_REP) {
		ff = (struct fip_fc4_feat *)(frame + 1);
		ff->fd_desc.fip_dtype = FIP_DT_FC4F;
		ff->fd_desc.fip_dlen = sizeof(*ff) / FIP_BPW;
		ff->fd_fts = fip->lp->fcts;

		fcp_feat = 0;
		if (fip->lp->service_params & FCP_SPPF_INIT_FCN)
			fcp_feat |= FCP_FEAT_INIT;
		if (fip->lp->service_params & FCP_SPPF_TARG_FCN)
			fcp_feat |= FCP_FEAT_TARG;
		fcp_feat <<= (FC_TYPE_FCP * 4) % 32;
		ff->fd_ff.fd_feat[FC_TYPE_FCP * 4 / 32] = htonl(fcp_feat);

		size = (struct fip_size_desc *)(ff + 1);
		size->fd_desc.fip_dtype = FIP_DT_FCOE_SIZE;
		size->fd_desc.fip_dlen = sizeof(*size) / FIP_BPW;
		size->fd_size = htons(fcoe_ctlr_fcoe_size(fip));
	}

	skb_put(skb, len);
	skb->protocol = htons(ETH_P_FIP);
	skb->priority = fip->priority;
	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);

	fip->send(fip, skb);
}

/**
 * fcoe_ctlr_vn_rport_callback - Event handler for rport events.
 * @lport: The lport which is receiving the event
 * @rdata: remote port private data
 * @event: The event that occurred
 *
 * Locking Note:  The rport lock must not be held when calling this function.
 */
static void fcoe_ctlr_vn_rport_callback(struct fc_lport *lport,
					struct fc_rport_priv *rdata,
					enum fc_rport_event event)
{
	struct fcoe_ctlr *fip = lport->disc.priv;
	struct fcoe_rport *frport = fcoe_ctlr_rport(rdata);

	LIBFCOE_FIP_DBG(fip, "vn_rport_callback %x event %d\n",
			rdata->ids.port_id, event);

	mutex_lock(&fip->ctlr_mutex);
	switch (event) {
	case RPORT_EV_READY:
		frport->login_count = 0;
		break;
	case RPORT_EV_LOGO:
	case RPORT_EV_FAILED:
	case RPORT_EV_STOP:
		frport->login_count++;
		if (frport->login_count > FCOE_CTLR_VN2VN_LOGIN_LIMIT) {
			LIBFCOE_FIP_DBG(fip,
					"rport FLOGI limited port_id %6.6x\n",
					rdata->ids.port_id);
			lport->tt.rport_logoff(rdata);
		}
		break;
	default:
		break;
	}
	mutex_unlock(&fip->ctlr_mutex);
}

static struct fc_rport_operations fcoe_ctlr_vn_rport_ops = {
	.event_callback = fcoe_ctlr_vn_rport_callback,
};

/**
 * fcoe_ctlr_disc_stop_locked() - stop discovery in VN2VN mode
 * @fip: The FCoE controller
 *
 * Called with ctlr_mutex held.
 */
static void fcoe_ctlr_disc_stop_locked(struct fc_lport *lport)
{
	mutex_lock(&lport->disc.disc_mutex);
	lport->disc.disc_callback = NULL;
	mutex_unlock(&lport->disc.disc_mutex);
}

/**
 * fcoe_ctlr_disc_stop() - stop discovery in VN2VN mode
 * @fip: The FCoE controller
 *
 * Called through the local port template for discovery.
 * Called without the ctlr_mutex held.
 */
static void fcoe_ctlr_disc_stop(struct fc_lport *lport)
{
	struct fcoe_ctlr *fip = lport->disc.priv;

	mutex_lock(&fip->ctlr_mutex);
	fcoe_ctlr_disc_stop_locked(lport);
	mutex_unlock(&fip->ctlr_mutex);
}

/**
 * fcoe_ctlr_disc_stop_final() - stop discovery for shutdown in VN2VN mode
 * @fip: The FCoE controller
 *
 * Called through the local port template for discovery.
 * Called without the ctlr_mutex held.
 */
static void fcoe_ctlr_disc_stop_final(struct fc_lport *lport)
{
	fcoe_ctlr_disc_stop(lport);
	lport->tt.rport_flush_queue();
	synchronize_rcu();
}

/**
 * fcoe_ctlr_vn_restart() - VN2VN probe restart with new port_id
 * @fip: The FCoE controller
 *
 * Called with fcoe_ctlr lock held.
 */
static void fcoe_ctlr_vn_restart(struct fcoe_ctlr *fip)
{
	unsigned long wait;
	u32 port_id;

	fcoe_ctlr_disc_stop_locked(fip->lp);

	/*
	 * Get proposed port ID.
	 * If this is the first try after link up, use any previous port_id.
	 * If there was none, use the low bits of the port_name.
	 * On subsequent tries, get the next random one.
	 * Don't use reserved IDs, use another non-zero value, just as random.
	 */
	port_id = fip->port_id;
	if (fip->probe_tries)
		port_id = prandom32(&fip->rnd_state) & 0xffff;
	else if (!port_id)
		port_id = fip->lp->wwpn & 0xffff;
	if (!port_id || port_id == 0xffff)
		port_id = 1;
	fip->port_id = port_id;

	if (fip->probe_tries < FIP_VN_RLIM_COUNT) {
		fip->probe_tries++;
		wait = random32() % FIP_VN_PROBE_WAIT;
	} else
		wait = FIP_VN_RLIM_INT;
	mod_timer(&fip->timer, jiffies + msecs_to_jiffies(wait));
	fcoe_ctlr_set_state(fip, FIP_ST_VNMP_START);
}

/**
 * fcoe_ctlr_vn_start() - Start in VN2VN mode
 * @fip: The FCoE controller
 *
 * Called with fcoe_ctlr lock held.
 */
static void fcoe_ctlr_vn_start(struct fcoe_ctlr *fip)
{
	fip->probe_tries = 0;
	prandom32_seed(&fip->rnd_state, fip->lp->wwpn);
	fcoe_ctlr_vn_restart(fip);
}

/**
 * fcoe_ctlr_vn_parse - parse probe request or response
 * @fip: The FCoE controller
 * @skb: incoming packet
 * @rdata: buffer for resulting parsed VN entry plus fcoe_rport
 *
 * Returns non-zero error number on error.
 * Does not consume the packet.
 */
static int fcoe_ctlr_vn_parse(struct fcoe_ctlr *fip,
			      struct sk_buff *skb,
			      struct fc_rport_priv *rdata)
{
	struct fip_header *fiph;
	struct fip_desc *desc = NULL;
	struct fip_mac_desc *macd = NULL;
	struct fip_wwn_desc *wwn = NULL;
	struct fip_vn_desc *vn = NULL;
	struct fip_size_desc *size = NULL;
	struct fcoe_rport *frport;
	size_t rlen;
	size_t dlen;
	u32 desc_mask = 0;
	u32 dtype;
	u8 sub;

	memset(rdata, 0, sizeof(*rdata) + sizeof(*frport));
	frport = fcoe_ctlr_rport(rdata);

	fiph = (struct fip_header *)skb->data;
	frport->flags = ntohs(fiph->fip_flags);

	sub = fiph->fip_subcode;
	switch (sub) {
	case FIP_SC_VN_PROBE_REQ:
	case FIP_SC_VN_PROBE_REP:
	case FIP_SC_VN_BEACON:
		desc_mask = BIT(FIP_DT_MAC) | BIT(FIP_DT_NAME) |
			    BIT(FIP_DT_VN_ID);
		break;
	case FIP_SC_VN_CLAIM_NOTIFY:
	case FIP_SC_VN_CLAIM_REP:
		desc_mask = BIT(FIP_DT_MAC) | BIT(FIP_DT_NAME) |
			    BIT(FIP_DT_VN_ID) | BIT(FIP_DT_FC4F) |
			    BIT(FIP_DT_FCOE_SIZE);
		break;
	default:
		LIBFCOE_FIP_DBG(fip, "vn_parse unknown subcode %u\n", sub);
		return -EINVAL;
	}

	rlen = ntohs(fiph->fip_dl_len) * 4;
	if (rlen + sizeof(*fiph) > skb->len)
		return -EINVAL;

	desc = (struct fip_desc *)(fiph + 1);
	while (rlen > 0) {
		dlen = desc->fip_dlen * FIP_BPW;
		if (dlen < sizeof(*desc) || dlen > rlen)
			return -EINVAL;

		dtype = desc->fip_dtype;
		if (dtype < 32) {
			if (!(desc_mask & BIT(dtype))) {
				LIBFCOE_FIP_DBG(fip,
						"unexpected or duplicated desc "
						"desc type %u in "
						"FIP VN2VN subtype %u\n",
						dtype, sub);
				return -EINVAL;
			}
			desc_mask &= ~BIT(dtype);
		}

		switch (dtype) {
		case FIP_DT_MAC:
			if (dlen != sizeof(struct fip_mac_desc))
				goto len_err;
			macd = (struct fip_mac_desc *)desc;
			if (!is_valid_ether_addr(macd->fd_mac)) {
				LIBFCOE_FIP_DBG(fip,
					"Invalid MAC addr %pM in FIP VN2VN\n",
					 macd->fd_mac);
				return -EINVAL;
			}
			memcpy(frport->enode_mac, macd->fd_mac, ETH_ALEN);
			break;
		case FIP_DT_NAME:
			if (dlen != sizeof(struct fip_wwn_desc))
				goto len_err;
			wwn = (struct fip_wwn_desc *)desc;
			rdata->ids.node_name = get_unaligned_be64(&wwn->fd_wwn);
			break;
		case FIP_DT_VN_ID:
			if (dlen != sizeof(struct fip_vn_desc))
				goto len_err;
			vn = (struct fip_vn_desc *)desc;
			memcpy(frport->vn_mac, vn->fd_mac, ETH_ALEN);
			rdata->ids.port_id = ntoh24(vn->fd_fc_id);
			rdata->ids.port_name = get_unaligned_be64(&vn->fd_wwpn);
			break;
		case FIP_DT_FC4F:
			if (dlen != sizeof(struct fip_fc4_feat))
				goto len_err;
			break;
		case FIP_DT_FCOE_SIZE:
			if (dlen != sizeof(struct fip_size_desc))
				goto len_err;
			size = (struct fip_size_desc *)desc;
			frport->fcoe_len = ntohs(size->fd_size);
			break;
		default:
			LIBFCOE_FIP_DBG(fip, "unexpected descriptor type %x "
					"in FIP probe\n", dtype);
			/* standard says ignore unknown descriptors >= 128 */
			if (dtype < FIP_DT_VENDOR_BASE)
				return -EINVAL;
			break;
		}
		desc = (struct fip_desc *)((char *)desc + dlen);
		rlen -= dlen;
	}
	return 0;

len_err:
	LIBFCOE_FIP_DBG(fip, "FIP length error in descriptor type %x len %zu\n",
			dtype, dlen);
	return -EINVAL;
}

/**
 * fcoe_ctlr_vn_send_claim() - send multicast FIP VN2VN Claim Notification.
 * @fip: The FCoE controller
 *
 * Called with ctlr_mutex held.
 */
static void fcoe_ctlr_vn_send_claim(struct fcoe_ctlr *fip)
{
	fcoe_ctlr_vn_send(fip, FIP_SC_VN_CLAIM_NOTIFY, fcoe_all_vn2vn, 0);
	fip->sol_time = jiffies;
}

/**
 * fcoe_ctlr_vn_probe_req() - handle incoming VN2VN probe request.
 * @fip: The FCoE controller
 * @rdata: parsed remote port with frport from the probe request
 *
 * Called with ctlr_mutex held.
 */
static void fcoe_ctlr_vn_probe_req(struct fcoe_ctlr *fip,
				   struct fc_rport_priv *rdata)
{
	struct fcoe_rport *frport = fcoe_ctlr_rport(rdata);

	if (rdata->ids.port_id != fip->port_id)
		return;

	switch (fip->state) {
	case FIP_ST_VNMP_CLAIM:
	case FIP_ST_VNMP_UP:
		fcoe_ctlr_vn_send(fip, FIP_SC_VN_PROBE_REP,
				  frport->enode_mac, 0);
		break;
	case FIP_ST_VNMP_PROBE1:
	case FIP_ST_VNMP_PROBE2:
		/*
		 * Decide whether to reply to the Probe.
		 * Our selected address is never a "recorded" one, so
		 * only reply if our WWPN is greater and the
		 * Probe's REC bit is not set.
		 * If we don't reply, we will change our address.
		 */
		if (fip->lp->wwpn > rdata->ids.port_name &&
		    !(frport->flags & FIP_FL_REC_OR_P2P)) {
			fcoe_ctlr_vn_send(fip, FIP_SC_VN_PROBE_REP,
					  frport->enode_mac, 0);
			break;
		}
		/* fall through */
	case FIP_ST_VNMP_START:
		fcoe_ctlr_vn_restart(fip);
		break;
	default:
		break;
	}
}

/**
 * fcoe_ctlr_vn_probe_reply() - handle incoming VN2VN probe reply.
 * @fip: The FCoE controller
 * @rdata: parsed remote port with frport from the probe request
 *
 * Called with ctlr_mutex held.
 */
static void fcoe_ctlr_vn_probe_reply(struct fcoe_ctlr *fip,
				   struct fc_rport_priv *rdata)
{
	if (rdata->ids.port_id != fip->port_id)
		return;
	switch (fip->state) {
	case FIP_ST_VNMP_START:
	case FIP_ST_VNMP_PROBE1:
	case FIP_ST_VNMP_PROBE2:
	case FIP_ST_VNMP_CLAIM:
		fcoe_ctlr_vn_restart(fip);
		break;
	case FIP_ST_VNMP_UP:
		fcoe_ctlr_vn_send_claim(fip);
		break;
	default:
		break;
	}
}

/**
 * fcoe_ctlr_vn_add() - Add a VN2VN entry to the list, based on a claim reply.
 * @fip: The FCoE controller
 * @new: newly-parsed remote port with frport as a template for new rdata
 *
 * Called with ctlr_mutex held.
 */
static void fcoe_ctlr_vn_add(struct fcoe_ctlr *fip, struct fc_rport_priv *new)
{
	struct fc_lport *lport = fip->lp;
	struct fc_rport_priv *rdata;
	struct fc_rport_identifiers *ids;
	struct fcoe_rport *frport;
	u32 port_id;

	port_id = new->ids.port_id;
	if (port_id == fip->port_id)
		return;

	mutex_lock(&lport->disc.disc_mutex);
	rdata = lport->tt.rport_create(lport, port_id);
	if (!rdata) {
		mutex_unlock(&lport->disc.disc_mutex);
		return;
	}

	rdata->ops = &fcoe_ctlr_vn_rport_ops;
	rdata->disc_id = lport->disc.disc_id;

	ids = &rdata->ids;
	if ((ids->port_name != -1 && ids->port_name != new->ids.port_name) ||
	    (ids->node_name != -1 && ids->node_name != new->ids.node_name))
		lport->tt.rport_logoff(rdata);
	ids->port_name = new->ids.port_name;
	ids->node_name = new->ids.node_name;
	mutex_unlock(&lport->disc.disc_mutex);

	frport = fcoe_ctlr_rport(rdata);
	LIBFCOE_FIP_DBG(fip, "vn_add rport %6.6x %s\n",
			port_id, frport->fcoe_len ? "old" : "new");
	*frport = *fcoe_ctlr_rport(new);
	frport->time = 0;
}

/**
 * fcoe_ctlr_vn_lookup() - Find VN remote port's MAC address
 * @fip: The FCoE controller
 * @port_id:  The port_id of the remote VN_node
 * @mac: buffer which will hold the VN_NODE destination MAC address, if found.
 *
 * Returns non-zero error if no remote port found.
 */
static int fcoe_ctlr_vn_lookup(struct fcoe_ctlr *fip, u32 port_id, u8 *mac)
{
	struct fc_lport *lport = fip->lp;
	struct fc_rport_priv *rdata;
	struct fcoe_rport *frport;
	int ret = -1;

	rcu_read_lock();
	rdata = lport->tt.rport_lookup(lport, port_id);
	if (rdata) {
		frport = fcoe_ctlr_rport(rdata);
		memcpy(mac, frport->enode_mac, ETH_ALEN);
		ret = 0;
	}
	rcu_read_unlock();
	return ret;
}

/**
 * fcoe_ctlr_vn_claim_notify() - handle received FIP VN2VN Claim Notification
 * @fip: The FCoE controller
 * @new: newly-parsed remote port with frport as a template for new rdata
 *
 * Called with ctlr_mutex held.
 */
static void fcoe_ctlr_vn_claim_notify(struct fcoe_ctlr *fip,
				      struct fc_rport_priv *new)
{
	struct fcoe_rport *frport = fcoe_ctlr_rport(new);

	if (frport->flags & FIP_FL_REC_OR_P2P) {
		fcoe_ctlr_vn_send(fip, FIP_SC_VN_PROBE_REQ, fcoe_all_vn2vn, 0);
		return;
	}
	switch (fip->state) {
	case FIP_ST_VNMP_START:
	case FIP_ST_VNMP_PROBE1:
	case FIP_ST_VNMP_PROBE2:
		if (new->ids.port_id == fip->port_id)
			fcoe_ctlr_vn_restart(fip);
		break;
	case FIP_ST_VNMP_CLAIM:
	case FIP_ST_VNMP_UP:
		if (new->ids.port_id == fip->port_id) {
			if (new->ids.port_name > fip->lp->wwpn) {
				fcoe_ctlr_vn_restart(fip);
				break;
			}
			fcoe_ctlr_vn_send_claim(fip);
			break;
		}
		fcoe_ctlr_vn_send(fip, FIP_SC_VN_CLAIM_REP, frport->enode_mac,
				  min((u32)frport->fcoe_len,
				      fcoe_ctlr_fcoe_size(fip)));
		fcoe_ctlr_vn_add(fip, new);
		break;
	default:
		break;
	}
}

/**
 * fcoe_ctlr_vn_claim_resp() - handle received Claim Response
 * @fip: The FCoE controller that received the frame
 * @new: newly-parsed remote port with frport from the Claim Response
 *
 * Called with ctlr_mutex held.
 */
static void fcoe_ctlr_vn_claim_resp(struct fcoe_ctlr *fip,
				    struct fc_rport_priv *new)
{
	LIBFCOE_FIP_DBG(fip, "claim resp from from rport %x - state %s\n",
			new->ids.port_id, fcoe_ctlr_state(fip->state));
	if (fip->state == FIP_ST_VNMP_UP || fip->state == FIP_ST_VNMP_CLAIM)
		fcoe_ctlr_vn_add(fip, new);
}

/**
 * fcoe_ctlr_vn_beacon() - handle received beacon.
 * @fip: The FCoE controller that received the frame
 * @new: newly-parsed remote port with frport from the Beacon
 *
 * Called with ctlr_mutex held.
 */
static void fcoe_ctlr_vn_beacon(struct fcoe_ctlr *fip,
				struct fc_rport_priv *new)
{
	struct fc_lport *lport = fip->lp;
	struct fc_rport_priv *rdata;
	struct fcoe_rport *frport;

	frport = fcoe_ctlr_rport(new);
	if (frport->flags & FIP_FL_REC_OR_P2P) {
		fcoe_ctlr_vn_send(fip, FIP_SC_VN_PROBE_REQ, fcoe_all_vn2vn, 0);
		return;
	}
	mutex_lock(&lport->disc.disc_mutex);
	rdata = lport->tt.rport_lookup(lport, new->ids.port_id);
	if (rdata)
		kref_get(&rdata->kref);
	mutex_unlock(&lport->disc.disc_mutex);
	if (rdata) {
		if (rdata->ids.node_name == new->ids.node_name &&
		    rdata->ids.port_name == new->ids.port_name) {
			frport = fcoe_ctlr_rport(rdata);
			if (!frport->time && fip->state == FIP_ST_VNMP_UP)
				lport->tt.rport_login(rdata);
			frport->time = jiffies;
		}
		kref_put(&rdata->kref, lport->tt.rport_destroy);
		return;
	}
	if (fip->state != FIP_ST_VNMP_UP)
		return;

	/*
	 * Beacon from a new neighbor.
	 * Send a claim notify if one hasn't been sent recently.
	 * Don't add the neighbor yet.
	 */
	LIBFCOE_FIP_DBG(fip, "beacon from new rport %x. sending claim notify\n",
			new->ids.port_id);
	if (time_after(jiffies,
		       fip->sol_time + msecs_to_jiffies(FIP_VN_ANN_WAIT)))
		fcoe_ctlr_vn_send_claim(fip);
}

/**
 * fcoe_ctlr_vn_age() - Check for VN_ports without recent beacons
 * @fip: The FCoE controller
 *
 * Called with ctlr_mutex held.
 * Called only in state FIP_ST_VNMP_UP.
 * Returns the soonest time for next age-out or a time far in the future.
 */
static unsigned long fcoe_ctlr_vn_age(struct fcoe_ctlr *fip)
{
	struct fc_lport *lport = fip->lp;
	struct fc_rport_priv *rdata;
	struct fcoe_rport *frport;
	unsigned long next_time;
	unsigned long deadline;

	next_time = jiffies + msecs_to_jiffies(FIP_VN_BEACON_INT * 10);
	mutex_lock(&lport->disc.disc_mutex);
	list_for_each_entry_rcu(rdata, &lport->disc.rports, peers) {
		frport = fcoe_ctlr_rport(rdata);
		if (!frport->time)
			continue;
		deadline = frport->time +
			   msecs_to_jiffies(FIP_VN_BEACON_INT * 25 / 10);
		if (time_after_eq(jiffies, deadline)) {
			frport->time = 0;
			LIBFCOE_FIP_DBG(fip,
				"port %16.16llx fc_id %6.6x beacon expired\n",
				rdata->ids.port_name, rdata->ids.port_id);
			lport->tt.rport_logoff(rdata);
		} else if (time_before(deadline, next_time))
			next_time = deadline;
	}
	mutex_unlock(&lport->disc.disc_mutex);
	return next_time;
}

/**
 * fcoe_ctlr_vn_recv() - Receive a FIP frame
 * @fip: The FCoE controller that received the frame
 * @skb: The received FIP frame
 *
 * Returns non-zero if the frame is dropped.
 * Always consumes the frame.
 */
static int fcoe_ctlr_vn_recv(struct fcoe_ctlr *fip, struct sk_buff *skb)
{
	struct fip_header *fiph;
	enum fip_vn2vn_subcode sub;
	struct {
		struct fc_rport_priv rdata;
		struct fcoe_rport frport;
	} buf;
	int rc;

	fiph = (struct fip_header *)skb->data;
	sub = fiph->fip_subcode;

	rc = fcoe_ctlr_vn_parse(fip, skb, &buf.rdata);
	if (rc) {
		LIBFCOE_FIP_DBG(fip, "vn_recv vn_parse error %d\n", rc);
		goto drop;
	}

	mutex_lock(&fip->ctlr_mutex);
	switch (sub) {
	case FIP_SC_VN_PROBE_REQ:
		fcoe_ctlr_vn_probe_req(fip, &buf.rdata);
		break;
	case FIP_SC_VN_PROBE_REP:
		fcoe_ctlr_vn_probe_reply(fip, &buf.rdata);
		break;
	case FIP_SC_VN_CLAIM_NOTIFY:
		fcoe_ctlr_vn_claim_notify(fip, &buf.rdata);
		break;
	case FIP_SC_VN_CLAIM_REP:
		fcoe_ctlr_vn_claim_resp(fip, &buf.rdata);
		break;
	case FIP_SC_VN_BEACON:
		fcoe_ctlr_vn_beacon(fip, &buf.rdata);
		break;
	default:
		LIBFCOE_FIP_DBG(fip, "vn_recv unknown subcode %d\n", sub);
		rc = -1;
		break;
	}
	mutex_unlock(&fip->ctlr_mutex);
drop:
	kfree_skb(skb);
	return rc;
}

/**
 * fcoe_ctlr_disc_recv - discovery receive handler for VN2VN mode.
 * @lport: The local port
 * @fp: The received frame
 *
 * This should never be called since we don't see RSCNs or other
 * fabric-generated ELSes.
 */
static void fcoe_ctlr_disc_recv(struct fc_lport *lport, struct fc_frame *fp)
{
	struct fc_seq_els_data rjt_data;

	rjt_data.reason = ELS_RJT_UNSUP;
	rjt_data.explan = ELS_EXPL_NONE;
	lport->tt.seq_els_rsp_send(fp, ELS_LS_RJT, &rjt_data);
	fc_frame_free(fp);
}

/**
 * fcoe_ctlr_disc_recv - start discovery for VN2VN mode.
 * @fip: The FCoE controller
 *
 * This sets a flag indicating that remote ports should be created
 * and started for the peers we discover.  We use the disc_callback
 * pointer as that flag.  Peers already discovered are created here.
 *
 * The lport lock is held during this call. The callback must be done
 * later, without holding either the lport or discovery locks.
 * The fcoe_ctlr lock may also be held during this call.
 */
static void fcoe_ctlr_disc_start(void (*callback)(struct fc_lport *,
						  enum fc_disc_event),
				 struct fc_lport *lport)
{
	struct fc_disc *disc = &lport->disc;
	struct fcoe_ctlr *fip = disc->priv;

	mutex_lock(&disc->disc_mutex);
	disc->disc_callback = callback;
	disc->disc_id = (disc->disc_id + 2) | 1;
	disc->pending = 1;
	schedule_work(&fip->timer_work);
	mutex_unlock(&disc->disc_mutex);
}

/**
 * fcoe_ctlr_vn_disc() - report FIP VN_port discovery results after claim state.
 * @fip: The FCoE controller
 *
 * Starts the FLOGI and PLOGI login process to each discovered rport for which
 * we've received at least one beacon.
 * Performs the discovery complete callback.
 */
static void fcoe_ctlr_vn_disc(struct fcoe_ctlr *fip)
{
	struct fc_lport *lport = fip->lp;
	struct fc_disc *disc = &lport->disc;
	struct fc_rport_priv *rdata;
	struct fcoe_rport *frport;
	void (*callback)(struct fc_lport *, enum fc_disc_event);

	mutex_lock(&disc->disc_mutex);
	callback = disc->pending ? disc->disc_callback : NULL;
	disc->pending = 0;
	list_for_each_entry_rcu(rdata, &disc->rports, peers) {
		frport = fcoe_ctlr_rport(rdata);
		if (frport->time)
			lport->tt.rport_login(rdata);
	}
	mutex_unlock(&disc->disc_mutex);
	if (callback)
		callback(lport, DISC_EV_SUCCESS);
}

/**
 * fcoe_ctlr_vn_timeout - timer work function for VN2VN mode.
 * @fip: The FCoE controller
 */
static void fcoe_ctlr_vn_timeout(struct fcoe_ctlr *fip)
{
	unsigned long next_time;
	u8 mac[ETH_ALEN];
	u32 new_port_id = 0;

	mutex_lock(&fip->ctlr_mutex);
	switch (fip->state) {
	case FIP_ST_VNMP_START:
		fcoe_ctlr_set_state(fip, FIP_ST_VNMP_PROBE1);
		fcoe_ctlr_vn_send(fip, FIP_SC_VN_PROBE_REQ, fcoe_all_vn2vn, 0);
		next_time = jiffies + msecs_to_jiffies(FIP_VN_PROBE_WAIT);
		break;
	case FIP_ST_VNMP_PROBE1:
		fcoe_ctlr_set_state(fip, FIP_ST_VNMP_PROBE2);
		fcoe_ctlr_vn_send(fip, FIP_SC_VN_PROBE_REQ, fcoe_all_vn2vn, 0);
		next_time = jiffies + msecs_to_jiffies(FIP_VN_ANN_WAIT);
		break;
	case FIP_ST_VNMP_PROBE2:
		fcoe_ctlr_set_state(fip, FIP_ST_VNMP_CLAIM);
		new_port_id = fip->port_id;
		hton24(mac, FIP_VN_FC_MAP);
		hton24(mac + 3, new_port_id);
		fcoe_ctlr_map_dest(fip);
		fip->update_mac(fip->lp, mac);
		fcoe_ctlr_vn_send_claim(fip);
		next_time = jiffies + msecs_to_jiffies(FIP_VN_ANN_WAIT);
		break;
	case FIP_ST_VNMP_CLAIM:
		/*
		 * This may be invoked either by starting discovery so don't
		 * go to the next state unless it's been long enough.
		 */
		next_time = fip->sol_time + msecs_to_jiffies(FIP_VN_ANN_WAIT);
		if (time_after_eq(jiffies, next_time)) {
			fcoe_ctlr_set_state(fip, FIP_ST_VNMP_UP);
			fcoe_ctlr_vn_send(fip, FIP_SC_VN_BEACON,
					  fcoe_all_vn2vn, 0);
			next_time = jiffies + msecs_to_jiffies(FIP_VN_ANN_WAIT);
			fip->port_ka_time = next_time;
		}
		fcoe_ctlr_vn_disc(fip);
		break;
	case FIP_ST_VNMP_UP:
		next_time = fcoe_ctlr_vn_age(fip);
		if (time_after_eq(jiffies, fip->port_ka_time)) {
			fcoe_ctlr_vn_send(fip, FIP_SC_VN_BEACON,
					  fcoe_all_vn2vn, 0);
			fip->port_ka_time = jiffies +
				 msecs_to_jiffies(FIP_VN_BEACON_INT +
					(random32() % FIP_VN_BEACON_FUZZ));
		}
		if (time_before(fip->port_ka_time, next_time))
			next_time = fip->port_ka_time;
		break;
	case FIP_ST_LINK_WAIT:
		goto unlock;
	default:
		WARN(1, "unexpected state %d\n", fip->state);
		goto unlock;
	}
	mod_timer(&fip->timer, next_time);
unlock:
	mutex_unlock(&fip->ctlr_mutex);

	/* If port ID is new, notify local port after dropping ctlr_mutex */
	if (new_port_id)
		fc_lport_set_local_id(fip->lp, new_port_id);
}

/**
 * fcoe_libfc_config() - Sets up libfc related properties for local port
 * @lport:    The local port to configure libfc for
 * @fip:      The FCoE controller in use by the local port
 * @tt:       The libfc function template
 * @init_fcp: If non-zero, the FCP portion of libfc should be initialized
 *
 * Returns : 0 for success
 */
int fcoe_libfc_config(struct fc_lport *lport, struct fcoe_ctlr *fip,
		      const struct libfc_function_template *tt, int init_fcp)
{
	/* Set the function pointers set by the LLDD */
	memcpy(&lport->tt, tt, sizeof(*tt));
	if (init_fcp && fc_fcp_init(lport))
		return -ENOMEM;
	fc_exch_init(lport);
	fc_elsct_init(lport);
	fc_lport_init(lport);
	if (fip->mode == FIP_MODE_VN2VN)
		lport->rport_priv_size = sizeof(struct fcoe_rport);
	fc_rport_init(lport);
	if (fip->mode == FIP_MODE_VN2VN) {
		lport->point_to_multipoint = 1;
		lport->tt.disc_recv_req = fcoe_ctlr_disc_recv;
		lport->tt.disc_start = fcoe_ctlr_disc_start;
		lport->tt.disc_stop = fcoe_ctlr_disc_stop;
		lport->tt.disc_stop_final = fcoe_ctlr_disc_stop_final;
		mutex_init(&lport->disc.disc_mutex);
		INIT_LIST_HEAD(&lport->disc.rports);
		lport->disc.priv = fip;
	} else {
		fc_disc_init(lport);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(fcoe_libfc_config);

void fcoe_fcf_get_selected(struct fcoe_fcf_device *fcf_dev)
{
	struct fcoe_ctlr_device *ctlr_dev = fcoe_fcf_dev_to_ctlr_dev(fcf_dev);
	struct fcoe_ctlr *fip = fcoe_ctlr_device_priv(ctlr_dev);
	struct fcoe_fcf *fcf;

	mutex_lock(&fip->ctlr_mutex);
	mutex_lock(&ctlr_dev->lock);

	fcf = fcoe_fcf_device_priv(fcf_dev);
	if (fcf)
		fcf_dev->selected = (fcf == fip->sel_fcf) ? 1 : 0;
	else
		fcf_dev->selected = 0;

	mutex_unlock(&ctlr_dev->lock);
	mutex_unlock(&fip->ctlr_mutex);
}
EXPORT_SYMBOL(fcoe_fcf_get_selected);

void fcoe_ctlr_get_fip_mode(struct fcoe_ctlr_device *ctlr_dev)
{
	struct fcoe_ctlr *ctlr = fcoe_ctlr_device_priv(ctlr_dev);

	mutex_lock(&ctlr->ctlr_mutex);
	switch (ctlr->mode) {
	case FIP_MODE_FABRIC:
		ctlr_dev->mode = FIP_CONN_TYPE_FABRIC;
		break;
	case FIP_MODE_VN2VN:
		ctlr_dev->mode = FIP_CONN_TYPE_VN2VN;
		break;
	default:
		ctlr_dev->mode = FIP_CONN_TYPE_UNKNOWN;
		break;
	}
	mutex_unlock(&ctlr->ctlr_mutex);
}
EXPORT_SYMBOL(fcoe_ctlr_get_fip_mode);
