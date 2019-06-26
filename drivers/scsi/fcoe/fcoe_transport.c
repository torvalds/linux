/*
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
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
#include <linux/netdevice.h>
#include <linux/errno.h>
#include <linux/crc32.h>
#include <scsi/libfcoe.h>

#include "libfcoe.h"

MODULE_AUTHOR("Open-FCoE.org");
MODULE_DESCRIPTION("FIP discovery protocol and FCoE transport for FCoE HBAs");
MODULE_LICENSE("GPL v2");

static int fcoe_transport_create(const char *, const struct kernel_param *);
static int fcoe_transport_destroy(const char *, const struct kernel_param *);
static int fcoe_transport_show(char *buffer, const struct kernel_param *kp);
static struct fcoe_transport *fcoe_transport_lookup(struct net_device *device);
static struct fcoe_transport *fcoe_netdev_map_lookup(struct net_device *device);
static int fcoe_transport_enable(const char *, const struct kernel_param *);
static int fcoe_transport_disable(const char *, const struct kernel_param *);
static int libfcoe_device_notification(struct notifier_block *notifier,
				    ulong event, void *ptr);

static LIST_HEAD(fcoe_transports);
static DEFINE_MUTEX(ft_mutex);
static LIST_HEAD(fcoe_netdevs);
static DEFINE_MUTEX(fn_mutex);

unsigned int libfcoe_debug_logging;
module_param_named(debug_logging, libfcoe_debug_logging, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(debug_logging, "a bit mask of logging levels");

module_param_call(show, NULL, fcoe_transport_show, NULL, S_IRUSR);
__MODULE_PARM_TYPE(show, "string");
MODULE_PARM_DESC(show, " Show attached FCoE transports");

module_param_call(create, fcoe_transport_create, NULL,
		  (void *)FIP_MODE_FABRIC, S_IWUSR);
__MODULE_PARM_TYPE(create, "string");
MODULE_PARM_DESC(create, " Creates fcoe instance on an ethernet interface");

module_param_call(create_vn2vn, fcoe_transport_create, NULL,
		  (void *)FIP_MODE_VN2VN, S_IWUSR);
__MODULE_PARM_TYPE(create_vn2vn, "string");
MODULE_PARM_DESC(create_vn2vn, " Creates a VN_node to VN_node FCoE instance "
		 "on an Ethernet interface");

module_param_call(destroy, fcoe_transport_destroy, NULL, NULL, S_IWUSR);
__MODULE_PARM_TYPE(destroy, "string");
MODULE_PARM_DESC(destroy, " Destroys fcoe instance on an ethernet interface");

module_param_call(enable, fcoe_transport_enable, NULL, NULL, S_IWUSR);
__MODULE_PARM_TYPE(enable, "string");
MODULE_PARM_DESC(enable, " Enables fcoe on an ethernet interface.");

module_param_call(disable, fcoe_transport_disable, NULL, NULL, S_IWUSR);
__MODULE_PARM_TYPE(disable, "string");
MODULE_PARM_DESC(disable, " Disables fcoe on an ethernet interface.");

/* notification function for packets from net device */
static struct notifier_block libfcoe_notifier = {
	.notifier_call = libfcoe_device_notification,
};

static const struct {
	u32 fc_port_speed;
#define SPEED_2000	2000
#define SPEED_4000	4000
#define SPEED_8000	8000
#define SPEED_16000	16000
#define SPEED_32000	32000
	u32 eth_port_speed;
} fcoe_port_speed_mapping[] = {
	{ FC_PORTSPEED_1GBIT,   SPEED_1000   },
	{ FC_PORTSPEED_2GBIT,   SPEED_2000   },
	{ FC_PORTSPEED_4GBIT,   SPEED_4000   },
	{ FC_PORTSPEED_8GBIT,   SPEED_8000   },
	{ FC_PORTSPEED_10GBIT,  SPEED_10000  },
	{ FC_PORTSPEED_16GBIT,  SPEED_16000  },
	{ FC_PORTSPEED_20GBIT,  SPEED_20000  },
	{ FC_PORTSPEED_25GBIT,  SPEED_25000  },
	{ FC_PORTSPEED_32GBIT,  SPEED_32000  },
	{ FC_PORTSPEED_40GBIT,  SPEED_40000  },
	{ FC_PORTSPEED_50GBIT,  SPEED_50000  },
	{ FC_PORTSPEED_100GBIT, SPEED_100000 },
};

