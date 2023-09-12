// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2005-2013 Solarflare Communications Inc.
 */

#include <linux/filter.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/notifier.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/in.h>
#include <linux/ethtool.h>
#include <linux/topology.h>
#include <linux/gfp.h>
#include <linux/interrupt.h>
#include "net_driver.h"
#include <net/gre.h>
#include <net/udp_tunnel.h>
#include "efx.h"
#include "efx_common.h"
#include "efx_channels.h"
#include "ef100.h"
#include "rx_common.h"
#include "tx_common.h"
#include "nic.h"
#include "io.h"
#include "selftest.h"
#include "sriov.h"
#include "efx_devlink.h"

#include "mcdi_port_common.h"
#include "mcdi_pcol.h"
#include "workarounds.h"

/**************************************************************************
 *
 * Configurable values
 *
 *************************************************************************/

module_param_named(interrupt_mode, efx_interrupt_mode, uint, 0444);
MODULE_PARM_DESC(interrupt_mode,
		 "Interrupt mode (0=>MSIX 1=>MSI 2=>legacy)");

module_param(rss_cpus, uint, 0444);
MODULE_PARM_DESC(rss_cpus, "Number of CPUs to use for Receive-Side Scaling");

/*
 * Use separate channels for TX and RX events
 *
 * Set this to 1 to use separate channels for TX and RX. It allows us
 * to control interrupt affinity separately for TX and RX.
 *
 * This is only used in MSI-X interrupt mode
 */
bool efx_separate_tx_channels;
module_param(efx_separate_tx_channels, bool, 0444);
MODULE_PARM_DESC(efx_separate_tx_channels,
		 "Use separate channels for TX and RX");

/* Initial interrupt moderation settings.  They can be modified after
 * module load with ethtool.
 *
 * The default for RX should strike a balance between increasing the
 * round-trip latency and reducing overhead.
 */
static unsigned int rx_irq_mod_usec = 60;

/* Initial interrupt moderation settings.  They can be modified after
 * module load with ethtool.
 *
 * This default is chosen to ensure that a 10G link does not go idle
 * while a TX queue is stopped after it has become full.  A queue is
 * restarted when it drops below half full.  The time this takes (assuming
 * worst case 3 descriptors per packet and 1024 descriptors) is
 *   512 / 3 * 1.2 = 205 usec.
 */
static unsigned int tx_irq_mod_usec = 150;

static bool phy_flash_cfg;
module_param(phy_flash_cfg, bool, 0644);
MODULE_PARM_DESC(phy_flash_cfg, "Set PHYs into reflash mode initially");

static unsigned debug = (NETIF_MSG_DRV | NETIF_MSG_PROBE |
			 NETIF_MSG_LINK | NETIF_MSG_IFDOWN |
			 NETIF_MSG_IFUP | NETIF_MSG_RX_ERR |
			 NETIF_MSG_TX_ERR | NETIF_MSG_HW);
module_param(debug, uint, 0);
MODULE_PARM_DESC(debug, "Bitmapped debugging message enable value");

/**************************************************************************
 *
 * Utility functions and prototypes
 *
 *************************************************************************/

static void efx_remove_port(struct efx_nic *efx);
static int efx_xdp_setup_prog(struct efx_nic *efx, struct bpf_prog *prog);
static int efx_xdp(struct net_device *dev, struct netdev_bpf *xdp);
static int efx_xdp_xmit(struct net_device *dev, int n, struct xdp_frame **xdpfs,
			u32 flags);

/**************************************************************************
 *
 * Port handling
 *
 **************************************************************************/

static void efx_fini_port(struct efx_nic *efx);

static int efx_probe_port(struct efx_nic *efx)
{
	int rc;

	netif_dbg(efx, probe, efx->net_dev, "create port\n");

	if (phy_flash_cfg)
		efx->phy_mode = PHY_MODE_SPECIAL;

	/* Connect up MAC/PHY operations table */
	rc = efx->type->probe_port(efx);
	if (rc)
		return rc;

	/* Initialise MAC address to permanent address */
	eth_hw_addr_set(efx->net_dev, efx->net_dev->perm_addr);

	return 0;
}

static int efx_init_port(struct efx_nic *efx)
{
	int rc;

	netif_dbg(efx, drv, efx->net_dev, "init port\n");

	mutex_lock(&efx->mac_lock);

	efx->port_initialized = true;

	/* Ensure the PHY advertises the correct flow control settings */
	rc = efx_mcdi_port_reconfigure(efx);
	if (rc && rc != -EPERM)
		goto fail;

	mutex_unlock(&efx->mac_lock);
	return 0;

fail:
	mutex_unlock(&efx->mac_lock);
	return rc;
}

static void efx_fini_port(struct efx_nic *efx)
{
	netif_dbg(efx, drv, efx->net_dev, "shut down port\n");

	if (!efx->port_initialized)
		return;

	efx->port_initialized = false;

	efx->link_state.up = false;
	efx_link_status_changed(efx);
}

static void efx_remove_port(struct efx_nic *efx)
{
	netif_dbg(efx, drv, efx->net_dev, "destroying port\n");

	efx->type->remove_port(efx);
}

/**************************************************************************
 *
 * NIC handling
 *
 **************************************************************************/

static LIST_HEAD(efx_primary_list);
static LIST_HEAD(efx_unassociated_list);

