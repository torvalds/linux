/**
  * This file contains the major functions in WLAN
  * driver. It includes init, exit, open, close and main
  * thread etc..
  */

#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/kthread.h>
#include <linux/kfifo.h>
#include <linux/stddef.h>

#include <net/iw_handler.h>
#include <net/ieee80211.h>

#include "host.h"
#include "decl.h"
#include "dev.h"
#include "wext.h"
#include "debugfs.h"
#include "scan.h"
#include "assoc.h"
#include "cmd.h"

#define DRIVER_RELEASE_VERSION "323.p0"
const char lbs_driver_version[] = "COMM-USB8388-" DRIVER_RELEASE_VERSION
#ifdef  DEBUG
    "-dbg"
#endif
    "";


/* Module parameters */
unsigned int lbs_debug;
EXPORT_SYMBOL_GPL(lbs_debug);
module_param_named(libertas_debug, lbs_debug, int, 0644);


/* This global structure is used to send the confirm_sleep command as
 * fast as possible down to the firmware. */
struct cmd_confirm_sleep confirm_sleep;


#define LBS_TX_PWR_DEFAULT		20	/*100mW */
#define LBS_TX_PWR_US_DEFAULT		20	/*100mW */
#define LBS_TX_PWR_JP_DEFAULT		16	/*50mW */
#define LBS_TX_PWR_FR_DEFAULT		20	/*100mW */
#define LBS_TX_PWR_EMEA_DEFAULT	20	/*100mW */

/* Format { channel, frequency (MHz), maxtxpower } */
/* band: 'B/G', region: USA FCC/Canada IC */
static struct chan_freq_power channel_freq_power_US_BG[] = {
	{1, 2412, LBS_TX_PWR_US_DEFAULT},
	{2, 2417, LBS_TX_PWR_US_DEFAULT},
	{3, 2422, LBS_TX_PWR_US_DEFAULT},
	{4, 2427, LBS_TX_PWR_US_DEFAULT},
	{5, 2432, LBS_TX_PWR_US_DEFAULT},
	{6, 2437, LBS_TX_PWR_US_DEFAULT},
	{7, 2442, LBS_TX_PWR_US_DEFAULT},
	{8, 2447, LBS_TX_PWR_US_DEFAULT},
	{9, 2452, LBS_TX_PWR_US_DEFAULT},
	{10, 2457, LBS_TX_PWR_US_DEFAULT},
	{11, 2462, LBS_TX_PWR_US_DEFAULT}
};

/* band: 'B/G', region: Europe ETSI */
static struct chan_freq_power channel_freq_power_EU_BG[] = {
	{1, 2412, LBS_TX_PWR_EMEA_DEFAULT},
	{2, 2417, LBS_TX_PWR_EMEA_DEFAULT},
	{3, 2422, LBS_TX_PWR_EMEA_DEFAULT},
	{4, 2427, LBS_TX_PWR_EMEA_DEFAULT},
	{5, 2432, LBS_TX_PWR_EMEA_DEFAULT},
	{6, 2437, LBS_TX_PWR_EMEA_DEFAULT},
	{7, 2442, LBS_TX_PWR_EMEA_DEFAULT},
	{8, 2447, LBS_TX_PWR_EMEA_DEFAULT},
	{9, 2452, LBS_TX_PWR_EMEA_DEFAULT},
	{10, 2457, LBS_TX_PWR_EMEA_DEFAULT},
	{11, 2462, LBS_TX_PWR_EMEA_DEFAULT},
	{12, 2467, LBS_TX_PWR_EMEA_DEFAULT},
	{13, 2472, LBS_TX_PWR_EMEA_DEFAULT}
};

/* band: 'B/G', region: Spain */
static struct chan_freq_power channel_freq_power_SPN_BG[] = {
	{10, 2457, LBS_TX_PWR_DEFAULT},
	{11, 2462, LBS_TX_PWR_DEFAULT}
};

/* band: 'B/G', region: France */
static struct chan_freq_power channel_freq_power_FR_BG[] = {
	{10, 2457, LBS_TX_PWR_FR_DEFAULT},
	{11, 2462, LBS_TX_PWR_FR_DEFAULT},
	{12, 2467, LBS_TX_PWR_FR_DEFAULT},
	{13, 2472, LBS_TX_PWR_FR_DEFAULT}
};

/* band: 'B/G', region: Japan */
static struct chan_freq_power channel_freq_power_JPN_BG[] = {
	{1, 2412, LBS_TX_PWR_JP_DEFAULT},
	{2, 2417, LBS_TX_PWR_JP_DEFAULT},
	{3, 2422, LBS_TX_PWR_JP_DEFAULT},
	{4, 2427, LBS_TX_PWR_JP_DEFAULT},
	{5, 2432, LBS_TX_PWR_JP_DEFAULT},
	{6, 2437, LBS_TX_PWR_JP_DEFAULT},
	{7, 2442, LBS_TX_PWR_JP_DEFAULT},
	{8, 2447, LBS_TX_PWR_JP_DEFAULT},
	{9, 2452, LBS_TX_PWR_JP_DEFAULT},
	{10, 2457, LBS_TX_PWR_JP_DEFAULT},
	{11, 2462, LBS_TX_PWR_JP_DEFAULT},
	{12, 2467, LBS_TX_PWR_JP_DEFAULT},
	{13, 2472, LBS_TX_PWR_JP_DEFAULT},
	{14, 2484, LBS_TX_PWR_JP_DEFAULT}
};

/**
 * the structure for channel, frequency and power
 */
struct region_cfp_table {
	u8 region;
	struct chan_freq_power *cfp_BG;
	int cfp_no_BG;
};

/**
 * the structure for the mapping between region and CFP
 */
static struct region_cfp_table region_cfp_table[] = {
	{0x10,			/*US FCC */
	 channel_freq_power_US_BG,
	 ARRAY_SIZE(channel_freq_power_US_BG),
	 }
	,
	{0x20,			/*CANADA IC */
	 channel_freq_power_US_BG,
	 ARRAY_SIZE(channel_freq_power_US_BG),
	 }
	,
	{0x30, /*EU*/ channel_freq_power_EU_BG,
	 ARRAY_SIZE(channel_freq_power_EU_BG),
	 }
	,
	{0x31, /*SPAIN*/ channel_freq_power_SPN_BG,
	 ARRAY_SIZE(channel_freq_power_SPN_BG),
	 }
	,
	{0x32, /*FRANCE*/ channel_freq_power_FR_BG,
	 ARRAY_SIZE(channel_freq_power_FR_BG),
	 }
	,
	{0x40, /*JAPAN*/ channel_freq_power_JPN_BG,
	 ARRAY_SIZE(channel_freq_power_JPN_BG),
	 }
	,
/*Add new region here */
};

/**
 * the table to keep region code
 */
u16 lbs_region_code_to_index[MRVDRV_MAX_REGION_CODE] =
    { 0x10, 0x20, 0x30, 0x31, 0x32, 0x40 };

/**
 * 802.11b/g supported bitrates (in 500Kb/s units)
 */
u8 lbs_bg_rates[MAX_RATES] =
    { 0x02, 0x04, 0x0b, 0x16, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c,
0x00, 0x00 };