static inline u32 eth2fc_speed(u32 eth_port_speed)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fcoe_port_speed_mapping); i++) {
		if (fcoe_port_speed_mapping[i].eth_port_speed == eth_port_speed)
			return fcoe_port_speed_mapping[i].fc_port_speed;
	}

	return FC_PORTSPEED_UNKNOWN;
}

/**
 * fcoe_link_speed_update() - Update the supported and actual link speeds
 * @lport: The local port to update speeds for
 *
 * Returns: 0 if the ethtool query was successful
 *          -1 if the ethtool query failed
 */
int fcoe_link_speed_update(struct fc_lport *lport)
{
	struct net_device *netdev = fcoe_get_netdev(lport);
	struct ethtool_link_ksettings ecmd;

	if (!__ethtool_get_link_ksettings(netdev, &ecmd)) {
		lport->link_supported_speeds &= ~(FC_PORTSPEED_1GBIT  |
		                                  FC_PORTSPEED_10GBIT |
		                                  FC_PORTSPEED_20GBIT |
		                                  FC_PORTSPEED_40GBIT);

		if (ecmd.link_modes.supported[0] & (
			    SUPPORTED_1000baseT_Half |
			    SUPPORTED_1000baseT_Full |
			    SUPPORTED_1000baseKX_Full))
			lport->link_supported_speeds |= FC_PORTSPEED_1GBIT;

		if (ecmd.link_modes.supported[0] & (
			    SUPPORTED_10000baseT_Full   |
			    SUPPORTED_10000baseKX4_Full |
			    SUPPORTED_10000baseKR_Full  |
			    SUPPORTED_10000baseR_FEC))
			lport->link_supported_speeds |= FC_PORTSPEED_10GBIT;

		if (ecmd.link_modes.supported[0] & (
			    SUPPORTED_20000baseMLD2_Full |
			    SUPPORTED_20000baseKR2_Full))
			lport->link_supported_speeds |= FC_PORTSPEED_20GBIT;

		if (ecmd.link_modes.supported[0] & (
			    SUPPORTED_40000baseKR4_Full |
			    SUPPORTED_40000baseCR4_Full |
			    SUPPORTED_40000baseSR4_Full |
			    SUPPORTED_40000baseLR4_Full))
			lport->link_supported_speeds |= FC_PORTSPEED_40GBIT;

		lport->link_speed = eth2fc_speed(ecmd.base.speed);
		return 0;
	}
	return -1;
}
EXPORT_SYMBOL_GPL(fcoe_link_speed_update);

/**
 * __fcoe_get_lesb() - Get the Link Error Status Block (LESB) for a given lport
 * @lport: The local port to update speeds for
 * @fc_lesb: Pointer to the LESB to be filled up
 * @netdev: Pointer to the netdev that is associated with the lport
 *
 * Note, the Link Error Status Block (LESB) for FCoE is defined in FC-BB-6
 * Clause 7.11 in v1.04.
 */
void __fcoe_get_lesb(struct fc_lport *lport,
		     struct fc_els_lesb *fc_lesb,
		     struct net_device *netdev)
{
	unsigned int cpu;
	u32 lfc, vlfc, mdac;
	struct fc_stats *stats;
	struct fcoe_fc_els_lesb *lesb;
	struct rtnl_link_stats64 temp;

	lfc = 0;
	vlfc = 0;
	mdac = 0;
	lesb = (struct fcoe_fc_els_lesb *)fc_lesb;
	memset(lesb, 0, sizeof(*lesb));
	for_each_possible_cpu(cpu) {
		stats = per_cpu_ptr(lport->stats, cpu);
		lfc += stats->LinkFailureCount;
		vlfc += stats->VLinkFailureCount;
		mdac += stats->MissDiscAdvCount;
	}
	lesb->lesb_link_fail = htonl(lfc);
	lesb->lesb_vlink_fail = htonl(vlfc);
	lesb->lesb_miss_fka = htonl(mdac);
	lesb->lesb_fcs_error =
			htonl(dev_get_stats(netdev, &temp)->rx_crc_errors);
}
EXPORT_SYMBOL_GPL(__fcoe_get_lesb);

