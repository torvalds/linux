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
#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
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
#include <scsi/fc/fc_fip.h>

#include <scsi/libfc.h>
#include <scsi/fc_frame.h>
#include <scsi/libfcoe.h>

#include "fcoe.h"

static int debug_fcoe;

MODULE_AUTHOR("Open-FCoE.org");
MODULE_DESCRIPTION("FCoE");
MODULE_LICENSE("GPL v2");

/* fcoe host list */
LIST_HEAD(fcoe_hostlist);
DEFINE_RWLOCK(fcoe_hostlist_lock);
DEFINE_TIMER(fcoe_timer, NULL, 0, 0);
DEFINE_PER_CPU(struct fcoe_percpu_s, fcoe_percpu);

/* Function Prototyes */
static int fcoe_reset(struct Scsi_Host *shost);
static int fcoe_xmit(struct fc_lport *, struct fc_frame *);
static int fcoe_rcv(struct sk_buff *, struct net_device *,
		    struct packet_type *, struct net_device *);
static int fcoe_percpu_receive_thread(void *arg);
static void fcoe_clean_pending_queue(struct fc_lport *lp);
static void fcoe_percpu_clean(struct fc_lport *lp);
static int fcoe_link_ok(struct fc_lport *lp);

static struct fc_lport *fcoe_hostlist_lookup(const struct net_device *);
static int fcoe_hostlist_add(const struct fc_lport *);
static int fcoe_hostlist_remove(const struct fc_lport *);

static int fcoe_check_wait_queue(struct fc_lport *);
static int fcoe_device_notification(struct notifier_block *, ulong, void *);
static void fcoe_dev_setup(void);
static void fcoe_dev_cleanup(void);

/* notification function from net device */
static struct notifier_block fcoe_notifier = {
	.notifier_call = fcoe_device_notification,
};

static struct scsi_transport_template *scsi_transport_fcoe_sw;

struct fc_function_template fcoe_transport_function = {
	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.show_host_supported_classes = 1,
	.show_host_supported_fc4s = 1,
	.show_host_active_fc4s = 1,
	.show_host_maxframe_size = 1,

	.show_host_port_id = 1,
	.show_host_supported_speeds = 1,
	.get_host_speed = fc_get_host_speed,
	.show_host_speed = 1,
	.show_host_port_type = 1,
	.get_host_port_state = fc_get_host_port_state,
	.show_host_port_state = 1,
	.show_host_symbolic_name = 1,

	.dd_fcrport_size = sizeof(struct fc_rport_libfc_priv),
	.show_rport_maxframe_size = 1,
	.show_rport_supported_classes = 1,

	.show_host_fabric_name = 1,
	.show_starget_node_name = 1,
	.show_starget_port_name = 1,
	.show_starget_port_id = 1,
	.set_rport_dev_loss_tmo = fc_set_rport_loss_tmo,
	.show_rport_dev_loss_tmo = 1,
	.get_fc_host_stats = fc_get_host_stats,
	.issue_fc_host_lip = fcoe_reset,

	.terminate_rport_io = fc_rport_terminate_io,
};

static struct scsi_host_template fcoe_shost_template = {
	.module = THIS_MODULE,
	.name = "FCoE Driver",
	.proc_name = FCOE_NAME,
	.queuecommand = fc_queuecommand,
	.eh_abort_handler = fc_eh_abort,
	.eh_device_reset_handler = fc_eh_device_reset,
	.eh_host_reset_handler = fc_eh_host_reset,
	.slave_alloc = fc_slave_alloc,
	.change_queue_depth = fc_change_queue_depth,
	.change_queue_type = fc_change_queue_type,
	.this_id = -1,
	.cmd_per_lun = 32,
	.can_queue = FCOE_MAX_OUTSTANDING_COMMANDS,
	.use_clustering = ENABLE_CLUSTERING,
	.sg_tablesize = SG_ALL,
	.max_sectors = 0xffff,
};

/**
 * fcoe_lport_config() - sets up the fc_lport
 * @lp: ptr to the fc_lport
 * @shost: ptr to the parent scsi host
 *
 * Returns: 0 for success
 */
static int fcoe_lport_config(struct fc_lport *lp)
{
	lp->link_up = 0;
	lp->qfull = 0;
	lp->max_retry_count = 3;
	lp->e_d_tov = 2 * 1000;	/* FC-FS default */
	lp->r_a_tov = 2 * 2 * 1000;
	lp->service_params = (FCP_SPPF_INIT_FCN | FCP_SPPF_RD_XRDY_DIS |
			      FCP_SPPF_RETRY | FCP_SPPF_CONF_COMPL);

	fc_lport_init_stats(lp);

	/* lport fc_lport related configuration */
	fc_lport_config(lp);

	/* offload related configuration */
	lp->crc_offload = 0;
	lp->seq_offload = 0;
	lp->lro_enabled = 0;
	lp->lro_xid = 0;
	lp->lso_max = 0;

	return 0;
}