/**
 * FW rate table.  FW refers to rates by their index in this table, not by the
 * rate value itself.  Values of 0x00 are
 * reserved positions.
 */
static u8 fw_data_rates[MAX_RATES] =
    { 0x02, 0x04, 0x0B, 0x16, 0x00, 0x0C, 0x12,
      0x18, 0x24, 0x30, 0x48, 0x60, 0x6C, 0x00
};

/**
 *  @brief use index to get the data rate
 *
 *  @param idx                The index of data rate
 *  @return 	   		data rate or 0
 */
u32 lbs_fw_index_to_data_rate(u8 idx)
{
	if (idx >= sizeof(fw_data_rates))
		idx = 0;
	return fw_data_rates[idx];
}

/**
 *  @brief use rate to get the index
 *
 *  @param rate                 data rate
 *  @return 	   		index or 0
 */
u8 lbs_data_rate_to_fw_index(u32 rate)
{
	u8 i;

	if (!rate)
		return 0;

	for (i = 0; i < sizeof(fw_data_rates); i++) {
		if (rate == fw_data_rates[i])
			return i;
	}
	return 0;
}

/**
 * Attributes exported through sysfs
 */

/**
 * @brief Get function for sysfs attribute anycast_mask
 */
static ssize_t lbs_anycast_get(struct device *dev,
		struct device_attribute *attr, char * buf)
{
	struct lbs_private *priv = to_net_dev(dev)->priv;
	struct cmd_ds_mesh_access mesh_access;
	int ret;

	memset(&mesh_access, 0, sizeof(mesh_access));

	ret = lbs_mesh_access(priv, CMD_ACT_MESH_GET_ANYCAST, &mesh_access);
	if (ret)
		return ret;

	return snprintf(buf, 12, "0x%X\n", le32_to_cpu(mesh_access.data[0]));
}

/**
 * @brief Set function for sysfs attribute anycast_mask
 */
static ssize_t lbs_anycast_set(struct device *dev,
		struct device_attribute *attr, const char * buf, size_t count)
{
	struct lbs_private *priv = to_net_dev(dev)->priv;
	struct cmd_ds_mesh_access mesh_access;
	uint32_t datum;
	int ret;

	memset(&mesh_access, 0, sizeof(mesh_access));
	sscanf(buf, "%x", &datum);
	mesh_access.data[0] = cpu_to_le32(datum);

	ret = lbs_mesh_access(priv, CMD_ACT_MESH_SET_ANYCAST, &mesh_access);
	if (ret)
		return ret;

	return strlen(buf);
}

static int lbs_add_rtap(struct lbs_private *priv);
static void lbs_remove_rtap(struct lbs_private *priv);
static int lbs_add_mesh(struct lbs_private *priv);
static void lbs_remove_mesh(struct lbs_private *priv);


/**
 * Get function for sysfs attribute rtap
 */
static ssize_t lbs_rtap_get(struct device *dev,
		struct device_attribute *attr, char * buf)
{
	struct lbs_private *priv = to_net_dev(dev)->priv;
	return snprintf(buf, 5, "0x%X\n", priv->monitormode);
}

/**
 *  Set function for sysfs attribute rtap
 */
static ssize_t lbs_rtap_set(struct device *dev,
		struct device_attribute *attr, const char * buf, size_t count)
{
	int monitor_mode;
	struct lbs_private *priv = to_net_dev(dev)->priv;

	sscanf(buf, "%x", &monitor_mode);
	if (monitor_mode) {
		if (priv->monitormode == monitor_mode)
			return strlen(buf);
		if (!priv->monitormode) {
			if (priv->infra_open || priv->mesh_open)
				return -EBUSY;
			if (priv->mode == IW_MODE_INFRA)
				lbs_cmd_80211_deauthenticate(priv,
							     priv->curbssparams.bssid,
							     WLAN_REASON_DEAUTH_LEAVING);
			else if (priv->mode == IW_MODE_ADHOC)
				lbs_stop_adhoc_network(priv);
			lbs_add_rtap(priv);
		}
		priv->monitormode = monitor_mode;
	} else {
		if (!priv->monitormode)
			return strlen(buf);
		priv->monitormode = 0;
		lbs_remove_rtap(priv);

		if (priv->currenttxskb) {
			dev_kfree_skb_any(priv->currenttxskb);
			priv->currenttxskb = NULL;
		}

		/* Wake queues, command thread, etc. */
		lbs_host_to_card_done(priv);
	}

	lbs_prepare_and_send_command(priv,
			CMD_802_11_MONITOR_MODE, CMD_ACT_SET,
			CMD_OPTION_WAITFORRSP, 0, &priv->monitormode);
	return strlen(buf);
}

/**
 * lbs_rtap attribute to be exported per ethX interface
 * through sysfs (/sys/class/net/ethX/lbs_rtap)
 */
static DEVICE_ATTR(lbs_rtap, 0644, lbs_rtap_get, lbs_rtap_set );

/**
 * Get function for sysfs attribute mesh
 */
static ssize_t lbs_mesh_get(struct device *dev,
		struct device_attribute *attr, char * buf)
{
	struct lbs_private *priv = to_net_dev(dev)->priv;
	return snprintf(buf, 5, "0x%X\n", !!priv->mesh_dev);
}

/**
 *  Set function for sysfs attribute mesh
 */
static ssize_t lbs_mesh_set(struct device *dev,
		struct device_attribute *attr, const char * buf, size_t count)
{
	struct lbs_private *priv = to_net_dev(dev)->priv;
	int enable;
	int ret, action = CMD_ACT_MESH_CONFIG_STOP;

	sscanf(buf, "%x", &enable);
	enable = !!enable;
	if (enable == !!priv->mesh_dev)
		return count;
	if (enable)
		action = CMD_ACT_MESH_CONFIG_START;
	ret = lbs_mesh_config(priv, action, priv->curbssparams.channel);
	if (ret)
		return ret;

	if (enable)
		lbs_add_mesh(priv);
	else
		lbs_remove_mesh(priv);

	return count;
}

/**
 * lbs_mesh attribute to be exported per ethX interface
 * through sysfs (/sys/class/net/ethX/lbs_mesh)
 */
static DEVICE_ATTR(lbs_mesh, 0644, lbs_mesh_get, lbs_mesh_set);

/**
 * anycast_mask attribute to be exported per mshX interface
 * through sysfs (/sys/class/net/mshX/anycast_mask)
 */
static DEVICE_ATTR(anycast_mask, 0644, lbs_anycast_get, lbs_anycast_set);

static struct attribute *lbs_mesh_sysfs_entries[] = {
	&dev_attr_anycast_mask.attr,
	NULL,
};

static struct attribute_group lbs_mesh_attr_group = {
	.attrs = lbs_mesh_sysfs_entries,
};

/**
 *  @brief This function opens the ethX or mshX interface
 *
 *  @param dev     A pointer to net_device structure
 *  @return 	   0 or -EBUSY if monitor mode active
 */