/**
 * fcoe_get_lesb() - Fill the FCoE Link Error Status Block
 * @lport: the local port
 * @fc_lesb: the link error status block
 */
void fcoe_get_lesb(struct fc_lport *lport,
			 struct fc_els_lesb *fc_lesb)
{
	struct net_device *netdev = fcoe_get_netdev(lport);

	__fcoe_get_lesb(lport, fc_lesb, netdev);
}
EXPORT_SYMBOL_GPL(fcoe_get_lesb);

/**
 * fcoe_ctlr_get_lesb() - Get the Link Error Status Block (LESB) for a given
 * fcoe controller device
 * @ctlr_dev: The given fcoe controller device
 *
 */
void fcoe_ctlr_get_lesb(struct fcoe_ctlr_device *ctlr_dev)
{
	struct fcoe_ctlr *fip = fcoe_ctlr_device_priv(ctlr_dev);
	struct net_device *netdev = fcoe_get_netdev(fip->lp);
	struct fc_els_lesb *fc_lesb;

	fc_lesb = (struct fc_els_lesb *)(&ctlr_dev->lesb);
	__fcoe_get_lesb(fip->lp, fc_lesb, netdev);
}
EXPORT_SYMBOL_GPL(fcoe_ctlr_get_lesb);

void fcoe_wwn_to_str(u64 wwn, char *buf, int len)
{
	u8 wwpn[8];

	u64_to_wwn(wwn, wwpn);
	snprintf(buf, len, "%02x%02x%02x%02x%02x%02x%02x%02x",
		 wwpn[0], wwpn[1], wwpn[2], wwpn[3],
		 wwpn[4], wwpn[5], wwpn[6], wwpn[7]);
}
EXPORT_SYMBOL_GPL(fcoe_wwn_to_str);

/**
 * fcoe_validate_vport_create() - Validate a vport before creating it
 * @vport: NPIV port to be created
 *
 * This routine is meant to add validation for a vport before creating it
 * via fcoe_vport_create().
 * Current validations are:
 *      - WWPN supplied is unique for given lport
 */
int fcoe_validate_vport_create(struct fc_vport *vport)
{
	struct Scsi_Host *shost = vport_to_shost(vport);
	struct fc_lport *n_port = shost_priv(shost);
	struct fc_lport *vn_port;
	int rc = 0;
	char buf[32];

	mutex_lock(&n_port->lp_mutex);

	fcoe_wwn_to_str(vport->port_name, buf, sizeof(buf));
	/* Check if the wwpn is not same as that of the lport */
	if (!memcmp(&n_port->wwpn, &vport->port_name, sizeof(u64))) {
		LIBFCOE_TRANSPORT_DBG("vport WWPN 0x%s is same as that of the "
				      "base port WWPN\n", buf);
		rc = -EINVAL;
		goto out;
	}

	/* Check if there is any existing vport with same wwpn */
	list_for_each_entry(vn_port, &n_port->vports, list) {
		if (!memcmp(&vn_port->wwpn, &vport->port_name, sizeof(u64))) {
			LIBFCOE_TRANSPORT_DBG("vport with given WWPN 0x%s "
					      "already exists\n", buf);
			rc = -EINVAL;
			break;
		}
	}
out:
	mutex_unlock(&n_port->lp_mutex);
	return rc;
}
EXPORT_SYMBOL_GPL(fcoe_validate_vport_create);

/**
 * fcoe_get_wwn() - Get the world wide name from LLD if it supports it
 * @netdev: the associated net device
 * @wwn: the output WWN
 * @type: the type of WWN (WWPN or WWNN)
 *
 * Returns: 0 for success
 */