static bool efx_same_controller(struct efx_nic *left, struct efx_nic *right)
{
	return left->type == right->type &&
		left->vpd_sn && right->vpd_sn &&
		!strcmp(left->vpd_sn, right->vpd_sn);
}

static void efx_associate(struct efx_nic *efx)
{
	struct efx_nic *other, *next;

	if (efx->primary == efx) {
		/* Adding primary function; look for secondaries */

		netif_dbg(efx, probe, efx->net_dev, "adding to primary list\n");
		list_add_tail(&efx->node, &efx_primary_list);

		list_for_each_entry_safe(other, next, &efx_unassociated_list,
					 node) {
			if (efx_same_controller(efx, other)) {
				list_del(&other->node);
				netif_dbg(other, probe, other->net_dev,
					  "moving to secondary list of %s %s\n",
					  pci_name(efx->pci_dev),
					  efx->net_dev->name);
				list_add_tail(&other->node,
					      &efx->secondary_list);
				other->primary = efx;
			}
		}
	} else {
		/* Adding secondary function; look for primary */

		list_for_each_entry(other, &efx_primary_list, node) {
			if (efx_same_controller(efx, other)) {
				netif_dbg(efx, probe, efx->net_dev,
					  "adding to secondary list of %s %s\n",
					  pci_name(other->pci_dev),
					  other->net_dev->name);
				list_add_tail(&efx->node,
					      &other->secondary_list);
				efx->primary = other;
				return;
			}
		}

		netif_dbg(efx, probe, efx->net_dev,
			  "adding to unassociated list\n");
		list_add_tail(&efx->node, &efx_unassociated_list);
	}
}

static void efx_dissociate(struct efx_nic *efx)
{
	struct efx_nic *other, *next;

	list_del(&efx->node);
	efx->primary = NULL;

	list_for_each_entry_safe(other, next, &efx->secondary_list, node) {
		list_del(&other->node);
		netif_dbg(other, probe, other->net_dev,
			  "moving to unassociated list\n");
		list_add_tail(&other->node, &efx_unassociated_list);
		other->primary = NULL;
	}
}

static int efx_probe_nic(struct efx_nic *efx)
{
	int rc;

	netif_dbg(efx, probe, efx->net_dev, "creating NIC\n");

	/* Carry out hardware-type specific initialisation */
	rc = efx->type->probe(efx);
	if (rc)
		return rc;

	do {
		if (!efx->max_channels || !efx->max_tx_channels) {
			netif_err(efx, drv, efx->net_dev,
				  "Insufficient resources to allocate"
				  " any channels\n");
			rc = -ENOSPC;
			goto fail1;
		}

		/* Determine the number of channels and queues by trying
		 * to hook in MSI-X interrupts.
		 */
		rc = efx_probe_interrupts(efx);
		if (rc)
			goto fail1;

		rc = efx_set_channels(efx);
		if (rc)
			goto fail1;

		/* dimension_resources can fail with EAGAIN */
		rc = efx->type->dimension_resources(efx);
		if (rc != 0 && rc != -EAGAIN)
			goto fail2;

		if (rc == -EAGAIN)
			/* try again with new max_channels */
			efx_remove_interrupts(efx);

	} while (rc == -EAGAIN);

	if (efx->n_channels > 1)
		netdev_rss_key_fill(efx->rss_context.rx_hash_key,
				    sizeof(efx->rss_context.rx_hash_key));
	efx_set_default_rx_indir_table(efx, &efx->rss_context);

	/* Initialise the interrupt moderation settings */
	efx->irq_mod_step_us = DIV_ROUND_UP(efx->timer_quantum_ns, 1000);
	efx_init_irq_moderation(efx, tx_irq_mod_usec, rx_irq_mod_usec, true,
				true);

	return 0;

fail2:
	efx_remove_interrupts(efx);
fail1:
	efx->type->remove(efx);
	return rc;
}

static void efx_remove_nic(struct efx_nic *efx)
{
	netif_dbg(efx, drv, efx->net_dev, "destroying NIC\n");

	efx_remove_interrupts(efx);
	efx->type->remove(efx);
}

/**************************************************************************
 *
 * NIC startup/shutdown
 *
 *************************************************************************/

static int efx_probe_all(struct efx_nic *efx)
{
	int rc;

	rc = efx_probe_nic(efx);
	if (rc) {
		netif_err(efx, probe, efx->net_dev, "failed to create NIC\n");
		goto fail1;
	}

	rc = efx_probe_port(efx);
	if (rc) {
		netif_err(efx, probe, efx->net_dev, "failed to create port\n");
		goto fail2;
	}

	BUILD_BUG_ON(EFX_DEFAULT_DMAQ_SIZE < EFX_RXQ_MIN_ENT);
	if (WARN_ON(EFX_DEFAULT_DMAQ_SIZE < EFX_TXQ_MIN_ENT(efx))) {
		rc = -EINVAL;
		goto fail3;
	}

#ifdef CONFIG_SFC_SRIOV
	rc = efx->type->vswitching_probe(efx);
	if (rc) /* not fatal; the PF will still work fine */
		netif_warn(efx, probe, efx->net_dev,
			   "failed to setup vswitching rc=%d;"
			   " VFs may not function\n", rc);
#endif

	rc = efx_probe_filters(efx);
	if (rc) {
		netif_err(efx, probe, efx->net_dev,
			  "failed to create filter tables\n");
		goto fail4;
	}

	rc = efx_probe_channels(efx);
	if (rc)
		goto fail5;

	efx->state = STATE_NET_DOWN;

	return 0;

 fail5:
	efx_remove_filters(efx);
 fail4:
#ifdef CONFIG_SFC_SRIOV
	efx->type->vswitching_remove(efx);
#endif
 fail3:
	efx_remove_port(efx);
 fail2:
	efx_remove_nic(efx);
 fail1:
	return rc;
}