static int lbs_dev_open(struct net_device *dev)
{
	struct lbs_private *priv = (struct lbs_private *) dev->priv ;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_NET);

	spin_lock_irq(&priv->driver_lock);

	if (priv->monitormode) {
		ret = -EBUSY;
		goto out;
	}

	if (dev == priv->mesh_dev) {
		priv->mesh_open = 1;
		priv->mesh_connect_status = LBS_CONNECTED;
		netif_carrier_on(dev);
	} else {
		priv->infra_open = 1;

		if (priv->connect_status == LBS_CONNECTED)
			netif_carrier_on(dev);
		else
			netif_carrier_off(dev);
	}

	if (!priv->tx_pending_len)
		netif_wake_queue(dev);
 out:

	spin_unlock_irq(&priv->driver_lock);
	lbs_deb_leave_args(LBS_DEB_NET, "ret %d", ret);
	return ret;
}

/**
 *  @brief This function closes the mshX interface
 *
 *  @param dev     A pointer to net_device structure
 *  @return 	   0
 */
static int lbs_mesh_stop(struct net_device *dev)
{
	struct lbs_private *priv = (struct lbs_private *) (dev->priv);

	lbs_deb_enter(LBS_DEB_MESH);
	spin_lock_irq(&priv->driver_lock);

	priv->mesh_open = 0;
	priv->mesh_connect_status = LBS_DISCONNECTED;

	netif_stop_queue(dev);
	netif_carrier_off(dev);

	spin_unlock_irq(&priv->driver_lock);

	schedule_work(&priv->mcast_work);

	lbs_deb_leave(LBS_DEB_MESH);
	return 0;
}

/**
 *  @brief This function closes the ethX interface
 *
 *  @param dev     A pointer to net_device structure
 *  @return 	   0
 */
static int lbs_eth_stop(struct net_device *dev)
{
	struct lbs_private *priv = (struct lbs_private *) dev->priv;

	lbs_deb_enter(LBS_DEB_NET);

	spin_lock_irq(&priv->driver_lock);
	priv->infra_open = 0;
	netif_stop_queue(dev);
	spin_unlock_irq(&priv->driver_lock);

	schedule_work(&priv->mcast_work);

	lbs_deb_leave(LBS_DEB_NET);
	return 0;
}

static void lbs_tx_timeout(struct net_device *dev)
{
	struct lbs_private *priv = (struct lbs_private *) dev->priv;

	lbs_deb_enter(LBS_DEB_TX);

	lbs_pr_err("tx watch dog timeout\n");

	dev->trans_start = jiffies;

	if (priv->currenttxskb)
		lbs_send_tx_feedback(priv, 0);

	/* XX: Shouldn't we also call into the hw-specific driver
	   to kick it somehow? */
	lbs_host_to_card_done(priv);

	/* More often than not, this actually happens because the
	   firmware has crapped itself -- rather than just a very
	   busy medium. So send a harmless command, and if/when
	   _that_ times out, we'll kick it in the head. */
	lbs_prepare_and_send_command(priv, CMD_802_11_RSSI, 0,
				     0, 0, NULL);

	lbs_deb_leave(LBS_DEB_TX);
}

void lbs_host_to_card_done(struct lbs_private *priv)
{
	unsigned long flags;

	lbs_deb_enter(LBS_DEB_THREAD);

	spin_lock_irqsave(&priv->driver_lock, flags);

	priv->dnld_sent = DNLD_RES_RECEIVED;

	/* Wake main thread if commands are pending */
	if (!priv->cur_cmd || priv->tx_pending_len > 0)
		wake_up_interruptible(&priv->waitq);

	spin_unlock_irqrestore(&priv->driver_lock, flags);
	lbs_deb_leave(LBS_DEB_THREAD);
}
EXPORT_SYMBOL_GPL(lbs_host_to_card_done);

/**
 *  @brief This function returns the network statistics
 *
 *  @param dev     A pointer to struct lbs_private structure
 *  @return 	   A pointer to net_device_stats structure
 */
static struct net_device_stats *lbs_get_stats(struct net_device *dev)
{
	struct lbs_private *priv = (struct lbs_private *) dev->priv;

	lbs_deb_enter(LBS_DEB_NET);
	return &priv->stats;
}

static int lbs_set_mac_address(struct net_device *dev, void *addr)
{
	int ret = 0;
	struct lbs_private *priv = (struct lbs_private *) dev->priv;
	struct sockaddr *phwaddr = addr;
	struct cmd_ds_802_11_mac_address cmd;

	lbs_deb_enter(LBS_DEB_NET);

	/* In case it was called from the mesh device */
	dev = priv->dev;

	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_ACT_SET);
	memcpy(cmd.macadd, phwaddr->sa_data, ETH_ALEN);

	ret = lbs_cmd_with_response(priv, CMD_802_11_MAC_ADDRESS, &cmd);
	if (ret) {
		lbs_deb_net("set MAC address failed\n");
		goto done;
	}

	memcpy(priv->current_addr, phwaddr->sa_data, ETH_ALEN);
	memcpy(dev->dev_addr, phwaddr->sa_data, ETH_ALEN);
	if (priv->mesh_dev)
		memcpy(priv->mesh_dev->dev_addr, phwaddr->sa_data, ETH_ALEN);

done:
	lbs_deb_leave_args(LBS_DEB_NET, "ret %d", ret);
	return ret;
}


static inline int mac_in_list(unsigned char *list, int list_len,
			      unsigned char *mac)
{
	while (list_len) {
		if (!memcmp(list, mac, ETH_ALEN))
			return 1;
		list += ETH_ALEN;
		list_len--;
	}
	return 0;
}


static int lbs_add_mcast_addrs(struct cmd_ds_mac_multicast_adr *cmd,
			       struct net_device *dev, int nr_addrs)
{
	int i = nr_addrs;
	struct dev_mc_list *mc_list;
	DECLARE_MAC_BUF(mac);

	if ((dev->flags & (IFF_UP|IFF_MULTICAST)) != (IFF_UP|IFF_MULTICAST))
		return nr_addrs;

	netif_addr_lock_bh(dev);
	for (mc_list = dev->mc_list; mc_list; mc_list = mc_list->next) {
		if (mac_in_list(cmd->maclist, nr_addrs, mc_list->dmi_addr)) {
			lbs_deb_net("mcast address %s:%s skipped\n", dev->name,
				    print_mac(mac, mc_list->dmi_addr));
			continue;
		}

		if (i == MRVDRV_MAX_MULTICAST_LIST_SIZE)
			break;
		memcpy(&cmd->maclist[6*i], mc_list->dmi_addr, ETH_ALEN);
		lbs_deb_net("mcast address %s:%s added to filter\n", dev->name,
			    print_mac(mac, mc_list->dmi_addr));
		i++;
	}
	netif_addr_unlock_bh(dev);
	if (mc_list)
		return -EOVERFLOW;

	return i;
}