/**
 * fcoe_netdev_config() - Set up netdev for SW FCoE
 * @lp : ptr to the fc_lport
 * @netdev : ptr to the associated netdevice struct
 *
 * Must be called after fcoe_lport_config() as it will use lport mutex
 *
 * Returns : 0 for success
 */
static int fcoe_netdev_config(struct fc_lport *lp, struct net_device *netdev)
{
	u32 mfs;
	u64 wwnn, wwpn;
	struct fcoe_softc *fc;
	u8 flogi_maddr[ETH_ALEN];

	/* Setup lport private data to point to fcoe softc */
	fc = lport_priv(lp);
	fc->ctlr.lp = lp;
	fc->real_dev = netdev;
	fc->phys_dev = netdev;

	/* Require support for get_pauseparam ethtool op. */
	if (netdev->priv_flags & IFF_802_1Q_VLAN)
		fc->phys_dev = vlan_dev_real_dev(netdev);

	/* Do not support for bonding device */
	if ((fc->real_dev->priv_flags & IFF_MASTER_ALB) ||
	    (fc->real_dev->priv_flags & IFF_SLAVE_INACTIVE) ||
	    (fc->real_dev->priv_flags & IFF_MASTER_8023AD)) {
		return -EOPNOTSUPP;
	}

	/*
	 * Determine max frame size based on underlying device and optional
	 * user-configured limit.  If the MFS is too low, fcoe_link_ok()
	 * will return 0, so do this first.
	 */
	mfs = fc->real_dev->mtu - (sizeof(struct fcoe_hdr) +
				   sizeof(struct fcoe_crc_eof));
	if (fc_set_mfs(lp, mfs))
		return -EINVAL;

	/* offload features support */
	if (fc->real_dev->features & NETIF_F_SG)
		lp->sg_supp = 1;

#ifdef NETIF_F_FCOE_CRC
	if (netdev->features & NETIF_F_FCOE_CRC) {
		lp->crc_offload = 1;
		printk(KERN_DEBUG "fcoe:%s supports FCCRC offload\n",
		       netdev->name);
	}
#endif
#ifdef NETIF_F_FSO
	if (netdev->features & NETIF_F_FSO) {
		lp->seq_offload = 1;
		lp->lso_max = netdev->gso_max_size;
		printk(KERN_DEBUG "fcoe:%s supports LSO for max len 0x%x\n",
		       netdev->name, lp->lso_max);
	}
#endif
	if (netdev->fcoe_ddp_xid) {
		lp->lro_enabled = 1;
		lp->lro_xid = netdev->fcoe_ddp_xid;
		printk(KERN_DEBUG "fcoe:%s supports LRO for max xid 0x%x\n",
		       netdev->name, lp->lro_xid);
	}
	skb_queue_head_init(&fc->fcoe_pending_queue);
	fc->fcoe_pending_queue_active = 0;

	/* setup Source Mac Address */
	memcpy(fc->ctlr.ctl_src_addr, fc->real_dev->dev_addr,
	       fc->real_dev->addr_len);

	wwnn = fcoe_wwn_from_mac(fc->real_dev->dev_addr, 1, 0);
	fc_set_wwnn(lp, wwnn);
	/* XXX - 3rd arg needs to be vlan id */
	wwpn = fcoe_wwn_from_mac(fc->real_dev->dev_addr, 2, 0);
	fc_set_wwpn(lp, wwpn);

	/*
	 * Add FCoE MAC address as second unicast MAC address
	 * or enter promiscuous mode if not capable of listening
	 * for multiple unicast MACs.
	 */
	rtnl_lock();
	memcpy(flogi_maddr, (u8[6]) FC_FCOE_FLOGI_MAC, ETH_ALEN);
	dev_unicast_add(fc->real_dev, flogi_maddr, ETH_ALEN);
	rtnl_unlock();

	/*
	 * setup the receive function from ethernet driver
	 * on the ethertype for the given device
	 */
	fc->fcoe_packet_type.func = fcoe_rcv;
	fc->fcoe_packet_type.type = __constant_htons(ETH_P_FCOE);
	fc->fcoe_packet_type.dev = fc->real_dev;
	dev_add_pack(&fc->fcoe_packet_type);

	return 0;
}