static void efx_remove_all(struct efx_nic *efx)
{
	rtnl_lock();
	efx_xdp_setup_prog(efx, NULL);
	rtnl_unlock();

	efx_remove_channels(efx);
	efx_remove_filters(efx);
#ifdef CONFIG_SFC_SRIOV
	efx->type->vswitching_remove(efx);
#endif
	efx_remove_port(efx);
	efx_remove_nic(efx);
}

/**************************************************************************
 *
 * Interrupt moderation
 *
 **************************************************************************/
unsigned int efx_usecs_to_ticks(struct efx_nic *efx, unsigned int usecs)
{
	if (usecs == 0)
		return 0;
	if (usecs * 1000 < efx->timer_quantum_ns)
		return 1; /* never round down to 0 */
	return usecs * 1000 / efx->timer_quantum_ns;
}

unsigned int efx_ticks_to_usecs(struct efx_nic *efx, unsigned int ticks)
{
	/* We must round up when converting ticks to microseconds
	 * because we round down when converting the other way.
	 */
	return DIV_ROUND_UP(ticks * efx->timer_quantum_ns, 1000);
}

/* Set interrupt moderation parameters */
int efx_init_irq_moderation(struct efx_nic *efx, unsigned int tx_usecs,
			    unsigned int rx_usecs, bool rx_adaptive,
			    bool rx_may_override_tx)
{
	struct efx_channel *channel;
	unsigned int timer_max_us;

	EFX_ASSERT_RESET_SERIALISED(efx);

	timer_max_us = efx->timer_max_ns / 1000;

	if (tx_usecs > timer_max_us || rx_usecs > timer_max_us)
		return -EINVAL;

	if (tx_usecs != rx_usecs && efx->tx_channel_offset == 0 &&
	    !rx_may_override_tx) {
		netif_err(efx, drv, efx->net_dev, "Channels are shared. "
			  "RX and TX IRQ moderation must be equal\n");
		return -EINVAL;
	}

	efx->irq_rx_adaptive = rx_adaptive;
	efx->irq_rx_moderation_us = rx_usecs;
	efx_for_each_channel(channel, efx) {
		if (efx_channel_has_rx_queue(channel))
			channel->irq_moderation_us = rx_usecs;
		else if (efx_channel_has_tx_queues(channel))
			channel->irq_moderation_us = tx_usecs;
		else if (efx_channel_is_xdp_tx(channel))
			channel->irq_moderation_us = tx_usecs;
	}

	return 0;
}

void efx_get_irq_moderation(struct efx_nic *efx, unsigned int *tx_usecs,
			    unsigned int *rx_usecs, bool *rx_adaptive)
{
	*rx_adaptive = efx->irq_rx_adaptive;
	*rx_usecs = efx->irq_rx_moderation_us;

	/* If channels are shared between RX and TX, so is IRQ
	 * moderation.  Otherwise, IRQ moderation is the same for all
	 * TX channels and is not adaptive.
	 */
	if (efx->tx_channel_offset == 0) {
		*tx_usecs = *rx_usecs;
	} else {
		struct efx_channel *tx_channel;

		tx_channel = efx->channel[efx->tx_channel_offset];
		*tx_usecs = tx_channel->irq_moderation_us;
	}
}

/**************************************************************************
 *
 * ioctls
 *
 *************************************************************************/

/* Net device ioctl
 * Context: process, rtnl_lock() held.
 */
static int efx_ioctl(struct net_device *net_dev, struct ifreq *ifr, int cmd)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);
	struct mii_ioctl_data *data = if_mii(ifr);

	if (cmd == SIOCSHWTSTAMP)
		return efx_ptp_set_ts_config(efx, ifr);
	if (cmd == SIOCGHWTSTAMP)
		return efx_ptp_get_ts_config(efx, ifr);

	/* Convert phy_id from older PRTAD/DEVAD format */
	if ((cmd == SIOCGMIIREG || cmd == SIOCSMIIREG) &&
	    (data->phy_id & 0xfc00) == 0x0400)
		data->phy_id ^= MDIO_PHY_ID_C45 | 0x0400;

	return mdio_mii_ioctl(&efx->mdio, data, cmd);
}

/**************************************************************************
 *
 * Kernel net device interface
 *
 *************************************************************************/

/* Context: process, rtnl_lock() held. */
int efx_net_open(struct net_device *net_dev)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);
	int rc;

	netif_dbg(efx, ifup, efx->net_dev, "opening device on CPU %d\n",
		  raw_smp_processor_id());

	rc = efx_check_disabled(efx);
	if (rc)
		return rc;
	if (efx->phy_mode & PHY_MODE_SPECIAL)
		return -EBUSY;
	if (efx_mcdi_poll_reboot(efx) && efx_reset(efx, RESET_TYPE_ALL))
		return -EIO;

	/* Notify the kernel of the link state polled during driver load,
	 * before the monitor starts running */
	efx_link_status_changed(efx);

	efx_start_all(efx);
	if (efx->state == STATE_DISABLED || efx->reset_pending)
		netif_device_detach(efx->net_dev);
	else
		efx->state = STATE_NET_UP;

	return 0;
}

