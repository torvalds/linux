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
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <net/rtnetlink.h>

#include <scsi/fc/fc_els.h>
#include <scsi/fc/fc_encaps.h>
#include <scsi/fc/fc_fs.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>

#include <scsi/libfc.h>
#include <scsi/libfcoe.h>
#include <scsi/fc_transport_fcoe.h>

#define FCOE_SW_VERSION	"0.1"
#define	FCOE_SW_NAME	"fcoesw"
#define	FCOE_SW_VENDOR	"Open-FCoE.org"

#define FCOE_MAX_LUN		255
#define FCOE_MAX_FCP_TARGET	256

#define FCOE_MAX_OUTSTANDING_COMMANDS	1024

#define FCOE_MIN_XID		0x0001	/* the min xid supported by fcoe_sw */
#define FCOE_MAX_XID		0x07ef	/* the max xid supported by fcoe_sw */

static struct scsi_transport_template *scsi_transport_fcoe_sw;

struct fc_function_template fcoe_sw_transport_function = {
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

static struct scsi_host_template fcoe_sw_shost_template = {
	.module = THIS_MODULE,
	.name = "FCoE Driver",
	.proc_name = FCOE_SW_NAME,
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
 * fcoe_sw_lport_config() - sets up the fc_lport
 * @lp: ptr to the fc_lport
 * @shost: ptr to the parent scsi host
 *
 * Returns: 0 for success
 */
static int fcoe_sw_lport_config(struct fc_lport *lp)
{
	int i = 0;

	lp->link_up = 0;
	lp->qfull = 0;
	lp->max_retry_count = 3;
	lp->e_d_tov = 2 * 1000;	/* FC-FS default */
	lp->r_a_tov = 2 * 2 * 1000;
	lp->service_params = (FCP_SPPF_INIT_FCN | FCP_SPPF_RD_XRDY_DIS |
			      FCP_SPPF_RETRY | FCP_SPPF_CONF_COMPL);

	/*
	 * allocate per cpu stats block
	 */
	for_each_online_cpu(i)
		lp->dev_stats[i] = kzalloc(sizeof(struct fcoe_dev_stats),
					   GFP_KERNEL);

	/* lport fc_lport related configuration */
	fc_lport_config(lp);

	return 0;
}

/**
 * fcoe_sw_netdev_config() - Set up netdev for SW FCoE
 * @lp : ptr to the fc_lport
 * @netdev : ptr to the associated netdevice struct
 *
 * Must be called after fcoe_sw_lport_config() as it will use lport mutex
 *
 * Returns : 0 for success
 */
static int fcoe_sw_netdev_config(struct fc_lport *lp, struct net_device *netdev)
{
	u32 mfs;
	u64 wwnn, wwpn;
	struct fcoe_softc *fc;
	u8 flogi_maddr[ETH_ALEN];

	/* Setup lport private data to point to fcoe softc */
	fc = lport_priv(lp);
	fc->lp = lp;
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

	if (!fcoe_link_ok(lp))
		lp->link_up = 1;

	/* offload features support */
	if (fc->real_dev->features & NETIF_F_SG)
		lp->sg_supp = 1;


	skb_queue_head_init(&fc->fcoe_pending_queue);
	fc->fcoe_pending_queue_active = 0;

	/* setup Source Mac Address */
	memcpy(fc->ctl_src_addr, fc->real_dev->dev_addr,
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
 * fcoe_sw_shost_config() - Sets up fc_lport->host
 * @lp : ptr to the fc_lport
 * @shost : ptr to the associated scsi host
 * @dev : device associated to scsi host
 *
 * Must be called after fcoe_sw_lport_config() and fcoe_sw_netdev_config()
 *
 * Returns : 0 for success
 */
static int fcoe_sw_shost_config(struct fc_lport *lp, struct Scsi_Host *shost,
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
		FC_DBG("fcoe_sw_shost_config:error on scsi_add_host\n");
		return rc;
	}
	sprintf(fc_host_symbolic_name(lp->host), "%s v%s over %s",
		FCOE_SW_NAME, FCOE_SW_VERSION,
		fcoe_netdev(lp)->name);

	return 0;
}

/**
 * fcoe_sw_em_config() - allocates em for this lport
 * @lp: the port that em is to allocated for
 *
 * Returns : 0 on success
 */
static inline int fcoe_sw_em_config(struct fc_lport *lp)
{
	BUG_ON(lp->emp);

	lp->emp = fc_exch_mgr_alloc(lp, FC_CLASS_3,
				    FCOE_MIN_XID, FCOE_MAX_XID);
	if (!lp->emp)
		return -ENOMEM;

	return 0;
}

/**
 * fcoe_sw_destroy() - FCoE software HBA tear-down function
 * @netdev: ptr to the associated net_device
 *
 * Returns: 0 if link is OK for use by FCoE.
 */
static int fcoe_sw_destroy(struct net_device *netdev)
{
	int cpu;
	struct fc_lport *lp = NULL;
	struct fcoe_softc *fc;
	u8 flogi_maddr[ETH_ALEN];

	BUG_ON(!netdev);

	printk(KERN_DEBUG "fcoe_sw_destroy:interface on %s\n",
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
	if (compare_ether_addr(fc->data_src_addr, (u8[6]) { 0 }))
		dev_unicast_delete(fc->real_dev, fc->data_src_addr, ETH_ALEN);
	rtnl_unlock();

	/* Free the per-CPU revieve threads */
	fcoe_percpu_clean(lp);

	/* Free existing skbs */
	fcoe_clean_pending_queue(lp);

	/* Free memory used by statistical counters */
	for_each_online_cpu(cpu)
		kfree(lp->dev_stats[cpu]);

	/* Release the net_device and Scsi_Host */
	dev_put(fc->real_dev);
	scsi_host_put(lp->host);

	return 0;
}

static struct libfc_function_template fcoe_sw_libfc_fcn_templ = {
	.frame_send = fcoe_xmit,
};

/**
 * fcoe_sw_create() - this function creates the fcoe interface
 * @netdev: pointer the associated netdevice
 *
 * Creates fc_lport struct and scsi_host for lport, configures lport
 * and starts fabric login.
 *
 * Returns : 0 on success
 */
static int fcoe_sw_create(struct net_device *netdev)
{
	int rc;
	struct fc_lport *lp = NULL;
	struct fcoe_softc *fc;
	struct Scsi_Host *shost;

	BUG_ON(!netdev);

	printk(KERN_DEBUG "fcoe_sw_create:interface on %s\n",
	       netdev->name);

	lp = fcoe_hostlist_lookup(netdev);
	if (lp)
		return -EEXIST;

	shost = fcoe_host_alloc(&fcoe_sw_shost_template,
				sizeof(struct fcoe_softc));
	if (!shost) {
		FC_DBG("Could not allocate host structure\n");
		return -ENOMEM;
	}
	lp = shost_priv(shost);
	fc = lport_priv(lp);

	/* configure fc_lport, e.g., em */
	rc = fcoe_sw_lport_config(lp);
	if (rc) {
		FC_DBG("Could not configure lport\n");
		goto out_host_put;
	}

	/* configure lport network properties */
	rc = fcoe_sw_netdev_config(lp, netdev);
	if (rc) {
		FC_DBG("Could not configure netdev for lport\n");
		goto out_host_put;
	}

	/* configure lport scsi host properties */
	rc = fcoe_sw_shost_config(lp, shost, &netdev->dev);
	if (rc) {
		FC_DBG("Could not configure shost for lport\n");
		goto out_host_put;
	}

	/* lport exch manager allocation */
	rc = fcoe_sw_em_config(lp);
	if (rc) {
		FC_DBG("Could not configure em for lport\n");
		goto out_host_put;
	}

	/* Initialize the library */
	rc = fcoe_libfc_config(lp, &fcoe_sw_libfc_fcn_templ);
	if (rc) {
		FC_DBG("Could not configure libfc for lport!\n");
		goto out_lp_destroy;
	}

	/* add to lports list */
	fcoe_hostlist_add(lp);

	lp->boot_time = jiffies;

	fc_fabric_login(lp);

	dev_hold(netdev);

	return rc;

out_lp_destroy:
	fc_exch_mgr_free(lp->emp); /* Free the EM */
out_host_put:
	scsi_host_put(lp->host);
	return rc;
}

/**
 * fcoe_sw_match() - The FCoE SW transport match function
 *
 * Returns : false always
 */
static bool fcoe_sw_match(struct net_device *netdev)
{
	/* FIXME - for sw transport, always return false */
	return false;
}

/* the sw hba fcoe transport */
struct fcoe_transport fcoe_sw_transport = {
	.name = "fcoesw",
	.create = fcoe_sw_create,
	.destroy = fcoe_sw_destroy,
	.match = fcoe_sw_match,
	.vendor = 0x0,
	.device = 0xffff,
};

/**
 * fcoe_sw_init() - Registers fcoe_sw_transport
 *
 * Returns : 0 on success
 */
int __init fcoe_sw_init(void)
{
	/* attach to scsi transport */
	scsi_transport_fcoe_sw =
		fc_attach_transport(&fcoe_sw_transport_function);

	if (!scsi_transport_fcoe_sw) {
		printk(KERN_ERR "fcoe_sw_init:fc_attach_transport() failed\n");
		return -ENODEV;
	}

	mutex_init(&fcoe_sw_transport.devlock);
	INIT_LIST_HEAD(&fcoe_sw_transport.devlist);

	/* register sw transport */
	fcoe_transport_register(&fcoe_sw_transport);
	return 0;
}

/**
 * fcoe_sw_exit() - Unregisters fcoe_sw_transport
 *
 * Returns : 0 on success
 */
int __exit fcoe_sw_exit(void)
{
	/* dettach the transport */
	fc_release_transport(scsi_transport_fcoe_sw);
	fcoe_transport_unregister(&fcoe_sw_transport);
	return 0;
}