static void lbs_set_mcast_worker(struct work_struct *work)
{
	struct lbs_private *priv = container_of(work, struct lbs_private, mcast_work);
	struct cmd_ds_mac_multicast_adr mcast_cmd;
	int dev_flags;
	int nr_addrs;
	int old_mac_control = priv->mac_control;

	lbs_deb_enter(LBS_DEB_NET);

	dev_flags = priv->dev->flags;
	if (priv->mesh_dev)
		dev_flags |= priv->mesh_dev->flags;

	if (dev_flags & IFF_PROMISC) {
		priv->mac_control |= CMD_ACT_MAC_PROMISCUOUS_ENABLE;
		priv->mac_control &= ~(CMD_ACT_MAC_ALL_MULTICAST_ENABLE |
				       CMD_ACT_MAC_MULTICAST_ENABLE);
		goto out_set_mac_control;
	} else if (dev_flags & IFF_ALLMULTI) {
	do_allmulti:
		priv->mac_control |= CMD_ACT_MAC_ALL_MULTICAST_ENABLE;
		priv->mac_control &= ~(CMD_ACT_MAC_PROMISCUOUS_ENABLE |
				       CMD_ACT_MAC_MULTICAST_ENABLE);
		goto out_set_mac_control;
	}

	/* Once for priv->dev, again for priv->mesh_dev if it exists */
	nr_addrs = lbs_add_mcast_addrs(&mcast_cmd, priv->dev, 0);
	if (nr_addrs >= 0 && priv->mesh_dev)
		nr_addrs = lbs_add_mcast_addrs(&mcast_cmd, priv->mesh_dev, nr_addrs);
	if (nr_addrs < 0)
		goto do_allmulti;

	if (nr_addrs) {
		int size = offsetof(struct cmd_ds_mac_multicast_adr,
				    maclist[6*nr_addrs]);

		mcast_cmd.action = cpu_to_le16(CMD_ACT_SET);
		mcast_cmd.hdr.size = cpu_to_le16(size);
		mcast_cmd.nr_of_adrs = cpu_to_le16(nr_addrs);

		lbs_cmd_async(priv, CMD_MAC_MULTICAST_ADR, &mcast_cmd.hdr, size);

		priv->mac_control |= CMD_ACT_MAC_MULTICAST_ENABLE;
	} else
		priv->mac_control &= ~CMD_ACT_MAC_MULTICAST_ENABLE;

	priv->mac_control &= ~(CMD_ACT_MAC_PROMISCUOUS_ENABLE |
			       CMD_ACT_MAC_ALL_MULTICAST_ENABLE);
 out_set_mac_control:
	if (priv->mac_control != old_mac_control)
		lbs_set_mac_control(priv);

	lbs_deb_leave(LBS_DEB_NET);
}

static void lbs_set_multicast_list(struct net_device *dev)
{
	struct lbs_private *priv = dev->priv;

	schedule_work(&priv->mcast_work);
}

/**
 *  @brief This function handles the major jobs in the LBS driver.
 *  It handles all events generated by firmware, RX data received
 *  from firmware and TX data sent from kernel.
 *
 *  @param data    A pointer to lbs_thread structure
 *  @return 	   0
 */