/* Context: process, rtnl_lock() held.
 * Note that the kernel will ignore our return code; this method
 * should really be a void.
 */
int efx_net_stop(struct net_device *net_dev)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);

	netif_dbg(efx, ifdown, efx->net_dev, "closing on CPU %d\n",
		  raw_smp_processor_id());

	/* Stop the device and flush all the channels */
	efx_stop_all(efx);

	return 0;
}

static int efx_vlan_rx_add_vid(struct net_device *net_dev, __be16 proto, u16 vid)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);

	if (efx->type->vlan_rx_add_vid)
		return efx->type->vlan_rx_add_vid(efx, proto, vid);
	else
		return -EOPNOTSUPP;
}

static int efx_vlan_rx_kill_vid(struct net_device *net_dev, __be16 proto, u16 vid)
{
	struct efx_nic *efx = efx_netdev_priv(net_dev);

	if (efx->type->vlan_rx_kill_vid)
		return efx->type->vlan_rx_kill_vid(efx, proto, vid);
	else
		return -EOPNOTSUPP;
}

static const struct net_device_ops efx_netdev_ops = {
	.ndo_open		= efx_net_open,
	.ndo_stop		= efx_net_stop,
	.ndo_get_stats64	= efx_net_stats,
	.ndo_tx_timeout		= efx_watchdog,
	.ndo_start_xmit		= efx_hard_start_xmit,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_eth_ioctl		= efx_ioctl,
	.ndo_change_mtu		= efx_change_mtu,
	.ndo_set_mac_address	= efx_set_mac_address,
	.ndo_set_rx_mode	= efx_set_rx_mode,
	.ndo_set_features	= efx_set_features,
	.ndo_features_check	= efx_features_check,
	.ndo_vlan_rx_add_vid	= efx_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= efx_vlan_rx_kill_vid,
#ifdef CONFIG_SFC_SRIOV
	.ndo_set_vf_mac		= efx_sriov_set_vf_mac,
	.ndo_set_vf_vlan	= efx_sriov_set_vf_vlan,
	.ndo_set_vf_spoofchk	= efx_sriov_set_vf_spoofchk,
	.ndo_get_vf_config	= efx_sriov_get_vf_config,
	.ndo_set_vf_link_state  = efx_sriov_set_vf_link_state,
#endif
	.ndo_get_phys_port_id   = efx_get_phys_port_id,
	.ndo_get_phys_port_name	= efx_get_phys_port_name,
#ifdef CONFIG_RFS_ACCEL
	.ndo_rx_flow_steer	= efx_filter_rfs,
#endif
	.ndo_xdp_xmit		= efx_xdp_xmit,
	.ndo_bpf		= efx_xdp
};

static int efx_xdp_setup_prog(struct efx_nic *efx, struct bpf_prog *prog)
{
	struct bpf_prog *old_prog;

	if (efx->xdp_rxq_info_failed) {
		netif_err(efx, drv, efx->net_dev,
			  "Unable to bind XDP program due to previous failure of rxq_info\n");
		return -EINVAL;
	}

	if (prog && efx->net_dev->mtu > efx_xdp_max_mtu(efx)) {
		netif_err(efx, drv, efx->net_dev,
			  "Unable to configure XDP with MTU of %d (max: %d)\n",
			  efx->net_dev->mtu, efx_xdp_max_mtu(efx));
		return -EINVAL;
	}

	old_prog = rtnl_dereference(efx->xdp_prog);
	rcu_assign_pointer(efx->xdp_prog, prog);
	/* Release the reference that was originally passed by the caller. */
	if (old_prog)
		bpf_prog_put(old_prog);

	return 0;
}

/* Context: process, rtnl_lock() held. */
static int efx_xdp(struct net_device *dev, struct netdev_bpf *xdp)
{
	struct efx_nic *efx = efx_netdev_priv(dev);

	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return efx_xdp_setup_prog(efx, xdp->prog);
	default:
		return -EINVAL;
	}
}

static int efx_xdp_xmit(struct net_device *dev, int n, struct xdp_frame **xdpfs,
			u32 flags)
{
	struct efx_nic *efx = efx_netdev_priv(dev);

	if (!netif_running(dev))
		return -EINVAL;

	return efx_xdp_tx_buffers(efx, n, xdpfs, flags & XDP_XMIT_FLUSH);
}

static void efx_update_name(struct efx_nic *efx)
{
	strcpy(efx->name, efx->net_dev->name);
	efx_mtd_rename(efx);
	efx_set_channel_names(efx);
}

static int efx_netdev_event(struct notifier_block *this,
			    unsigned long event, void *ptr)
{
	struct net_device *net_dev = netdev_notifier_info_to_dev(ptr);

	if ((net_dev->netdev_ops == &efx_netdev_ops) &&
	    event == NETDEV_CHANGENAME)
		efx_update_name(efx_netdev_priv(net_dev));

	return NOTIFY_DONE;
}

static struct notifier_block efx_netdev_notifier = {
	.notifier_call = efx_netdev_event,
};