/**
 * fcoe_shost_config() - Sets up fc_lport->host
 * @lp : ptr to the fc_lport
 * @shost : ptr to the associated scsi host
 * @dev : device associated to scsi host
 *
 * Must be called after fcoe_lport_config() and fcoe_netdev_config()
 *
 * Returns : 0 for success
 */
static int fcoe_shost_config(struct fc_lport *lp, struct Scsi_Host *shost,
				struct device *dev)
{
	int rc = 0;

	/* lport scsi host config */
	lp->host = shost;

	lp->host->max_lun = FCOE_MAX_LUN;
	lp->host->max_id = FCOE_MAX_FCP_TARGET;
	lp->host->max_channel = 0;
	lp->host->transportt = scsi_transport_fcoe_sw;

	/* add the new host to the SCSI-ml */
	rc = scsi_add_host(lp->host, dev);
	if (rc) {
		FC_DBG("fcoe_shost_config:error on scsi_add_host\n");
		return rc;
	}
	sprintf(fc_host_symbolic_name(lp->host), "%s v%s over %s",
		FCOE_NAME, FCOE_VERSION,
		fcoe_netdev(lp)->name);

	return 0;
}

/**
 * fcoe_em_config() - allocates em for this lport
 * @lp: the port that em is to allocated for
 *
 * Returns : 0 on success
 */
static inline int fcoe_em_config(struct fc_lport *lp)
{
	BUG_ON(lp->emp);

	lp->emp = fc_exch_mgr_alloc(lp, FC_CLASS_3,
				    FCOE_MIN_XID, FCOE_MAX_XID);
	if (!lp->emp)
		return -ENOMEM;

	return 0;
}

/**
 * fcoe_if_destroy() - FCoE software HBA tear-down function
 * @netdev: ptr to the associated net_device
 *
 * Returns: 0 if link is OK for use by FCoE.
 */
static int fcoe_if_destroy(struct net_device *netdev)
{
	struct fc_lport *lp = NULL;
	struct fcoe_softc *fc;
	u8 flogi_maddr[ETH_ALEN];

	BUG_ON(!netdev);

	printk(KERN_DEBUG "fcoe_if_destroy:interface on %s\n",
	       netdev->name);

	lp = fcoe_hostlist_lookup(netdev);
	if (!lp)
		return -ENODEV;

	fc = lport_priv(lp);

	/* Logout of the fabric */
	fc_fabric_logoff(lp);

	/* Remove the instance from fcoe's list */
	fcoe_hostlist_remove(lp);

	/* Don't listen for Ethernet packets anymore */
	dev_remove_pack(&fc->fcoe_packet_type);
	dev_remove_pack(&fc->fip_packet_type);
	fcoe_ctlr_destroy(&fc->ctlr);

	/* Cleanup the fc_lport */
	fc_lport_destroy(lp);
	fc_fcp_destroy(lp);

	/* Detach from the scsi-ml */
	fc_remove_host(lp->host);
	scsi_remove_host(lp->host);

	/* There are no more rports or I/O, free the EM */
	if (lp->emp)
		fc_exch_mgr_free(lp->emp);

	/* Delete secondary MAC addresses */
	rtnl_lock();
	memcpy(flogi_maddr, (u8[6]) FC_FCOE_FLOGI_MAC, ETH_ALEN);
	dev_unicast_delete(fc->real_dev, flogi_maddr, ETH_ALEN);
	if (!is_zero_ether_addr(fc->ctlr.data_src_addr))
		dev_unicast_delete(fc->real_dev,
				   fc->ctlr.data_src_addr, ETH_ALEN);
	dev_mc_delete(fc->real_dev, FIP_ALL_ENODE_MACS, ETH_ALEN, 0);
	rtnl_unlock();

	/* Free the per-CPU revieve threads */
	fcoe_percpu_clean(lp);

	/* Free existing skbs */
	fcoe_clean_pending_queue(lp);

	/* Free memory used by statistical counters */
	fc_lport_free_stats(lp);

	/* Release the net_device and Scsi_Host */
	dev_put(fc->real_dev);
	scsi_host_put(lp->host);

	return 0;
}

/*
 * fcoe_ddp_setup - calls LLD's ddp_setup through net_device
 * @lp:	the corresponding fc_lport
 * @xid: the exchange id for this ddp transfer
 * @sgl: the scatterlist describing this transfer
 * @sgc: number of sg items
 *
 * Returns : 0 no ddp
 */
static int fcoe_ddp_setup(struct fc_lport *lp, u16 xid,
			     struct scatterlist *sgl, unsigned int sgc)
{
	struct net_device *n = fcoe_netdev(lp);