static int lbs_thread(void *data)
{
	struct net_device *dev = data;
	struct lbs_private *priv = dev->priv;
	wait_queue_t wait;

	lbs_deb_enter(LBS_DEB_THREAD);

	init_waitqueue_entry(&wait, current);

	for (;;) {
		int shouldsleep;
		u8 resp_idx;

		lbs_deb_thread("1: currenttxskb %p, dnld_sent %d\n",
				priv->currenttxskb, priv->dnld_sent);

		add_wait_queue(&priv->waitq, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock_irq(&priv->driver_lock);

		if (kthread_should_stop())
			shouldsleep = 0;	/* Bye */
		else if (priv->surpriseremoved)
			shouldsleep = 1;	/* We need to wait until we're _told_ to die */
		else if (priv->psstate == PS_STATE_SLEEP)
			shouldsleep = 1;	/* Sleep mode. Nothing we can do till it wakes */
		else if (priv->cmd_timed_out)
			shouldsleep = 0;	/* Command timed out. Recover */
		else if (!priv->fw_ready)
			shouldsleep = 1;	/* Firmware not ready. We're waiting for it */
		else if (priv->dnld_sent)
			shouldsleep = 1;	/* Something is en route to the device already */
		else if (priv->tx_pending_len > 0)
			shouldsleep = 0;	/* We've a packet to send */
		else if (priv->resp_len[priv->resp_idx])
			shouldsleep = 0;	/* We have a command response */
		else if (priv->cur_cmd)
			shouldsleep = 1;	/* Can't send a command; one already running */
		else if (!list_empty(&priv->cmdpendingq))
			shouldsleep = 0;	/* We have a command to send */
		else if (__kfifo_len(priv->event_fifo))
			shouldsleep = 0;	/* We have an event to process */
		else
			shouldsleep = 1;	/* No command */

		if (shouldsleep) {
			lbs_deb_thread("sleeping, connect_status %d, "
				"psmode %d, psstate %d\n",
				priv->connect_status,
				priv->psmode, priv->psstate);
			spin_unlock_irq(&priv->driver_lock);
			schedule();
		} else
			spin_unlock_irq(&priv->driver_lock);

		lbs_deb_thread("2: currenttxskb %p, dnld_send %d\n",
			       priv->currenttxskb, priv->dnld_sent);

		set_current_state(TASK_RUNNING);
		remove_wait_queue(&priv->waitq, &wait);

		lbs_deb_thread("3: currenttxskb %p, dnld_sent %d\n",
			       priv->currenttxskb, priv->dnld_sent);

		if (kthread_should_stop()) {
			lbs_deb_thread("break from main thread\n");
			break;
		}

		if (priv->surpriseremoved) {
			lbs_deb_thread("adapter removed; waiting to die...\n");
			continue;
		}

		lbs_deb_thread("4: currenttxskb %p, dnld_sent %d\n",
		       priv->currenttxskb, priv->dnld_sent);

		/* Process any pending command response */
		spin_lock_irq(&priv->driver_lock);
		resp_idx = priv->resp_idx;
		if (priv->resp_len[resp_idx]) {
			spin_unlock_irq(&priv->driver_lock);
			lbs_process_command_response(priv,
				priv->resp_buf[resp_idx],
				priv->resp_len[resp_idx]);
			spin_lock_irq(&priv->driver_lock);
			priv->resp_len[resp_idx] = 0;
		}
		spin_unlock_irq(&priv->driver_lock);

		/* command timeout stuff */
		if (priv->cmd_timed_out && priv->cur_cmd) {
			struct cmd_ctrl_node *cmdnode = priv->cur_cmd;

			if (++priv->nr_retries > 3) {
				lbs_pr_info("Excessive timeouts submitting "
					"command 0x%04x\n",
					le16_to_cpu(cmdnode->cmdbuf->command));
				lbs_complete_command(priv, cmdnode, -ETIMEDOUT);
				priv->nr_retries = 0;
				if (priv->reset_card)
					priv->reset_card(priv);
			} else {
				priv->cur_cmd = NULL;
				priv->dnld_sent = DNLD_RES_RECEIVED;
				lbs_pr_info("requeueing command 0x%04x due "
					"to timeout (#%d)\n",
					le16_to_cpu(cmdnode->cmdbuf->command),
					priv->nr_retries);

				/* Stick it back at the _top_ of the pending queue
				   for immediate resubmission */
				list_add(&cmdnode->list, &priv->cmdpendingq);
			}
		}
		priv->cmd_timed_out = 0;

		/* Process hardware events, e.g. card removed, link lost */
		spin_lock_irq(&priv->driver_lock);
		while (__kfifo_len(priv->event_fifo)) {
			u32 event;

			__kfifo_get(priv->event_fifo, (unsigned char *) &event,
				sizeof(event));
			spin_unlock_irq(&priv->driver_lock);
			lbs_process_event(priv, event);
			spin_lock_irq(&priv->driver_lock);
		}
		spin_unlock_irq(&priv->driver_lock);

		if (!priv->fw_ready)
			continue;

		/* Check if we need to confirm Sleep Request received previously */
		if (priv->psstate == PS_STATE_PRE_SLEEP &&
		    !priv->dnld_sent && !priv->cur_cmd) {
			if (priv->connect_status == LBS_CONNECTED) {
				lbs_deb_thread("pre-sleep, currenttxskb %p, "
					"dnld_sent %d, cur_cmd %p\n",
					priv->currenttxskb, priv->dnld_sent,
					priv->cur_cmd);

				lbs_ps_confirm_sleep(priv);
			} else {
				/* workaround for firmware sending
				 * deauth/linkloss event immediately
				 * after sleep request; remove this
				 * after firmware fixes it
				 */
				priv->psstate = PS_STATE_AWAKE;
				lbs_pr_alert("ignore PS_SleepConfirm in "
					"non-connected state\n");
			}
		}

		/* The PS state is changed during processing of Sleep Request
		 * event above
		 */
		if ((priv->psstate == PS_STATE_SLEEP) ||
		    (priv->psstate == PS_STATE_PRE_SLEEP))
			continue;

		/* Execute the next command */
		if (!priv->dnld_sent && !priv->cur_cmd)
			lbs_execute_next_command(priv);

		/* Wake-up command waiters which can't sleep in
		 * lbs_prepare_and_send_command
		 */
		if (!list_empty(&priv->cmdpendingq))
			wake_up_all(&priv->cmd_pending);

		spin_lock_irq(&priv->driver_lock);
		if (!priv->dnld_sent && priv->tx_pending_len > 0) {
			int ret = priv->hw_host_to_card(priv, MVMS_DAT,
							priv->tx_pending_buf,
							priv->tx_pending_len);
			if (ret) {
				lbs_deb_tx("host_to_card failed %d\n", ret);
				priv->dnld_sent = DNLD_RES_RECEIVED;
			}
			priv->tx_pending_len = 0;
			if (!priv->currenttxskb) {
				/* We can wake the queues immediately if we aren't
				   waiting for TX feedback */
				if (priv->connect_status == LBS_CONNECTED)
					netif_wake_queue(priv->dev);
				if (priv->mesh_dev &&
				    priv->mesh_connect_status == LBS_CONNECTED)
					netif_wake_queue(priv->mesh_dev);
			}
		}
		spin_unlock_irq(&priv->driver_lock);
	}

	del_timer(&priv->command_timer);
	wake_up_all(&priv->cmd_pending);

	lbs_deb_leave(LBS_DEB_THREAD);
	return 0;
}

static int lbs_suspend_callback(struct lbs_private *priv, unsigned long dummy,
				struct cmd_header *cmd)
{
	lbs_deb_enter(LBS_DEB_FW);

	netif_device_detach(priv->dev);
	if (priv->mesh_dev)
		netif_device_detach(priv->mesh_dev);

	priv->fw_ready = 0;
	lbs_deb_leave(LBS_DEB_FW);
	return 0;
}

int lbs_suspend(struct lbs_private *priv)
{
	struct cmd_header cmd;
	int ret;

	lbs_deb_enter(LBS_DEB_FW);

	if (priv->wol_criteria == 0xffffffff) {
		lbs_pr_info("Suspend attempt without configuring wake params!\n");
		return -EINVAL;
	}

	memset(&cmd, 0, sizeof(cmd));

	ret = __lbs_cmd(priv, CMD_802_11_HOST_SLEEP_ACTIVATE, &cmd,
			sizeof(cmd), lbs_suspend_callback, 0);
	if (ret)
		lbs_pr_info("HOST_SLEEP_ACTIVATE failed: %d\n", ret);

	lbs_deb_leave_args(LBS_DEB_FW, "ret %d", ret);
	return ret;
}
EXPORT_SYMBOL_GPL(lbs_suspend);

void lbs_resume(struct lbs_private *priv)
{
	lbs_deb_enter(LBS_DEB_FW);

	priv->fw_ready = 1;

	/* Firmware doesn't seem to give us RX packets any more
	   until we send it some command. Might as well update */
	lbs_prepare_and_send_command(priv, CMD_802_11_RSSI, 0,
				     0, 0, NULL);

	netif_device_attach(priv->dev);
	if (priv->mesh_dev)
		netif_device_attach(priv->mesh_dev);

	lbs_deb_leave(LBS_DEB_FW);
}
EXPORT_SYMBOL_GPL(lbs_resume);

/**
 *  @brief This function downloads firmware image, gets
 *  HW spec from firmware and set basic parameters to
 *  firmware.
 *
 *  @param priv    A pointer to struct lbs_private structure
 *  @return 	   0 or -1
 */
static int lbs_setup_firmware(struct lbs_private *priv)
{
	int ret = -1;
	s16 curlevel = 0, minlevel = 0, maxlevel = 0;

	lbs_deb_enter(LBS_DEB_FW);

	/* Read MAC address from firmware */
	memset(priv->current_addr, 0xff, ETH_ALEN);
	ret = lbs_update_hw_spec(priv);
	if (ret)
		goto done;

	/* Read power levels if available */
	ret = lbs_get_tx_power(priv, &curlevel, &minlevel, &maxlevel);
	if (ret == 0) {
		priv->txpower_cur = curlevel;
		priv->txpower_min = minlevel;
		priv->txpower_max = maxlevel;
	}

	lbs_set_mac_control(priv);
done:
	lbs_deb_leave_args(LBS_DEB_FW, "ret %d", ret);
	return ret;
}

/**
 *  This function handles the timeout of command sending.
 *  It will re-send the same command again.
 */
static void command_timer_fn(unsigned long data)
{
	struct lbs_private *priv = (struct lbs_private *)data;
	unsigned long flags;

	lbs_deb_enter(LBS_DEB_CMD);
	spin_lock_irqsave(&priv->driver_lock, flags);

	if (!priv->cur_cmd)
		goto out;

	lbs_pr_info("command 0x%04x timed out\n",
		le16_to_cpu(priv->cur_cmd->cmdbuf->command));

	priv->cmd_timed_out = 1;
	wake_up_interruptible(&priv->waitq);
out:
	spin_unlock_irqrestore(&priv->driver_lock, flags);
	lbs_deb_leave(LBS_DEB_CMD);
}

static void lbs_sync_channel_worker(struct work_struct *work)
{
	struct lbs_private *priv = container_of(work, struct lbs_private,
		sync_channel);

	lbs_deb_enter(LBS_DEB_MAIN);
	if (lbs_update_channel(priv))
		lbs_pr_info("Channel synchronization failed.");
	lbs_deb_leave(LBS_DEB_MAIN);
}


static int lbs_init_adapter(struct lbs_private *priv)
{
	size_t bufsize;
	int i, ret = 0;

	lbs_deb_enter(LBS_DEB_MAIN);

	/* Allocate buffer to store the BSSID list */
	bufsize = MAX_NETWORK_COUNT * sizeof(struct bss_descriptor);
	priv->networks = kzalloc(bufsize, GFP_KERNEL);
	if (!priv->networks) {
		lbs_pr_err("Out of memory allocating beacons\n");
		ret = -1;
		goto out;
	}

	/* Initialize scan result lists */
	INIT_LIST_HEAD(&priv->network_free_list);
	INIT_LIST_HEAD(&priv->network_list);
	for (i = 0; i < MAX_NETWORK_COUNT; i++) {
		list_add_tail(&priv->networks[i].list,
			      &priv->network_free_list);
	}

	memset(priv->current_addr, 0xff, ETH_ALEN);

	priv->connect_status = LBS_DISCONNECTED;
	priv->mesh_connect_status = LBS_DISCONNECTED;
	priv->secinfo.auth_mode = IW_AUTH_ALG_OPEN_SYSTEM;
	priv->mode = IW_MODE_INFRA;
	priv->curbssparams.channel = DEFAULT_AD_HOC_CHANNEL;
	priv->mac_control = CMD_ACT_MAC_RX_ON | CMD_ACT_MAC_TX_ON;
	priv->radio_on = 1;
	priv->enablehwauto = 1;
	priv->capability = WLAN_CAPABILITY_SHORT_PREAMBLE;
	priv->psmode = LBS802_11POWERMODECAM;
	priv->psstate = PS_STATE_FULL_POWER;

	mutex_init(&priv->lock);

	setup_timer(&priv->command_timer, command_timer_fn,
		(unsigned long)priv);

	INIT_LIST_HEAD(&priv->cmdfreeq);
	INIT_LIST_HEAD(&priv->cmdpendingq);

	spin_lock_init(&priv->driver_lock);
	init_waitqueue_head(&priv->cmd_pending);

	/* Allocate the command buffers */
	if (lbs_allocate_cmd_buffer(priv)) {
		lbs_pr_err("Out of memory allocating command buffers\n");
		ret = -ENOMEM;
		goto out;
	}
	priv->resp_idx = 0;
	priv->resp_len[0] = priv->resp_len[1] = 0;

	/* Create the event FIFO */
	priv->event_fifo = kfifo_alloc(sizeof(u32) * 16, GFP_KERNEL, NULL);
	if (IS_ERR(priv->event_fifo)) {
		lbs_pr_err("Out of memory allocating event FIFO buffer\n");
		ret = -ENOMEM;
		goto out;
	}

out:
	lbs_deb_leave_args(LBS_DEB_MAIN, "ret %d", ret);

	return ret;
}

static void lbs_free_adapter(struct lbs_private *priv)
{
	lbs_deb_enter(LBS_DEB_MAIN);

	lbs_free_cmd_buffer(priv);
	if (priv->event_fifo)
		kfifo_free(priv->event_fifo);
	del_timer(&priv->command_timer);
	kfree(priv->networks);
	priv->networks = NULL;

	lbs_deb_leave(LBS_DEB_MAIN);
}

/**
 * @brief This function adds the card. it will probe the
 * card, allocate the lbs_priv and initialize the device.
 *
 *  @param card    A pointer to card
 *  @return 	   A pointer to struct lbs_private structure
 */
struct lbs_private *lbs_add_card(void *card, struct device *dmdev)
{
	struct net_device *dev = NULL;
	struct lbs_private *priv = NULL;

	lbs_deb_enter(LBS_DEB_MAIN);

	/* Allocate an Ethernet device and register it */
	dev = alloc_etherdev(sizeof(struct lbs_private));
	if (!dev) {
		lbs_pr_err("init ethX device failed\n");
		goto done;
	}
	priv = dev->priv;

	if (lbs_init_adapter(priv)) {
		lbs_pr_err("failed to initialize adapter structure.\n");
		goto err_init_adapter;
	}

	priv->dev = dev;
	priv->card = card;
	priv->mesh_open = 0;
	priv->infra_open = 0;

	/* Setup the OS Interface to our functions */
	dev->open = lbs_dev_open;
	dev->hard_start_xmit = lbs_hard_start_xmit;
	dev->stop = lbs_eth_stop;
	dev->set_mac_address = lbs_set_mac_address;
	dev->tx_timeout = lbs_tx_timeout;
	dev->get_stats = lbs_get_stats;
	dev->watchdog_timeo = 5 * HZ;
	dev->ethtool_ops = &lbs_ethtool_ops;
#ifdef	WIRELESS_EXT
	dev->wireless_handlers = (struct iw_handler_def *)&lbs_handler_def;
#endif
	dev->flags |= IFF_BROADCAST | IFF_MULTICAST;
	dev->set_multicast_list = lbs_set_multicast_list;

	SET_NETDEV_DEV(dev, dmdev);

	priv->rtap_net_dev = NULL;

	lbs_deb_thread("Starting main thread...\n");
	init_waitqueue_head(&priv->waitq);
	priv->main_thread = kthread_run(lbs_thread, dev, "lbs_main");
	if (IS_ERR(priv->main_thread)) {
		lbs_deb_thread("Error creating main thread.\n");
		goto err_init_adapter;
	}

	priv->work_thread = create_singlethread_workqueue("lbs_worker");
	INIT_DELAYED_WORK(&priv->assoc_work, lbs_association_worker);
	INIT_DELAYED_WORK(&priv->scan_work, lbs_scan_worker);
	INIT_WORK(&priv->mcast_work, lbs_set_mcast_worker);
	INIT_WORK(&priv->sync_channel, lbs_sync_channel_worker);

	sprintf(priv->mesh_ssid, "mesh");
	priv->mesh_ssid_len = 4;

	priv->wol_criteria = 0xffffffff;
	priv->wol_gpio = 0xff;

	goto done;

err_init_adapter:
	lbs_free_adapter(priv);
	free_netdev(dev);
	priv = NULL;

done:
	lbs_deb_leave_args(LBS_DEB_MAIN, "priv %p", priv);
	return priv;
}
EXPORT_SYMBOL_GPL(lbs_add_card);


void lbs_remove_card(struct lbs_private *priv)
{
	struct net_device *dev = priv->dev;
	union iwreq_data wrqu;

	lbs_deb_enter(LBS_DEB_MAIN);

	lbs_remove_mesh(priv);
	lbs_remove_rtap(priv);

	dev = priv->dev;

	cancel_delayed_work_sync(&priv->scan_work);
	cancel_delayed_work_sync(&priv->assoc_work);
	cancel_work_sync(&priv->mcast_work);
	destroy_workqueue(priv->work_thread);

	if (priv->psmode == LBS802_11POWERMODEMAX_PSP) {
		priv->psmode = LBS802_11POWERMODECAM;
		lbs_ps_wakeup(priv, CMD_OPTION_WAITFORRSP);
	}

	memset(wrqu.ap_addr.sa_data, 0xaa, ETH_ALEN);
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(priv->dev, SIOCGIWAP, &wrqu, NULL);

	/* Stop the thread servicing the interrupts */
	priv->surpriseremoved = 1;
	kthread_stop(priv->main_thread);

	lbs_free_adapter(priv);

	priv->dev = NULL;
	free_netdev(dev);

	lbs_deb_leave(LBS_DEB_MAIN);
}
EXPORT_SYMBOL_GPL(lbs_remove_card);


int lbs_start_card(struct lbs_private *priv)
{
	struct net_device *dev = priv->dev;
	int ret = -1;

	lbs_deb_enter(LBS_DEB_MAIN);

	/* poke the firmware */
	ret = lbs_setup_firmware(priv);
	if (ret)
		goto done;

	/* init 802.11d */
	lbs_init_11d(priv);

	if (register_netdev(dev)) {
		lbs_pr_err("cannot register ethX device\n");
		goto done;
	}

	lbs_update_channel(priv);

	/* 5.0.16p0 is known to NOT support any mesh */
	if (priv->fwrelease > 0x05001000) {
		/* Enable mesh, if supported, and work out which TLV it uses.
		   0x100 + 291 is an unofficial value used in 5.110.20.pXX
		   0x100 + 37 is the official value used in 5.110.21.pXX
		   but we check them in that order because 20.pXX doesn't
		   give an error -- it just silently fails. */

		/* 5.110.20.pXX firmware will fail the command if the channel
		   doesn't match the existing channel. But only if the TLV
		   is correct. If the channel is wrong, _BOTH_ versions will
		   give an error to 0x100+291, and allow 0x100+37 to succeed.
		   It's just that 5.110.20.pXX will not have done anything
		   useful */

		priv->mesh_tlv = 0x100 + 291;
		if (lbs_mesh_config(priv, CMD_ACT_MESH_CONFIG_START,
				    priv->curbssparams.channel)) {
			priv->mesh_tlv = 0x100 + 37;
			if (lbs_mesh_config(priv, CMD_ACT_MESH_CONFIG_START,
					    priv->curbssparams.channel))
				priv->mesh_tlv = 0;
		}
		if (priv->mesh_tlv) {
			lbs_add_mesh(priv);

			if (device_create_file(&dev->dev, &dev_attr_lbs_mesh))
				lbs_pr_err("cannot register lbs_mesh attribute\n");

			/* While rtap isn't related to mesh, only mesh-enabled
			 * firmware implements the rtap functionality via
			 * CMD_802_11_MONITOR_MODE.
			 */
			if (device_create_file(&dev->dev, &dev_attr_lbs_rtap))
				lbs_pr_err("cannot register lbs_rtap attribute\n");
		}
	}

	lbs_debugfs_init_one(priv, dev);

	lbs_pr_info("%s: Marvell WLAN 802.11 adapter\n", dev->name);

	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_MAIN, "ret %d", ret);
	return ret;
}
EXPORT_SYMBOL_GPL(lbs_start_card);