static ssize_t phy_type_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct efx_nic *efx = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", efx->phy_type);
}
static DEVICE_ATTR_RO(phy_type);

static int efx_register_netdev(struct efx_nic *efx)
{
	struct net_device *net_dev = efx->net_dev;
	struct efx_channel *channel;
	int rc;

	net_dev->watchdog_timeo = 5 * HZ;
	net_dev->irq = efx->pci_dev->irq;
	net_dev->netdev_ops = &efx_netdev_ops;
	if (efx_nic_rev(efx) >= EFX_REV_HUNT_A0)
		net_dev->priv_flags |= IFF_UNICAST_FLT;
	net_dev->ethtool_ops = &efx_ethtool_ops;
	netif_set_tso_max_segs(net_dev, EFX_TSO_MAX_SEGS);
	net_dev->min_mtu = EFX_MIN_MTU;
	net_dev->max_mtu = EFX_MAX_MTU;

	rtnl_lock();

	/* Enable resets to be scheduled and check whether any were
	 * already requested.  If so, the NIC is probably hosed so we
	 * abort.
	 */
	if (efx->reset_pending) {
		pci_err(efx->pci_dev, "aborting probe due to scheduled reset\n");
		rc = -EIO;
		goto fail_locked;
	}

	rc = dev_alloc_name(net_dev, net_dev->name);
	if (rc < 0)
		goto fail_locked;
	efx_update_name(efx);

	/* Always start with carrier off; PHY events will detect the link */
	netif_carrier_off(net_dev);

	rc = register_netdevice(net_dev);
	if (rc)
		goto fail_locked;

	efx_for_each_channel(channel, efx) {
		struct efx_tx_queue *tx_queue;
		efx_for_each_channel_tx_queue(tx_queue, channel)
			efx_init_tx_queue_core_txq(tx_queue);
	}

	efx_associate(efx);

	efx->state = STATE_NET_DOWN;

	rtnl_unlock();

	rc = device_create_file(&efx->pci_dev->dev, &dev_attr_phy_type);
	if (rc) {
		netif_err(efx, drv, efx->net_dev,
			  "failed to init net dev attributes\n");
		goto fail_registered;
	}

	efx_init_mcdi_logging(efx);

	return 0;

fail_registered:
	rtnl_lock();
	efx_dissociate(efx);
	unregister_netdevice(net_dev);
fail_locked:
	efx->state = STATE_UNINIT;
	rtnl_unlock();
	netif_err(efx, drv, efx->net_dev, "could not register net dev\n");
	return rc;
}

static void efx_unregister_netdev(struct efx_nic *efx)
{
	if (!efx->net_dev)
		return;

	if (WARN_ON(efx_netdev_priv(efx->net_dev) != efx))
		return;

	if (efx_dev_registered(efx)) {
		strscpy(efx->name, pci_name(efx->pci_dev), sizeof(efx->name));
		efx_fini_mcdi_logging(efx);
		device_remove_file(&efx->pci_dev->dev, &dev_attr_phy_type);
		unregister_netdev(efx->net_dev);
	}
}

/**************************************************************************
 *
 * List of NICs we support
 *
 **************************************************************************/

/* PCI device ID table */
static const struct pci_device_id efx_pci_table[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_SOLARFLARE, 0x0903),  /* SFC9120 PF */
	 .driver_data = (unsigned long) &efx_hunt_a0_nic_type},
	{PCI_DEVICE(PCI_VENDOR_ID_SOLARFLARE, 0x1903),  /* SFC9120 VF */
	 .driver_data = (unsigned long) &efx_hunt_a0_vf_nic_type},
	{PCI_DEVICE(PCI_VENDOR_ID_SOLARFLARE, 0x0923),  /* SFC9140 PF */
	 .driver_data = (unsigned long) &efx_hunt_a0_nic_type},
	{PCI_DEVICE(PCI_VENDOR_ID_SOLARFLARE, 0x1923),  /* SFC9140 VF */
	 .driver_data = (unsigned long) &efx_hunt_a0_vf_nic_type},
	{PCI_DEVICE(PCI_VENDOR_ID_SOLARFLARE, 0x0a03),  /* SFC9220 PF */
	 .driver_data = (unsigned long) &efx_hunt_a0_nic_type},
	{PCI_DEVICE(PCI_VENDOR_ID_SOLARFLARE, 0x1a03),  /* SFC9220 VF */
	 .driver_data = (unsigned long) &efx_hunt_a0_vf_nic_type},
	{PCI_DEVICE(PCI_VENDOR_ID_SOLARFLARE, 0x0b03),  /* SFC9250 PF */
	 .driver_data = (unsigned long) &efx_hunt_a0_nic_type},
	{PCI_DEVICE(PCI_VENDOR_ID_SOLARFLARE, 0x1b03),  /* SFC9250 VF */
	 .driver_data = (unsigned long) &efx_hunt_a0_vf_nic_type},
	{0}			/* end of list */
};

/**************************************************************************
 *
 * Data housekeeping
 *
 **************************************************************************/

void efx_update_sw_stats(struct efx_nic *efx, u64 *stats)
{
	u64 n_rx_nodesc_trunc = 0;
	struct efx_channel *channel;

	efx_for_each_channel(channel, efx)
		n_rx_nodesc_trunc += channel->n_rx_nodesc_trunc;
	stats[GENERIC_STAT_rx_nodesc_trunc] = n_rx_nodesc_trunc;
	stats[GENERIC_STAT_rx_noskb_drops] = atomic_read(&efx->n_rx_noskb_drops);
}