int fcoe_get_wwn(struct net_device *netdev, u64 *wwn, int type)
{
	const struct net_device_ops *ops = netdev->netdev_ops;

	if (ops->ndo_fcoe_get_wwn)
		return ops->ndo_fcoe_get_wwn(netdev, wwn, type);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(fcoe_get_wwn);

/**
 * fcoe_fc_crc() - Calculates the CRC for a given frame
 * @fp: The frame to be checksumed
 *
 * This uses crc32() routine to calculate the CRC for a frame
 *
 * Return: The 32 bit CRC value
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
		len = skb_frag_size(frag);
		while (len > 0) {
			clen = min(len, PAGE_SIZE - (off & ~PAGE_MASK));
			data = kmap_atomic(
				skb_frag_page(frag) + (off >> PAGE_SHIFT));
			crc = crc32(crc, data + (off & ~PAGE_MASK), clen);
			kunmap_atomic(data);
			off += clen;
			len -= clen;
		}
	}
	return crc;
}
EXPORT_SYMBOL_GPL(fcoe_fc_crc);

/**
 * fcoe_start_io() - Start FCoE I/O
 * @skb: The packet to be transmitted
 *
 * This routine is called from the net device to start transmitting
 * FCoE packets.
 *
 * Returns: 0 for success
 */
int fcoe_start_io(struct sk_buff *skb)
{
	struct sk_buff *nskb;
	int rc;

	nskb = skb_clone(skb, GFP_ATOMIC);
	if (!nskb)
		return -ENOMEM;
	rc = dev_queue_xmit(nskb);
	if (rc != 0)
		return rc;
	kfree_skb(skb);
	return 0;
}
EXPORT_SYMBOL_GPL(fcoe_start_io);


/**
 * fcoe_clean_pending_queue() - Dequeue a skb and free it
 * @lport: The local port to dequeue a skb on
 */
void fcoe_clean_pending_queue(struct fc_lport *lport)
{
	struct fcoe_port  *port = lport_priv(lport);
	struct sk_buff *skb;

	spin_lock_bh(&port->fcoe_pending_queue.lock);
	while ((skb = __skb_dequeue(&port->fcoe_pending_queue)) != NULL) {
		spin_unlock_bh(&port->fcoe_pending_queue.lock);
		kfree_skb(skb);
		spin_lock_bh(&port->fcoe_pending_queue.lock);
	}
	spin_unlock_bh(&port->fcoe_pending_queue.lock);
}
EXPORT_SYMBOL_GPL(fcoe_clean_pending_queue);

/**
 * fcoe_check_wait_queue() - Attempt to clear the transmit backlog
 * @lport: The local port whose backlog is to be cleared
 *
 * This empties the wait_queue, dequeues the head of the wait_queue queue
 * and calls fcoe_start_io() for each packet. If all skb have been
 * transmitted it returns the qlen. If an error occurs it restores
 * wait_queue (to try again later) and returns -1.
 *
 * The wait_queue is used when the skb transmit fails. The failed skb
 * will go in the wait_queue which will be emptied by the timer function or
 * by the next skb transmit.
 */
void fcoe_check_wait_queue(struct fc_lport *lport, struct sk_buff *skb)
{
	struct fcoe_port *port = lport_priv(lport);
	int rc;

	spin_lock_bh(&port->fcoe_pending_queue.lock);

	if (skb)
		__skb_queue_tail(&port->fcoe_pending_queue, skb);

	if (port->fcoe_pending_queue_active)
		goto out;
	port->fcoe_pending_queue_active = 1;

	while (port->fcoe_pending_queue.qlen) {
		/* keep qlen > 0 until fcoe_start_io succeeds */
		port->fcoe_pending_queue.qlen++;
		skb = __skb_dequeue(&port->fcoe_pending_queue);

		spin_unlock_bh(&port->fcoe_pending_queue.lock);
		rc = fcoe_start_io(skb);
		spin_lock_bh(&port->fcoe_pending_queue.lock);

		if (rc) {
			__skb_queue_head(&port->fcoe_pending_queue, skb);
			/* undo temporary increment above */
			port->fcoe_pending_queue.qlen--;
			break;
		}
		/* undo temporary increment above */
		port->fcoe_pending_queue.qlen--;
	}

	if (port->fcoe_pending_queue.qlen < port->min_queue_depth)
		lport->qfull = 0;
	if (port->fcoe_pending_queue.qlen && !timer_pending(&port->timer))
		mod_timer(&port->timer, jiffies + 2);
	port->fcoe_pending_queue_active = 0;
out:
	if (port->fcoe_pending_queue.qlen > port->max_queue_depth)
		lport->qfull = 1;
	spin_unlock_bh(&port->fcoe_pending_queue.lock);
}
EXPORT_SYMBOL_GPL(fcoe_check_wait_queue);

/**
 * fcoe_queue_timer() - The fcoe queue timer
 * @lport: The local port
 *
 * Calls fcoe_check_wait_queue on timeout
 */
void fcoe_queue_timer(struct timer_list *t)
{
	struct fcoe_port *port = from_timer(port, t, timer);

	fcoe_check_wait_queue(port->lport, NULL);
}
EXPORT_SYMBOL_GPL(fcoe_queue_timer);

/**
 * fcoe_get_paged_crc_eof() - Allocate a page to be used for the trailer CRC
 * @skb:  The packet to be transmitted
 * @tlen: The total length of the trailer
 * @fps:  The fcoe context
 *
 * This routine allocates a page for frame trailers. The page is re-used if
 * there is enough room left on it for the current trailer. If there isn't
 * enough buffer left a new page is allocated for the trailer. Reference to
 * the page from this function as well as the skbs using the page fragments
 * ensure that the page is freed at the appropriate time.
 *
 * Returns: 0 for success
 */
int fcoe_get_paged_crc_eof(struct sk_buff *skb, int tlen,
			   struct fcoe_percpu_s *fps)
{
	struct page *page;

	page = fps->crc_eof_page;
	if (!page) {
		page = alloc_page(GFP_ATOMIC);
		if (!page)
			return -ENOMEM;

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

	return 0;
}
EXPORT_SYMBOL_GPL(fcoe_get_paged_crc_eof);

/**
 * fcoe_transport_lookup - find an fcoe transport that matches a netdev
 * @netdev: The netdev to look for from all attached transports
 *
 * Returns : ptr to the fcoe transport that supports this netdev or NULL
 * if not found.
 *
 * The ft_mutex should be held when this is called
 */
static struct fcoe_transport *fcoe_transport_lookup(struct net_device *netdev)
{
	struct fcoe_transport *ft = NULL;

	list_for_each_entry(ft, &fcoe_transports, list)
		if (ft->match && ft->match(netdev))
			return ft;
	return NULL;
}

/**
 * fcoe_transport_attach - Attaches an FCoE transport
 * @ft: The fcoe transport to be attached
 *
 * Returns : 0 for success
 */
int fcoe_transport_attach(struct fcoe_transport *ft)
{
	int rc = 0;

	mutex_lock(&ft_mutex);
	if (ft->attached) {
		LIBFCOE_TRANSPORT_DBG("transport %s already attached\n",
				       ft->name);
		rc = -EEXIST;
		goto out_attach;
	}

	/* Add default transport to the tail */
	if (strcmp(ft->name, FCOE_TRANSPORT_DEFAULT))
		list_add(&ft->list, &fcoe_transports);
	else
		list_add_tail(&ft->list, &fcoe_transports);

	ft->attached = true;
	LIBFCOE_TRANSPORT_DBG("attaching transport %s\n", ft->name);

out_attach:
	mutex_unlock(&ft_mutex);
	return rc;
}
EXPORT_SYMBOL(fcoe_transport_attach);

/**
 * fcoe_transport_detach - Detaches an FCoE transport
 * @ft: The fcoe transport to be attached
 *
 * Returns : 0 for success
 */
int fcoe_transport_detach(struct fcoe_transport *ft)
{
	int rc = 0;
	struct fcoe_netdev_mapping *nm = NULL, *tmp;

	mutex_lock(&ft_mutex);
	if (!ft->attached) {
		LIBFCOE_TRANSPORT_DBG("transport %s already detached\n",
			ft->name);
		rc = -ENODEV;
		goto out_attach;
	}

	/* remove netdev mapping for this transport as it is going away */
	mutex_lock(&fn_mutex);
	list_for_each_entry_safe(nm, tmp, &fcoe_netdevs, list) {
		if (nm->ft == ft) {
			LIBFCOE_TRANSPORT_DBG("transport %s going away, "
				"remove its netdev mapping for %s\n",
				ft->name, nm->netdev->name);
			list_del(&nm->list);
			kfree(nm);
		}
	}
	mutex_unlock(&fn_mutex);

	list_del(&ft->list);
	ft->attached = false;
	LIBFCOE_TRANSPORT_DBG("detaching transport %s\n", ft->name);

out_attach:
	mutex_unlock(&ft_mutex);
	return rc;

}
EXPORT_SYMBOL(fcoe_transport_detach);

static int fcoe_transport_show(char *buffer, const struct kernel_param *kp)
{
	int i, j;
	struct fcoe_transport *ft = NULL;

	i = j = sprintf(buffer, "Attached FCoE transports:");
	mutex_lock(&ft_mutex);
	list_for_each_entry(ft, &fcoe_transports, list) {
		if (i >= PAGE_SIZE - IFNAMSIZ)
			break;
		i += snprintf(&buffer[i], IFNAMSIZ, "%s ", ft->name);
	}
	mutex_unlock(&ft_mutex);
	if (i == j)
		i += snprintf(&buffer[i], IFNAMSIZ, "none");
	return i;
}

static int __init fcoe_transport_init(void)
{
	register_netdevice_notifier(&libfcoe_notifier);
	return 0;
}

static int fcoe_transport_exit(void)
{
	struct fcoe_transport *ft;

	unregister_netdevice_notifier(&libfcoe_notifier);
	mutex_lock(&ft_mutex);
	list_for_each_entry(ft, &fcoe_transports, list)
		printk(KERN_ERR "FCoE transport %s is still attached!\n",
		      ft->name);
	mutex_unlock(&ft_mutex);
	return 0;
}


static int fcoe_add_netdev_mapping(struct net_device *netdev,
					struct fcoe_transport *ft)
{
	struct fcoe_netdev_mapping *nm;

	nm = kmalloc(sizeof(*nm), GFP_KERNEL);
	if (!nm) {
		printk(KERN_ERR "Unable to allocate netdev_mapping");
		return -ENOMEM;
	}

	nm->netdev = netdev;
	nm->ft = ft;

	mutex_lock(&fn_mutex);
	list_add(&nm->list, &fcoe_netdevs);
	mutex_unlock(&fn_mutex);
	return 0;
}


static void fcoe_del_netdev_mapping(struct net_device *netdev)
{
	struct fcoe_netdev_mapping *nm = NULL, *tmp;

	mutex_lock(&fn_mutex);
	list_for_each_entry_safe(nm, tmp, &fcoe_netdevs, list) {
		if (nm->netdev == netdev) {
			list_del(&nm->list);
			kfree(nm);
			mutex_unlock(&fn_mutex);
			return;
		}
	}
	mutex_unlock(&fn_mutex);
}


/**
 * fcoe_netdev_map_lookup - find the fcoe transport that matches the netdev on which
 * it was created
 *
 * Returns : ptr to the fcoe transport that supports this netdev or NULL
 * if not found.
 *
 * The ft_mutex should be held when this is called
 */
static struct fcoe_transport *fcoe_netdev_map_lookup(struct net_device *netdev)
{
	struct fcoe_transport *ft = NULL;
	struct fcoe_netdev_mapping *nm;

	mutex_lock(&fn_mutex);
	list_for_each_entry(nm, &fcoe_netdevs, list) {
		if (netdev == nm->netdev) {
			ft = nm->ft;
			mutex_unlock(&fn_mutex);
			return ft;
		}
	}

	mutex_unlock(&fn_mutex);
	return NULL;
}

/**
 * fcoe_if_to_netdev() - Parse a name buffer to get a net device
 * @buffer: The name of the net device
 *
 * Returns: NULL or a ptr to net_device
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
 * libfcoe_device_notification() - Handler for net device events
 * @notifier: The context of the notification
 * @event:    The type of event
 * @ptr:      The net device that the event was on
 *
 * This function is called by the Ethernet driver in case of link change event.
 *
 * Returns: 0 for success
 */
static int libfcoe_device_notification(struct notifier_block *notifier,
				    ulong event, void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);

	switch (event) {
	case NETDEV_UNREGISTER:
		LIBFCOE_TRANSPORT_DBG("NETDEV_UNREGISTER %s\n",
				      netdev->name);
		fcoe_del_netdev_mapping(netdev);
		break;
	}
	return NOTIFY_OK;
}

ssize_t fcoe_ctlr_create_store(struct bus_type *bus,
			       const char *buf, size_t count)
{
	struct net_device *netdev = NULL;
	struct fcoe_transport *ft = NULL;
	int rc = 0;
	int err;

	mutex_lock(&ft_mutex);

	netdev = fcoe_if_to_netdev(buf);
	if (!netdev) {
		LIBFCOE_TRANSPORT_DBG("Invalid device %s.\n", buf);
		rc = -ENODEV;
		goto out_nodev;
	}

	ft = fcoe_netdev_map_lookup(netdev);
	if (ft) {
		LIBFCOE_TRANSPORT_DBG("transport %s already has existing "
				      "FCoE instance on %s.\n",
				      ft->name, netdev->name);
		rc = -EEXIST;
		goto out_putdev;
	}

	ft = fcoe_transport_lookup(netdev);
	if (!ft) {
		LIBFCOE_TRANSPORT_DBG("no FCoE transport found for %s.\n",
				      netdev->name);
		rc = -ENODEV;
		goto out_putdev;
	}

	/* pass to transport create */
	err = ft->alloc ? ft->alloc(netdev) : -ENODEV;
	if (err) {
		fcoe_del_netdev_mapping(netdev);
		rc = -ENOMEM;
		goto out_putdev;
	}

	err = fcoe_add_netdev_mapping(netdev, ft);
	if (err) {
		LIBFCOE_TRANSPORT_DBG("failed to add new netdev mapping "
				      "for FCoE transport %s for %s.\n",
				      ft->name, netdev->name);
		rc = -ENODEV;
		goto out_putdev;
	}

	LIBFCOE_TRANSPORT_DBG("transport %s succeeded to create fcoe on %s.\n",
			      ft->name, netdev->name);

out_putdev:
	dev_put(netdev);
out_nodev:
	mutex_unlock(&ft_mutex);
	if (rc)
		return rc;
	return count;
}

ssize_t fcoe_ctlr_destroy_store(struct bus_type *bus,
				const char *buf, size_t count)
{
	int rc = -ENODEV;
	struct net_device *netdev = NULL;
	struct fcoe_transport *ft = NULL;

	mutex_lock(&ft_mutex);

	netdev = fcoe_if_to_netdev(buf);
	if (!netdev) {
		LIBFCOE_TRANSPORT_DBG("invalid device %s.\n", buf);
		goto out_nodev;
	}

	ft = fcoe_netdev_map_lookup(netdev);
	if (!ft) {
		LIBFCOE_TRANSPORT_DBG("no FCoE transport found for %s.\n",
				      netdev->name);
		goto out_putdev;
	}

	/* pass to transport destroy */
	rc = ft->destroy(netdev);
	if (rc)
		goto out_putdev;

	fcoe_del_netdev_mapping(netdev);
	LIBFCOE_TRANSPORT_DBG("transport %s %s to destroy fcoe on %s.\n",
			      ft->name, (rc) ? "failed" : "succeeded",
			      netdev->name);
	rc = count; /* required for successful return */
out_putdev:
	dev_put(netdev);
out_nodev:
	mutex_unlock(&ft_mutex);
	return rc;
}

/**
 * fcoe_transport_create() - Create a fcoe interface
 * @buffer: The name of the Ethernet interface to create on
 * @kp:	    The associated kernel param
 *
 * Called from sysfs. This holds the ft_mutex while calling the
 * registered fcoe transport's create function.
 *
 * Returns: 0 for success
 */
static int fcoe_transport_create(const char *buffer,
				 const struct kernel_param *kp)
{
	int rc = -ENODEV;
	struct net_device *netdev = NULL;
	struct fcoe_transport *ft = NULL;
	enum fip_mode fip_mode = (enum fip_mode)kp->arg;

	mutex_lock(&ft_mutex);

	netdev = fcoe_if_to_netdev(buffer);
	if (!netdev) {
		LIBFCOE_TRANSPORT_DBG("Invalid device %s.\n", buffer);
		goto out_nodev;
	}

	ft = fcoe_netdev_map_lookup(netdev);
	if (ft) {
		LIBFCOE_TRANSPORT_DBG("transport %s already has existing "
				      "FCoE instance on %s.\n",
				      ft->name, netdev->name);
		rc = -EEXIST;
		goto out_putdev;
	}

	ft = fcoe_transport_lookup(netdev);
	if (!ft) {
		LIBFCOE_TRANSPORT_DBG("no FCoE transport found for %s.\n",
				      netdev->name);
		goto out_putdev;
	}

	rc = fcoe_add_netdev_mapping(netdev, ft);
	if (rc) {
		LIBFCOE_TRANSPORT_DBG("failed to add new netdev mapping "
				      "for FCoE transport %s for %s.\n",
				      ft->name, netdev->name);
		goto out_putdev;
	}

	/* pass to transport create */
	rc = ft->create ? ft->create(netdev, fip_mode) : -ENODEV;
	if (rc)
		fcoe_del_netdev_mapping(netdev);

	LIBFCOE_TRANSPORT_DBG("transport %s %s to create fcoe on %s.\n",
			      ft->name, (rc) ? "failed" : "succeeded",
			      netdev->name);

out_putdev:
	dev_put(netdev);
out_nodev:
	mutex_unlock(&ft_mutex);
	return rc;
}

/**
 * fcoe_transport_destroy() - Destroy a FCoE interface
 * @buffer: The name of the Ethernet interface to be destroyed
 * @kp:	    The associated kernel parameter
 *
 * Called from sysfs. This holds the ft_mutex while calling the
 * registered fcoe transport's destroy function.
 *
 * Returns: 0 for success
 */
static int fcoe_transport_destroy(const char *buffer,
				  const struct kernel_param *kp)
{
	int rc = -ENODEV;
	struct net_device *netdev = NULL;
	struct fcoe_transport *ft = NULL;

	mutex_lock(&ft_mutex);

	netdev = fcoe_if_to_netdev(buffer);
	if (!netdev) {
		LIBFCOE_TRANSPORT_DBG("invalid device %s.\n", buffer);
		goto out_nodev;
	}

	ft = fcoe_netdev_map_lookup(netdev);
	if (!ft) {
		LIBFCOE_TRANSPORT_DBG("no FCoE transport found for %s.\n",
				      netdev->name);
		goto out_putdev;
	}

	/* pass to transport destroy */
	rc = ft->destroy ? ft->destroy(netdev) : -ENODEV;
	fcoe_del_netdev_mapping(netdev);
	LIBFCOE_TRANSPORT_DBG("transport %s %s to destroy fcoe on %s.\n",
			      ft->name, (rc) ? "failed" : "succeeded",
			      netdev->name);

out_putdev:
	dev_put(netdev);
out_nodev:
	mutex_unlock(&ft_mutex);
	return rc;
}

/**
 * fcoe_transport_disable() - Disables a FCoE interface
 * @buffer: The name of the Ethernet interface to be disabled
 * @kp:	    The associated kernel parameter
 *
 * Called from sysfs.
 *
 * Returns: 0 for success
 */
static int fcoe_transport_disable(const char *buffer,
				  const struct kernel_param *kp)
{
	int rc = -ENODEV;
	struct net_device *netdev = NULL;
	struct fcoe_transport *ft = NULL;

	mutex_lock(&ft_mutex);

	netdev = fcoe_if_to_netdev(buffer);
	if (!netdev)
		goto out_nodev;

	ft = fcoe_netdev_map_lookup(netdev);
	if (!ft)
		goto out_putdev;

	rc = ft->disable ? ft->disable(netdev) : -ENODEV;

out_putdev:
	dev_put(netdev);
out_nodev:
	mutex_unlock(&ft_mutex);
	return rc;
}

/**
 * fcoe_transport_enable() - Enables a FCoE interface
 * @buffer: The name of the Ethernet interface to be enabled
 * @kp:     The associated kernel parameter
 *
 * Called from sysfs.
 *
 * Returns: 0 for success
 */
static int fcoe_transport_enable(const char *buffer,
				 const struct kernel_param *kp)
{
	int rc = -ENODEV;
	struct net_device *netdev = NULL;
	struct fcoe_transport *ft = NULL;

	mutex_lock(&ft_mutex);

	netdev = fcoe_if_to_netdev(buffer);
	if (!netdev)
		goto out_nodev;

	ft = fcoe_netdev_map_lookup(netdev);
	if (!ft)
		goto out_putdev;

	rc = ft->enable ? ft->enable(netdev) : -ENODEV;

out_putdev:
	dev_put(netdev);
out_nodev:
	mutex_unlock(&ft_mutex);
	return rc;
}

/**
 * libfcoe_init() - Initialization routine for libfcoe.ko
 */
static int __init libfcoe_init(void)
{
	int rc = 0;

	rc = fcoe_transport_init();
	if (rc)
		return rc;

	rc = fcoe_sysfs_setup();
	if (rc)
		fcoe_transport_exit();

	return rc;
}
module_init(libfcoe_init);

/**
 * libfcoe_exit() - Tear down libfcoe.ko
 */
static void __exit libfcoe_exit(void)
{
	fcoe_sysfs_teardown();
	fcoe_transport_exit();
}
module_exit(libfcoe_exit);