	if (n->netdev_ops && n->netdev_ops->ndo_fcoe_ddp_setup)
		return n->netdev_ops->ndo_fcoe_ddp_setup(n, xid, sgl, sgc);

	return 0;
}

/*
 * fcoe_ddp_done - calls LLD's ddp_done through net_device
 * @lp:	the corresponding fc_lport
 * @xid: the exchange id for this ddp transfer
 *
 * Returns : the length of data that have been completed by ddp
 */
static int fcoe_ddp_done(struct fc_lport *lp, u16 xid)
{
	struct net_device *n = fcoe_netdev(lp);

	if (n->netdev_ops && n->netdev_ops->ndo_fcoe_ddp_done)
		return n->netdev_ops->ndo_fcoe_ddp_done(n, xid);
	return 0;
}

static struct libfc_function_template fcoe_libfc_fcn_templ = {
	.frame_send = fcoe_xmit,
	.ddp_setup = fcoe_ddp_setup,
	.ddp_done = fcoe_ddp_done,
};

/**
 * fcoe_fip_recv - handle a received FIP frame.
 * @skb: the receive skb
 * @dev: associated &net_device
 * @ptype: the &packet_type structure which was used to register this handler.
 * @orig_dev: original receive &net_device, in case @dev is a bond.
 *
 * Returns: 0 for success
 */
static int fcoe_fip_recv(struct sk_buff *skb, struct net_device *dev,
			 struct packet_type *ptype,
			 struct net_device *orig_dev)
{
	struct fcoe_softc *fc;

	fc = container_of(ptype, struct fcoe_softc, fip_packet_type);
	fcoe_ctlr_recv(&fc->ctlr, skb);
	return 0;
}

/**
 * fcoe_fip_send() - send an Ethernet-encapsulated FIP frame.
 * @fip: FCoE controller.
 * @skb: FIP Packet.
 */
static void fcoe_fip_send(struct fcoe_ctlr *fip, struct sk_buff *skb)
{
	skb->dev = fcoe_from_ctlr(fip)->real_dev;
	dev_queue_xmit(skb);
}

/**
 * fcoe_update_src_mac() - Update Ethernet MAC filters.
 * @fip: FCoE controller.
 * @old: Unicast MAC address to delete if the MAC is non-zero.
 * @new: Unicast MAC address to add.
 *
 * Remove any previously-set unicast MAC filter.
 * Add secondary FCoE MAC address filter for our OUI.
 */
static void fcoe_update_src_mac(struct fcoe_ctlr *fip, u8 *old, u8 *new)
{
	struct fcoe_softc *fc;

	fc = fcoe_from_ctlr(fip);
	rtnl_lock();
	if (!is_zero_ether_addr(old))
		dev_unicast_delete(fc->real_dev, old, ETH_ALEN);
	dev_unicast_add(fc->real_dev, new, ETH_ALEN);
	rtnl_unlock();
}

/**
 * fcoe_if_create() - this function creates the fcoe interface
 * @netdev: pointer the associated netdevice
 *
 * Creates fc_lport struct and scsi_host for lport, configures lport
 * and starts fabric login.
 *
 * Returns : 0 on success
 */
static int fcoe_if_create(struct net_device *netdev)
{
	int rc;
	struct fc_lport *lp = NULL;
	struct fcoe_softc *fc;
	struct Scsi_Host *shost;

	BUG_ON(!netdev);

	printk(KERN_DEBUG "fcoe_if_create:interface on %s\n",
	       netdev->name);

	lp = fcoe_hostlist_lookup(netdev);
	if (lp)
		return -EEXIST;

	shost = libfc_host_alloc(&fcoe_shost_template,
				 sizeof(struct fcoe_softc));
	if (!shost) {
		FC_DBG("Could not allocate host structure\n");
		return -ENOMEM;
	}
	lp = shost_priv(shost);
	fc = lport_priv(lp);

	/* configure fc_lport, e.g., em */
	rc = fcoe_lport_config(lp);
	if (rc) {
		FC_DBG("Could not configure lport\n");
		goto out_host_put;
	}

	/* configure lport network properties */
	rc = fcoe_netdev_config(lp, netdev);
	if (rc) {
		FC_DBG("Could not configure netdev for lport\n");
		goto out_host_put;
	}

	/*
	 * Initialize FIP.
	 */
	fcoe_ctlr_init(&fc->ctlr);
	fc->ctlr.send = fcoe_fip_send;
	fc->ctlr.update_mac = fcoe_update_src_mac;

	fc->fip_packet_type.func = fcoe_fip_recv;
	fc->fip_packet_type.type = htons(ETH_P_FIP);
	fc->fip_packet_type.dev = fc->real_dev;
	dev_add_pack(&fc->fip_packet_type);

	/* configure lport scsi host properties */
	rc = fcoe_shost_config(lp, shost, &netdev->dev);
	if (rc) {
		FC_DBG("Could not configure shost for lport\n");
		goto out_host_put;
	}

	/* lport exch manager allocation */
	rc = fcoe_em_config(lp);
	if (rc) {
		FC_DBG("Could not configure em for lport\n");
		goto out_host_put;
	}

	/* Initialize the library */
	rc = fcoe_libfc_config(lp, &fcoe_libfc_fcn_templ);
	if (rc) {
		FC_DBG("Could not configure libfc for lport!\n");
		goto out_lp_destroy;
	}

	/* add to lports list */
	fcoe_hostlist_add(lp);

	lp->boot_time = jiffies;

	fc_fabric_login(lp);

	if (!fcoe_link_ok(lp))
		fcoe_ctlr_link_up(&fc->ctlr);

	dev_hold(netdev);

	return rc;

out_lp_destroy:
	fc_exch_mgr_free(lp->emp); /* Free the EM */
out_host_put:
	scsi_host_put(lp->host);
	return rc;
}