/**************************************************************************
 *
 * PCI interface
 *
 **************************************************************************/

/* Main body of final NIC shutdown code
 * This is called only at module unload (or hotplug removal).
 */
static void efx_pci_remove_main(struct efx_nic *efx)
{
	/* Flush reset_work. It can no longer be scheduled since we
	 * are not READY.
	 */
	WARN_ON(efx_net_active(efx->state));
	efx_flush_reset_workqueue(efx);

	efx_disable_interrupts(efx);
	efx_clear_interrupt_affinity(efx);
	efx_nic_fini_interrupt(efx);
	efx_fini_port(efx);
	efx->type->fini(efx);
	efx_fini_napi(efx);
	efx_remove_all(efx);
}

/* Final NIC shutdown
 * This is called only at module unload (or hotplug removal).  A PF can call
 * this on its VFs to ensure they are unbound first.
 */
static void efx_pci_remove(struct pci_dev *pci_dev)
{
	struct efx_probe_data *probe_data;
	struct efx_nic *efx;

	efx = pci_get_drvdata(pci_dev);
	if (!efx)
		return;

	/* Mark the NIC as fini, then stop the interface */
	rtnl_lock();
	efx_dissociate(efx);
	dev_close(efx->net_dev);
	efx_disable_interrupts(efx);
	efx->state = STATE_UNINIT;
	rtnl_unlock();

	if (efx->type->sriov_fini)
		efx->type->sriov_fini(efx);

	efx_fini_devlink_lock(efx);
	efx_unregister_netdev(efx);

	efx_mtd_remove(efx);

	efx_pci_remove_main(efx);

	efx_fini_io(efx);
	pci_dbg(efx->pci_dev, "shutdown successful\n");

	efx_fini_devlink_and_unlock(efx);
	efx_fini_struct(efx);
	free_netdev(efx->net_dev);
	probe_data = container_of(efx, struct efx_probe_data, efx);
	kfree(probe_data);
};

/* NIC VPD information
 * Called during probe to display the part number of the
 * installed NIC.
 */
static void efx_probe_vpd_strings(struct efx_nic *efx)
{
	struct pci_dev *dev = efx->pci_dev;
	unsigned int vpd_size, kw_len;
	u8 *vpd_data;
	int start;

	vpd_data = pci_vpd_alloc(dev, &vpd_size);
	if (IS_ERR(vpd_data)) {
		pci_warn(dev, "Unable to read VPD\n");
		return;
	}

	start = pci_vpd_find_ro_info_keyword(vpd_data, vpd_size,
					     PCI_VPD_RO_KEYWORD_PARTNO, &kw_len);
	if (start < 0)
		pci_err(dev, "Part number not found or incomplete\n");
	else
		pci_info(dev, "Part Number : %.*s\n", kw_len, vpd_data + start);

	start = pci_vpd_find_ro_info_keyword(vpd_data, vpd_size,
					     PCI_VPD_RO_KEYWORD_SERIALNO, &kw_len);
	if (start < 0)
		pci_err(dev, "Serial number not found or incomplete\n");
	else
		efx->vpd_sn = kmemdup_nul(vpd_data + start, kw_len, GFP_KERNEL);

	kfree(vpd_data);
}


/* Main body of NIC initialisation
 * This is called at module load (or hotplug insertion, theoretically).
 */
static int efx_pci_probe_main(struct efx_nic *efx)
{
	int rc;

	/* Do start-of-day initialisation */
	rc = efx_probe_all(efx);
	if (rc)
		goto fail1;

	efx_init_napi(efx);

	down_write(&efx->filter_sem);
	rc = efx->type->init(efx);
	up_write(&efx->filter_sem);
	if (rc) {
		pci_err(efx->pci_dev, "failed to initialise NIC\n");
		goto fail3;
	}

	rc = efx_init_port(efx);
	if (rc) {
		netif_err(efx, probe, efx->net_dev,
			  "failed to initialise port\n");
		goto fail4;
	}

	rc = efx_nic_init_interrupt(efx);
	if (rc)
		goto fail5;

	efx_set_interrupt_affinity(efx);
	rc = efx_enable_interrupts(efx);
	if (rc)
		goto fail6;

	return 0;

 fail6:
	efx_clear_interrupt_affinity(efx);
	efx_nic_fini_interrupt(efx);
 fail5:
	efx_fini_port(efx);
 fail4:
	efx->type->fini(efx);
 fail3:
	efx_fini_napi(efx);
	efx_remove_all(efx);
 fail1:
	return rc;
}