void lbs_stop_card(struct lbs_private *priv)
{
	struct net_device *dev = priv->dev;
	struct cmd_ctrl_node *cmdnode;
	unsigned long flags;

	lbs_deb_enter(LBS_DEB_MAIN);

	if (!priv)
		goto out;

	netif_stop_queue(priv->dev);
	netif_carrier_off(priv->dev);

	lbs_debugfs_remove_one(priv);
	if (priv->mesh_tlv) {
		device_remove_file(&dev->dev, &dev_attr_lbs_mesh);
		device_remove_file(&dev->dev, &dev_attr_lbs_rtap);
	}

	/* Flush pending command nodes */
	del_timer_sync(&priv->command_timer);
	spin_lock_irqsave(&priv->driver_lock, flags);
	list_for_each_entry(cmdnode, &priv->cmdpendingq, list) {
		cmdnode->result = -ENOENT;
		cmdnode->cmdwaitqwoken = 1;
		wake_up_interruptible(&cmdnode->cmdwait_q);
	}
	spin_unlock_irqrestore(&priv->driver_lock, flags);

	unregister_netdev(dev);

out:
	lbs_deb_leave(LBS_DEB_MAIN);
}
EXPORT_SYMBOL_GPL(lbs_stop_card);


/**
 * @brief This function adds mshX interface
 *
 *  @param priv    A pointer to the struct lbs_private structure
 *  @return 	   0 if successful, -X otherwise
 */