/**
 * fcoe_if_init() - attach to scsi transport
 *
 * Returns : 0 on success
 */
static int __init fcoe_if_init(void)
{
	/* attach to scsi transport */
	scsi_transport_fcoe_sw =
		fc_attach_transport(&fcoe_transport_function);

	if (!scsi_transport_fcoe_sw) {
		printk(KERN_ERR "fcoe_init:fc_attach_transport() failed\n");
		return -ENODEV;
	}

	return 0;
}

/**
 * fcoe_if_exit() - detach from scsi transport
 *
 * Returns : 0 on success
 */
int __exit fcoe_if_exit(void)
{
	fc_release_transport(scsi_transport_fcoe_sw);
	return 0;
}

/**
 * fcoe_percpu_thread_create() - Create a receive thread for an online cpu
 * @cpu: cpu index for the online cpu
 */
static void fcoe_percpu_thread_create(unsigned int cpu)
{
	struct fcoe_percpu_s *p;
	struct task_struct *thread;

	p = &per_cpu(fcoe_percpu, cpu);

	thread = kthread_create(fcoe_percpu_receive_thread,
				(void *)p, "fcoethread/%d", cpu);

	if (likely(!IS_ERR(p->thread))) {
		kthread_bind(thread, cpu);
		wake_up_process(thread);

		spin_lock_bh(&p->fcoe_rx_list.lock);
		p->thread = thread;
		spin_unlock_bh(&p->fcoe_rx_list.lock);
	}
}

/**
 * fcoe_percpu_thread_destroy() - removes the rx thread for the given cpu
 * @cpu: cpu index the rx thread is to be removed
 *
 * Destroys a per-CPU Rx thread. Any pending skbs are moved to the
 * current CPU's Rx thread. If the thread being destroyed is bound to
 * the CPU processing this context the skbs will be freed.
 */