static int efx_pci_probe_post_io(struct efx_nic *efx)
{
	struct net_device *net_dev = efx->net_dev;
	int rc = efx_pci_probe_main(efx);

	if (rc)
		return rc;

	if (efx->type->sriov_init) {
		rc = efx->type->sriov_init(efx);
		if (rc)
			pci_err(efx->pci_dev, "SR-IOV can't be enabled rc %d\n",
				rc);
	}

	/* Determine netdevice features */
	net_dev->features |= efx->type->offload_features;

	/* Add TSO features */
	if (efx->type->tso_versions && efx->type->tso_versions(efx))
		net_dev->features |= NETIF_F_TSO | NETIF_F_TSO6;

	/* Mask for features that also apply to VLAN devices */
	net_dev->vlan_features |= (NETIF_F_HW_CSUM | NETIF_F_SG |
				   NETIF_F_HIGHDMA | NETIF_F_ALL_TSO |
				   NETIF_F_RXCSUM);

	/* Determine user configurable features */
	net_dev->hw_features |= net_dev->features & ~efx->fixed_features;

	/* Disable receiving frames with bad FCS, by default. */
	net_dev->features &= ~NETIF_F_RXALL;

	/* Disable VLAN filtering by default.  It may be enforced if
	 * the feature is fixed (i.e. VLAN filters are required to
	 * receive VLAN tagged packets due to vPort restrictions).
	 */
	net_dev->features &= ~NETIF_F_HW_VLAN_CTAG_FILTER;
	net_dev->features |= efx->fixed_features;

	net_dev->xdp_features = NETDEV_XDP_ACT_BASIC |
				NETDEV_XDP_ACT_REDIRECT |
				NETDEV_XDP_ACT_NDO_XMIT;

	/* devlink creation, registration and lock */
	rc = efx_probe_devlink_and_lock(efx);
	if (rc)
		pci_err(efx->pci_dev, "devlink registration failed");

	rc = efx_register_netdev(efx);
	efx_probe_devlink_unlock(efx);
	if (!rc)
		return 0;

	efx_pci_remove_main(efx);
	return rc;
}

/* NIC initialisation
 *
 * This is called at module load (or hotplug insertion,
 * theoretically).  It sets up PCI mappings, resets the NIC,
 * sets up and registers the network devices with the kernel and hooks
 * the interrupt service routine.  It does not prepare the device for
 * transmission; this is left to the first time one of the network
 * interfaces is brought up (i.e. efx_net_open).
 */
static int efx_pci_probe(struct pci_dev *pci_dev,
			 const struct pci_device_id *entry)
{
	struct efx_probe_data *probe_data, **probe_ptr;
	struct net_device *net_dev;
	struct efx_nic *efx;
	int rc;

	/* Allocate probe data and struct efx_nic */
	probe_data = kzalloc(sizeof(*probe_data), GFP_KERNEL);
	if (!probe_data)
		return -ENOMEM;
	probe_data->pci_dev = pci_dev;
	efx = &probe_data->efx;

	/* Allocate and initialise a struct net_device */
	net_dev = alloc_etherdev_mq(sizeof(probe_data), EFX_MAX_CORE_TX_QUEUES);
	if (!net_dev) {
		rc = -ENOMEM;
		goto fail0;
	}
	probe_ptr = netdev_priv(net_dev);
	*probe_ptr = probe_data;
	efx->net_dev = net_dev;
	efx->type = (const struct efx_nic_type *) entry->driver_data;
	efx->fixed_features |= NETIF_F_HIGHDMA;

	pci_set_drvdata(pci_dev, efx);
	SET_NETDEV_DEV(net_dev, &pci_dev->dev);
	rc = efx_init_struct(efx, pci_dev);
	if (rc)
		goto fail1;
	efx->mdio.dev = net_dev;

	pci_info(pci_dev, "Solarflare NIC detected\n");

	if (!efx->type->is_vf)
		efx_probe_vpd_strings(efx);

	/* Set up basic I/O (BAR mappings etc) */
	rc = efx_init_io(efx, efx->type->mem_bar(efx), efx->type->max_dma_mask,
			 efx->type->mem_map_size(efx));
	if (rc)
		goto fail2;

	rc = efx_pci_probe_post_io(efx);
	if (rc) {
		/* On failure, retry once immediately.
		 * If we aborted probe due to a scheduled reset, dismiss it.
		 */
		efx->reset_pending = 0;
		rc = efx_pci_probe_post_io(efx);
		if (rc) {
			/* On another failure, retry once more
			 * after a 50-305ms delay.
			 */
			unsigned char r;

			get_random_bytes(&r, 1);
			msleep((unsigned int)r + 50);
			efx->reset_pending = 0;
			rc = efx_pci_probe_post_io(efx);
		}
	}
	if (rc)
		goto fail3;

	netif_dbg(efx, probe, efx->net_dev, "initialisation successful\n");

	/* Try to create MTDs, but allow this to fail */
	rtnl_lock();
	rc = efx_mtd_probe(efx);
	rtnl_unlock();
	if (rc && rc != -EPERM)
		netif_warn(efx, probe, efx->net_dev,
			   "failed to create MTDs (%d)\n", rc);

	if (efx->type->udp_tnl_push_ports)
		efx->type->udp_tnl_push_ports(efx);

	return 0;

 fail3:
	efx_fini_io(efx);
 fail2:
	efx_fini_struct(efx);
 fail1:
	WARN_ON(rc > 0);
	netif_dbg(efx, drv, efx->net_dev, "initialisation failed. rc=%d\n", rc);
	free_netdev(net_dev);
 fail0:
	kfree(probe_data);
	return rc;
}

/* efx_pci_sriov_configure returns the actual number of Virtual Functions
 * enabled on success
 */