static int lbs_add_mesh(struct lbs_private *priv)
{
	struct net_device *mesh_dev = NULL;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_MESH);

	/* Allocate a virtual mesh device */
	if (!(mesh_dev = alloc_netdev(0, "msh%d", ether_setup))) {
		lbs_deb_mesh("init mshX device failed\n");
		ret = -ENOMEM;
		goto done;
	}
	mesh_dev->priv = priv;
	priv->mesh_dev = mesh_dev;

	mesh_dev->open = lbs_dev_open;
	mesh_dev->hard_start_xmit = lbs_hard_start_xmit;
	mesh_dev->stop = lbs_mesh_stop;
	mesh_dev->get_stats = lbs_get_stats;
	mesh_dev->set_mac_address = lbs_set_mac_address;
	mesh_dev->ethtool_ops = &lbs_ethtool_ops;
	memcpy(mesh_dev->dev_addr, priv->dev->dev_addr,
			sizeof(priv->dev->dev_addr));

	SET_NETDEV_DEV(priv->mesh_dev, priv->dev->dev.parent);

#ifdef	WIRELESS_EXT
	mesh_dev->wireless_handlers = (struct iw_handler_def *)&mesh_handler_def;
#endif
	mesh_dev->flags |= IFF_BROADCAST | IFF_MULTICAST;
	mesh_dev->set_multicast_list = lbs_set_multicast_list;
	/* Register virtual mesh interface */
	ret = register_netdev(mesh_dev);
	if (ret) {
		lbs_pr_err("cannot register mshX virtual interface\n");
		goto err_free;
	}

	ret = sysfs_create_group(&(mesh_dev->dev.kobj), &lbs_mesh_attr_group);
	if (ret)
		goto err_unregister;

	lbs_persist_config_init(mesh_dev);

	/* Everything successful */
	ret = 0;
	goto done;

err_unregister:
	unregister_netdev(mesh_dev);

err_free:
	free_netdev(mesh_dev);

done:
	lbs_deb_leave_args(LBS_DEB_MESH, "ret %d", ret);
	return ret;
}

static void lbs_remove_mesh(struct lbs_private *priv)
{
	struct net_device *mesh_dev;


	mesh_dev = priv->mesh_dev;
	if (!mesh_dev)
		return;

	lbs_deb_enter(LBS_DEB_MESH);
	netif_stop_queue(mesh_dev);
	netif_carrier_off(mesh_dev);
	sysfs_remove_group(&(mesh_dev->dev.kobj), &lbs_mesh_attr_group);
	lbs_persist_config_remove(mesh_dev);
	unregister_netdev(mesh_dev);
	priv->mesh_dev = NULL;
	free_netdev(mesh_dev);
	lbs_deb_leave(LBS_DEB_MESH);
}

/**
 *  @brief This function finds the CFP in
 *  region_cfp_table based on region and band parameter.
 *
 *  @param region  The region code
 *  @param band	   The band
 *  @param cfp_no  A pointer to CFP number
 *  @return 	   A pointer to CFP
 */
struct chan_freq_power *lbs_get_region_cfp_table(u8 region, int *cfp_no)
{
	int i, end;