static void fcoe_percpu_thread_destroy(unsigned int cpu)
{
	struct fcoe_percpu_s *p;
	struct task_struct *thread;
	struct page *crc_eof;
	struct sk_buff *skb;
#ifdef CONFIG_SMP
	struct fcoe_percpu_s *p0;
	unsigned targ_cpu = smp_processor_id();
#endif /* CONFIG_SMP */

	printk(KERN_DEBUG "fcoe: Destroying receive thread for CPU %d\n", cpu);

	/* Prevent any new skbs from being queued for this CPU. */
	p = &per_cpu(fcoe_percpu, cpu);
	spin_lock_bh(&p->fcoe_rx_list.lock);
	thread = p->thread;
	p->thread = NULL;
	crc_eof = p->crc_eof_page;
	p->crc_eof_page = NULL;
	p->crc_eof_offset = 0;
	spin_unlock_bh(&p->fcoe_rx_list.lock);

#ifdef CONFIG_SMP
	/*
	 * Don't bother moving the skb's if this context is running
	 * on the same CPU that is having its thread destroyed. This
	 * can easily happen when the module is removed.
	 */
	if (cpu != targ_cpu) {
		p0 = &per_cpu(fcoe_percpu, targ_cpu);
		spin_lock_bh(&p0->fcoe_rx_list.lock);
		if (p0->thread) {
			FC_DBG("Moving frames from CPU %d to CPU %d\n",
			       cpu, targ_cpu);

			while ((skb = __skb_dequeue(&p->fcoe_rx_list)) != NULL)
				__skb_queue_tail(&p0->fcoe_rx_list, skb);
			spin_unlock_bh(&p0->fcoe_rx_list.lock);
		} else {
			/*
			 * The targeted CPU is not initialized and cannot accept
			 * new  skbs. Unlock the targeted CPU and drop the skbs
			 * on the CPU that is going offline.
			 */
			while ((skb = __skb_dequeue(&p->fcoe_rx_list)) != NULL)
				kfree_skb(skb);
			spin_unlock_bh(&p0->fcoe_rx_list.lock);
		}
	} else {
		/*
		 * This scenario occurs when the module is being removed
		 * and all threads are being destroyed. skbs will continue
		 * to be shifted from the CPU thread that is being removed
		 * to the CPU thread associated with the CPU that is processing
		 * the module removal. Once there is only one CPU Rx thread it
		 * will reach this case and we will drop all skbs and later
		 * stop the thread.
		 */
		spin_lock_bh(&p->fcoe_rx_list.lock);
		while ((skb = __skb_dequeue(&p->fcoe_rx_list)) != NULL)
			kfree_skb(skb);
		spin_unlock_bh(&p->fcoe_rx_list.lock);
	}
#else
	/*
	 * This a non-SMP scenario where the singluar Rx thread is
	 * being removed. Free all skbs and stop the thread.
	 */
	spin_lock_bh(&p->fcoe_rx_list.lock);
	while ((skb = __skb_dequeue(&p->fcoe_rx_list)) != NULL)
		kfree_skb(skb);
	spin_unlock_bh(&p->fcoe_rx_list.lock);
#endif

	if (thread)
		kthread_stop(thread);

	if (crc_eof)
		put_page(crc_eof);
}

/**
 * fcoe_cpu_callback() - fcoe cpu hotplug event callback
 * @nfb: callback data block
 * @action: event triggering the callback
 * @hcpu: index for the cpu of this event
 *
 * This creates or destroys per cpu data for fcoe
 *
 * Returns NOTIFY_OK always.
 */