#ifdef CONFIG_SFC_SRIOV
static int efx_pci_sriov_configure(struct pci_dev *dev, int num_vfs)
{
	int rc;
	struct efx_nic *efx = pci_get_drvdata(dev);

	if (efx->type->sriov_configure) {
		rc = efx->type->sriov_configure(efx, num_vfs);
		if (rc)
			return rc;
		else
			return num_vfs;
	} else
		return -EOPNOTSUPP;
}
#endif

static int efx_pm_freeze(struct device *dev)
{
	struct efx_nic *efx = dev_get_drvdata(dev);

	rtnl_lock();

	if (efx_net_active(efx->state)) {
		efx_device_detach_sync(efx);

		efx_stop_all(efx);
		efx_disable_interrupts(efx);

		efx->state = efx_freeze(efx->state);
	}

	rtnl_unlock();

	return 0;
}

static void efx_pci_shutdown(struct pci_dev *pci_dev)
{
	struct efx_nic *efx = pci_get_drvdata(pci_dev);

	if (!efx)
		return;

	efx_pm_freeze(&pci_dev->dev);
	pci_disable_device(pci_dev);
}

static int efx_pm_thaw(struct device *dev)
{
	int rc;
	struct efx_nic *efx = dev_get_drvdata(dev);

	rtnl_lock();

	if (efx_frozen(efx->state)) {
		rc = efx_enable_interrupts(efx);
		if (rc)
			goto fail;

		mutex_lock(&efx->mac_lock);
		efx_mcdi_port_reconfigure(efx);
		mutex_unlock(&efx->mac_lock);

		efx_start_all(efx);

		efx_device_attach_if_not_resetting(efx);

		efx->state = efx_thaw(efx->state);

		efx->type->resume_wol(efx);
	}

	rtnl_unlock();

	/* Reschedule any quenched resets scheduled during efx_pm_freeze() */
	efx_queue_reset_work(efx);

	return 0;

fail:
	rtnl_unlock();

	return rc;
}

static int efx_pm_poweroff(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct efx_nic *efx = pci_get_drvdata(pci_dev);

	efx->type->fini(efx);

	efx->reset_pending = 0;

	pci_save_state(pci_dev);
	return pci_set_power_state(pci_dev, PCI_D3hot);
}

/* Used for both resume and restore */
static int efx_pm_resume(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct efx_nic *efx = pci_get_drvdata(pci_dev);
	int rc;

	rc = pci_set_power_state(pci_dev, PCI_D0);
	if (rc)
		return rc;
	pci_restore_state(pci_dev);
	rc = pci_enable_device(pci_dev);
	if (rc)
		return rc;
	pci_set_master(efx->pci_dev);
	rc = efx->type->reset(efx, RESET_TYPE_ALL);
	if (rc)
		return rc;
	down_write(&efx->filter_sem);
	rc = efx->type->init(efx);
	up_write(&efx->filter_sem);
	if (rc)
		return rc;
	rc = efx_pm_thaw(dev);
	return rc;
}

static int efx_pm_suspend(struct device *dev)
{
	int rc;

	efx_pm_freeze(dev);
	rc = efx_pm_poweroff(dev);
	if (rc)
		efx_pm_resume(dev);
	return rc;
}

static const struct dev_pm_ops efx_pm_ops = {
	.suspend	= efx_pm_suspend,
	.resume		= efx_pm_resume,
	.freeze		= efx_pm_freeze,
	.thaw		= efx_pm_thaw,
	.poweroff	= efx_pm_poweroff,
	.restore	= efx_pm_resume,
};

static struct pci_driver efx_pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= efx_pci_table,
	.probe		= efx_pci_probe,
	.remove		= efx_pci_remove,
	.driver.pm	= &efx_pm_ops,
	.shutdown	= efx_pci_shutdown,
	.err_handler	= &efx_err_handlers,
#ifdef CONFIG_SFC_SRIOV
	.sriov_configure = efx_pci_sriov_configure,
#endif
};

/**************************************************************************
 *
 * Kernel module interface
 *
 *************************************************************************/

static int __init efx_init_module(void)
{
	int rc;

	printk(KERN_INFO "Solarflare NET driver\n");

	rc = register_netdevice_notifier(&efx_netdev_notifier);
	if (rc)
		goto err_notifier;

	rc = efx_create_reset_workqueue();
	if (rc)
		goto err_reset;

	rc = pci_register_driver(&efx_pci_driver);
	if (rc < 0)
		goto err_pci;

	rc = pci_register_driver(&ef100_pci_driver);
	if (rc < 0)
		goto err_pci_ef100;

	return 0;

 err_pci_ef100:
	pci_unregister_driver(&efx_pci_driver);
 err_pci:
	efx_destroy_reset_workqueue();
 err_reset:
	unregister_netdevice_notifier(&efx_netdev_notifier);
 err_notifier:
	return rc;
}

static void __exit efx_exit_module(void)
{
	printk(KERN_INFO "Solarflare NET driver unloading\n");

	pci_unregister_driver(&ef100_pci_driver);
	pci_unregister_driver(&efx_pci_driver);
	efx_destroy_reset_workqueue();
	unregister_netdevice_notifier(&efx_netdev_notifier);

}

module_init(efx_init_module);
module_exit(efx_exit_module);

MODULE_AUTHOR("Solarflare Communications and "
	      "Michael Brown <mbrown@fensystems.co.uk>");
MODULE_DESCRIPTION("Solarflare network driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, efx_pci_table);