	lbs_deb_enter(LBS_DEB_MAIN);

	end = ARRAY_SIZE(region_cfp_table);

	for (i = 0; i < end ; i++) {
		lbs_deb_main("region_cfp_table[i].region=%d\n",
			region_cfp_table[i].region);
		if (region_cfp_table[i].region == region) {
			*cfp_no = region_cfp_table[i].cfp_no_BG;
			lbs_deb_leave(LBS_DEB_MAIN);
			return region_cfp_table[i].cfp_BG;
		}
	}

	lbs_deb_leave_args(LBS_DEB_MAIN, "ret NULL");
	return NULL;
}

int lbs_set_regiontable(struct lbs_private *priv, u8 region, u8 band)
{
	int ret = 0;
	int i = 0;

	struct chan_freq_power *cfp;
	int cfp_no;

	lbs_deb_enter(LBS_DEB_MAIN);

	memset(priv->region_channel, 0, sizeof(priv->region_channel));

	cfp = lbs_get_region_cfp_table(region, &cfp_no);
	if (cfp != NULL) {
		priv->region_channel[i].nrcfp = cfp_no;
		priv->region_channel[i].CFP = cfp;
	} else {
		lbs_deb_main("wrong region code %#x in band B/G\n",
		       region);
		ret = -1;
		goto out;
	}
	priv->region_channel[i].valid = 1;
	priv->region_channel[i].region = region;
	priv->region_channel[i].band = band;
	i++;
out:
	lbs_deb_leave_args(LBS_DEB_MAIN, "ret %d", ret);
	return ret;
}

void lbs_queue_event(struct lbs_private *priv, u32 event)
{
	unsigned long flags;

	lbs_deb_enter(LBS_DEB_THREAD);
	spin_lock_irqsave(&priv->driver_lock, flags);

	if (priv->psstate == PS_STATE_SLEEP)
		priv->psstate = PS_STATE_AWAKE;

	__kfifo_put(priv->event_fifo, (unsigned char *) &event, sizeof(u32));

	wake_up_interruptible(&priv->waitq);

	spin_unlock_irqrestore(&priv->driver_lock, flags);
	lbs_deb_leave(LBS_DEB_THREAD);
}
EXPORT_SYMBOL_GPL(lbs_queue_event);

void lbs_notify_command_response(struct lbs_private *priv, u8 resp_idx)
{
	lbs_deb_enter(LBS_DEB_THREAD);

	if (priv->psstate == PS_STATE_SLEEP)
		priv->psstate = PS_STATE_AWAKE;

	/* Swap buffers by flipping the response index */
	BUG_ON(resp_idx > 1);
	priv->resp_idx = resp_idx;

	wake_up_interruptible(&priv->waitq);

	lbs_deb_leave(LBS_DEB_THREAD);
}
EXPORT_SYMBOL_GPL(lbs_notify_command_response);

static int __init lbs_init_module(void)
{
	lbs_deb_enter(LBS_DEB_MAIN);
	memset(&confirm_sleep, 0, sizeof(confirm_sleep));
	confirm_sleep.hdr.command = cpu_to_le16(CMD_802_11_PS_MODE);
	confirm_sleep.hdr.size = cpu_to_le16(sizeof(confirm_sleep));
	confirm_sleep.action = cpu_to_le16(CMD_SUBCMD_SLEEP_CONFIRMED);
	lbs_debugfs_init();
	lbs_deb_leave(LBS_DEB_MAIN);
	return 0;
}

static void __exit lbs_exit_module(void)
{
	lbs_deb_enter(LBS_DEB_MAIN);
	lbs_debugfs_remove();
	lbs_deb_leave(LBS_DEB_MAIN);
}

/*
 * rtap interface support fuctions
 */

static int lbs_rtap_open(struct net_device *dev)
{
	/* Yes, _stop_ the queue. Because we don't support injection */
	lbs_deb_enter(LBS_DEB_MAIN);
	netif_carrier_off(dev);
	netif_stop_queue(dev);
	lbs_deb_leave(LBS_DEB_LEAVE);
	return 0;
}

static int lbs_rtap_stop(struct net_device *dev)
{
	lbs_deb_enter(LBS_DEB_MAIN);
	lbs_deb_leave(LBS_DEB_MAIN);
	return 0;
}

static int lbs_rtap_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	netif_stop_queue(dev);
	return NETDEV_TX_BUSY;
}

static struct net_device_stats *lbs_rtap_get_stats(struct net_device *dev)
{
	struct lbs_private *priv = dev->priv;
	lbs_deb_enter(LBS_DEB_NET);
	return &priv->stats;
}


static void lbs_remove_rtap(struct lbs_private *priv)
{
	lbs_deb_enter(LBS_DEB_MAIN);
	if (priv->rtap_net_dev == NULL)
		goto out;
	unregister_netdev(priv->rtap_net_dev);
	free_netdev(priv->rtap_net_dev);
	priv->rtap_net_dev = NULL;
out:
	lbs_deb_leave(LBS_DEB_MAIN);
}

static int lbs_add_rtap(struct lbs_private *priv)
{
	int ret = 0;
	struct net_device *rtap_dev;

	lbs_deb_enter(LBS_DEB_MAIN);
	if (priv->rtap_net_dev) {
		ret = -EPERM;
		goto out;
	}

	rtap_dev = alloc_netdev(0, "rtap%d", ether_setup);
	if (rtap_dev == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	memcpy(rtap_dev->dev_addr, priv->current_addr, ETH_ALEN);
	rtap_dev->type = ARPHRD_IEEE80211_RADIOTAP;
	rtap_dev->open = lbs_rtap_open;
	rtap_dev->stop = lbs_rtap_stop;
	rtap_dev->get_stats = lbs_rtap_get_stats;
	rtap_dev->hard_start_xmit = lbs_rtap_hard_start_xmit;
	rtap_dev->priv = priv;
	SET_NETDEV_DEV(rtap_dev, priv->dev->dev.parent);

	ret = register_netdev(rtap_dev);
	if (ret) {
		free_netdev(rtap_dev);
		goto out;
	}
	priv->rtap_net_dev = rtap_dev;

out:
	lbs_deb_leave_args(LBS_DEB_MAIN, "ret %d", ret);
	return ret;
}

#ifndef CONFIG_IEEE80211
const char *escape_essid(const char *essid, u8 essid_len)
{
	static char escaped[IW_ESSID_MAX_SIZE * 2 + 1];
	const char *s = essid;
	char *d = escaped;

	if (ieee80211_is_empty_essid(essid, essid_len)) {
		memcpy(escaped, "<hidden>", sizeof("<hidden>"));
		return escaped;
	}

	essid_len = min(essid_len, (u8) IW_ESSID_MAX_SIZE);
	while (essid_len--) {
		if (*s == '\0') {
			*d++ = '\\';
			*d++ = '0';
			s++;
		} else {
			*d++ = *s++;
		}
	}
	*d = '\0';
	return escaped;
}
#endif

module_init(lbs_init_module);
module_exit(lbs_exit_module);

MODULE_DESCRIPTION("Libertas WLAN Driver Library");
MODULE_AUTHOR("Marvell International Ltd.");
MODULE_LICENSE("GPL");