static int fcoe_cpu_callback(struct notifier_block *nfb,
			     unsigned long action, void *hcpu)
{
	unsigned cpu = (unsigned long)hcpu;

	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		FC_DBG("CPU %x online: Create Rx thread\n", cpu);
		fcoe_percpu_thread_create(cpu);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		FC_DBG("CPU %x offline: Remove Rx thread\n", cpu);
		fcoe_percpu_thread_destroy(cpu);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block fcoe_cpu_notifier = {
	.notifier_call = fcoe_cpu_callback,
};

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
	struct fc_frame_header *fh;
	struct fcoe_percpu_s *fps;
	unsigned short oxid;
	unsigned int cpu = 0;

	fc = container_of(ptype, struct fcoe_softc, fcoe_packet_type);
	lp = fc->ctlr.lp;
	if (unlikely(lp == NULL)) {
		FC_DBG("cannot find hba structure");
		goto err2;
	}
	if (!lp->link_up)
		goto err2;

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

#ifdef CONFIG_SMP
	/*
	 * The incoming frame exchange id(oxid) is ANDed with num of online
	 * cpu bits to get cpu and then this cpu is used for selecting
	 * a per cpu kernel thread from fcoe_percpu.
	 */
	cpu = oxid & (num_online_cpus() - 1);
#endif

	fps = &per_cpu(fcoe_percpu, cpu);
	spin_lock_bh(&fps->fcoe_rx_list.lock);
	if (unlikely(!fps->thread)) {
		/*
		 * The targeted CPU is not ready, let's target
		 * the first CPU now. For non-SMP systems this
		 * will check the same CPU twice.
		 */
		FC_DBG("CPU is online, but no receive thread ready "
		       "for incoming skb- using first online CPU.\n");

		spin_unlock_bh(&fps->fcoe_rx_list.lock);
		cpu = first_cpu(cpu_online_map);
		fps = &per_cpu(fcoe_percpu, cpu);
		spin_lock_bh(&fps->fcoe_rx_list.lock);
		if (!fps->thread) {
			spin_unlock_bh(&fps->fcoe_rx_list.lock);
			goto err;
		}
	}

	/*
	 * We now have a valid CPU that we're targeting for
	 * this skb. We also have this receive thread locked,
	 * so we're free to queue skbs into it's queue.
	 */
	__skb_queue_tail(&fps->fcoe_rx_list, skb);
	if (fps->fcoe_rx_list.qlen == 1)
		wake_up_process(fps->thread);

	spin_unlock_bh(&fps->fcoe_rx_list.lock);

	return 0;
err:
	fc_lport_get_stats(lp)->ErrorFrames++;

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

	fps = &get_cpu_var(fcoe_percpu);
	page = fps->crc_eof_page;
	if (!page) {
		page = alloc_page(GFP_ATOMIC);
		if (!page) {
			put_cpu_var(fcoe_percpu);
			return -ENOMEM;
		}
		fps->crc_eof_page = page;
		fps->crc_eof_offset = 0;
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
	put_cpu_var(fcoe_percpu);
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
	struct fcoe_softc *fc;
	u8 sof, eof;
	struct fcoe_hdr *hp;

	WARN_ON((fr_len(fp) % sizeof(u32)) != 0);

	fc = lport_priv(lp);
	fh = fc_frame_header_get(fp);
	skb = fp_skb(fp);
	wlen = skb->len / FCOE_WORD_TO_BYTE;

	if (!lp->link_up) {
		kfree(skb);
		return 0;
	}

	if (unlikely(fh->fh_r_ctl == FC_RCTL_ELS_REQ) &&
	    fcoe_ctlr_els_send(&fc->ctlr, skb))
		return 0;

	sof = fr_sof(fp);
	eof = fr_eof(fp);

	elen = (fc->real_dev->priv_flags & IFF_802_1Q_VLAN) ?
		sizeof(struct vlan_ethhdr) : sizeof(struct ethhdr);
	hlen = sizeof(struct fcoe_hdr);
	tlen = sizeof(struct fcoe_crc_eof);
	wlen = (skb->len - tlen + sizeof(crc)) / FCOE_WORD_TO_BYTE;

	/* crc offload */
	if (likely(lp->crc_offload)) {
		skb->ip_summed = CHECKSUM_PARTIAL;
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
	skb->protocol = htons(ETH_P_FCOE);
	skb->dev = fc->real_dev;

	/* fill up mac and fcoe headers */
	eh = eth_hdr(skb);
	eh->h_proto = htons(ETH_P_FCOE);
	if (fc->ctlr.map_dest)
		fc_fcoe_set_mac(eh->h_dest, fh->fh_d_id);
	else
		/* insert GW address */
		memcpy(eh->h_dest, fc->ctlr.dest_addr, ETH_ALEN);

	if (unlikely(fc->ctlr.flogi_oxid != FC_XID_UNKNOWN))
		memcpy(eh->h_source, fc->ctlr.ctl_src_addr, ETH_ALEN);
	else
		memcpy(eh->h_source, fc->ctlr.data_src_addr, ETH_ALEN);

	hp = (struct fcoe_hdr *)(eh + 1);
	memset(hp, 0, sizeof(*hp));
	if (FC_FCOE_VER)
		FC_FCOE_ENCAPS_VER(hp, FC_FCOE_VER);
	hp->fcoe_sof = sof;

#ifdef NETIF_F_FSO
	/* fcoe lso, mss is in max_payload which is non-zero for FCP data */
	if (lp->seq_offload && fr_max_payload(fp)) {
		skb_shinfo(skb)->gso_type = SKB_GSO_FCOE;
		skb_shinfo(skb)->gso_size = fr_max_payload(fp);
	} else {
		skb_shinfo(skb)->gso_type = 0;
		skb_shinfo(skb)->gso_size = 0;
	}
#endif
	/* update tx stats: regardless if LLD fails */
	stats = fc_lport_get_stats(lp);
	stats->TxFrames++;
	stats->TxWords += wlen;

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
		if (skb_is_nonlinear(skb))
			skb_linearize(skb);	/* not ideal */
		mac = eth_hdr(skb)->h_source;

		/*
		 * Frame length checks and setting up the header pointers
		 * was done in fcoe_rcv already.
		 */
		hp = (struct fcoe_hdr *) skb_network_header(skb);
		fh = (struct fc_frame_header *) skb_transport_header(skb);

		stats = fc_lport_get_stats(lp);
		if (unlikely(FC_FCOE_DECAPS_VER(hp) != FC_FCOE_VER)) {
			if (stats->ErrorFrames < 5)
				printk(KERN_WARNING "FCoE version "
				       "mismatch: The frame has "
				       "version %x, but the "
				       "initiator supports version "
				       "%x\n", FC_FCOE_DECAPS_VER(hp),
				       FC_FCOE_VER);
			stats->ErrorFrames++;
			kfree_skb(skb);
			continue;
		}

		skb_pull(skb, sizeof(struct fcoe_hdr));
		fr_len = skb->len - sizeof(struct fcoe_crc_eof);

		stats->RxFrames++;
		stats->RxWords += fr_len / FCOE_WORD_TO_BYTE;

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
		if (lp->crc_offload && skb->ip_summed == CHECKSUM_UNNECESSARY)
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
		if (unlikely(fc->ctlr.flogi_oxid != FC_XID_UNKNOWN) &&
		    fcoe_ctlr_recv_flogi(&fc->ctlr, fp, mac)) {
			fc_frame_free(fp);
			continue;
		}
		fc_exch_recv(lp, lp->emp, fp);
	}
	return 0;
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
		if (fc->ctlr.lp)
			fcoe_check_wait_queue(fc->ctlr.lp);
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
	u32 link_possible = 1;
	u32 mfs;
	int rc = NOTIFY_OK;

	read_lock(&fcoe_hostlist_lock);
	list_for_each_entry(fc, &fcoe_hostlist, list) {
		if (fc->real_dev == real_dev) {
			lp = fc->ctlr.lp;
			break;
		}
	}
	read_unlock(&fcoe_hostlist_lock);
	if (lp == NULL) {
		rc = NOTIFY_DONE;
		goto out;
	}

	switch (event) {
	case NETDEV_DOWN:
	case NETDEV_GOING_DOWN:
		link_possible = 0;
		break;
	case NETDEV_UP:
	case NETDEV_CHANGE:
		break;
	case NETDEV_CHANGEMTU:
		mfs = fc->real_dev->mtu -
			(sizeof(struct fcoe_hdr) +
			 sizeof(struct fcoe_crc_eof));
		if (mfs >= FC_MIN_MAX_FRAME)
			fc_set_mfs(lp, mfs);
		break;
	case NETDEV_REGISTER:
		break;
	default:
		FC_DBG("Unknown event %ld from netdev netlink\n", event);
	}
	if (link_possible && !fcoe_link_ok(lp))
		fcoe_ctlr_link_up(&fc->ctlr);
	else if (fcoe_ctlr_link_down(&fc->ctlr)) {
		stats = fc_lport_get_stats(lp);
		stats->LinkFailureCount++;
		fcoe_clean_pending_queue(lp);
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
	rc = fcoe_if_destroy(netdev);
	if (rc) {
		printk(KERN_ERR "fcoe: fcoe_if_destroy(%s) failed\n",
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

	rc = fcoe_if_create(netdev);
	if (rc) {
		printk(KERN_ERR "fcoe: fcoe_if_create(%s) failed\n",
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
	struct fcoe_percpu_s *pp;
	struct fcoe_rcv_info *fr;
	struct sk_buff_head *list;
	struct sk_buff *skb, *next;
	struct sk_buff *head;
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		pp = &per_cpu(fcoe_percpu, cpu);
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

	return (fc) ? fc->ctlr.lp : NULL;
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
 * fcoe_init() - fcoe module loading initialization
 *
 * Returns 0 on success, negative on failure
 */
static int __init fcoe_init(void)
{
	unsigned int cpu;
	int rc = 0;
	struct fcoe_percpu_s *p;

	INIT_LIST_HEAD(&fcoe_hostlist);
	rwlock_init(&fcoe_hostlist_lock);

	for_each_possible_cpu(cpu) {
		p = &per_cpu(fcoe_percpu, cpu);
		skb_queue_head_init(&p->fcoe_rx_list);
	}

	for_each_online_cpu(cpu)
		fcoe_percpu_thread_create(cpu);

	/* Initialize per CPU interrupt thread */
	rc = register_hotcpu_notifier(&fcoe_cpu_notifier);
	if (rc)
		goto out_free;

	/* Setup link change notification */
	fcoe_dev_setup();

	setup_timer(&fcoe_timer, fcoe_watchdog, 0);

	mod_timer(&fcoe_timer, jiffies + (10 * HZ));

	fcoe_if_init();

	return 0;

out_free:
	for_each_online_cpu(cpu) {
		fcoe_percpu_thread_destroy(cpu);
	}

	return rc;
}
module_init(fcoe_init);

/**
 * fcoe_exit() - fcoe module unloading cleanup
 *
 * Returns 0 on success, negative on failure
 */
static void __exit fcoe_exit(void)
{
	unsigned int cpu;
	struct fcoe_softc *fc, *tmp;

	fcoe_dev_cleanup();

	/* Stop the timer */
	del_timer_sync(&fcoe_timer);

	/* releases the associated fcoe hosts */
	list_for_each_entry_safe(fc, tmp, &fcoe_hostlist, list)
		fcoe_if_destroy(fc->real_dev);

	unregister_hotcpu_notifier(&fcoe_cpu_notifier);

	for_each_online_cpu(cpu) {
		fcoe_percpu_thread_destroy(cpu);
	}

	/* detach from scsi transport */
	fcoe_if_exit();
}
module_exit(fcoe_exit);
