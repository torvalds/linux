/**
  * This file contains the major functions in WLAN
  * driver. It includes init, exit, open, close and main
  * thread etc..
  */

#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/kthread.h>

#include <net/iw_handler.h>
#include <net/ieee80211.h>

#include "host.h"
#include "decl.h"
#include "dev.h"
#include "wext.h"
#include "debugfs.h"
#include "assoc.h"
#include "join.h"

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
	struct cmd_ds_mesh_access mesh_access;

	memset(&mesh_access, 0, sizeof(mesh_access));
	lbs_prepare_and_send_command(to_net_dev(dev)->priv,
			CMD_MESH_ACCESS,
			CMD_ACT_MESH_GET_ANYCAST,
			CMD_OPTION_WAITFORRSP, 0, (void *)&mesh_access);

	return snprintf(buf, 12, "0x%X\n", le32_to_cpu(mesh_access.data[0]));
}

/**
 * @brief Set function for sysfs attribute anycast_mask
 */
static ssize_t lbs_anycast_set(struct device *dev,
		struct device_attribute *attr, const char * buf, size_t count)
{
	struct cmd_ds_mesh_access mesh_access;
	uint32_t datum;

	memset(&mesh_access, 0, sizeof(mesh_access));
	sscanf(buf, "%x", &datum);
	mesh_access.data[0] = cpu_to_le32(datum);

	lbs_prepare_and_send_command((to_net_dev(dev))->priv,
			CMD_MESH_ACCESS,
			CMD_ACT_MESH_SET_ANYCAST,
			CMD_OPTION_WAITFORRSP, 0, (void *)&mesh_access);
	return strlen(buf);
}

int lbs_add_rtap(lbs_private *priv);
void lbs_remove_rtap(lbs_private *priv);

/**
 * Get function for sysfs attribute rtap
 */
static ssize_t lbs_rtap_get(struct device *dev,
		struct device_attribute *attr, char * buf)
{
	lbs_private *priv = (lbs_private *) (to_net_dev(dev))->priv;
	lbs_adapter *adapter = priv->adapter;
	return snprintf(buf, 5, "0x%X\n", adapter->monitormode);
}

/**
 *  Set function for sysfs attribute rtap
 */
static ssize_t lbs_rtap_set(struct device *dev,
		struct device_attribute *attr, const char * buf, size_t count)
{
	int monitor_mode;
	lbs_private *priv = (lbs_private *) (to_net_dev(dev))->priv;
	lbs_adapter *adapter = priv->adapter;

	sscanf(buf, "%x", &monitor_mode);
	if (monitor_mode != LBS_MONITOR_OFF) {
		if(adapter->monitormode == monitor_mode)
			return strlen(buf);
		if (adapter->monitormode == LBS_MONITOR_OFF) {
			if (adapter->mode == IW_MODE_INFRA)
				lbs_send_deauthentication(priv);
			else if (adapter->mode == IW_MODE_ADHOC)
				lbs_stop_adhoc_network(priv);
			lbs_add_rtap(priv);
		}
		adapter->monitormode = monitor_mode;
	}

	else {
		if (adapter->monitormode == LBS_MONITOR_OFF)
			return strlen(buf);
		adapter->monitormode = LBS_MONITOR_OFF;
		lbs_remove_rtap(priv);
		netif_wake_queue(priv->dev);
		netif_wake_queue(priv->mesh_dev);
	}

	lbs_prepare_and_send_command(priv,
			CMD_802_11_MONITOR_MODE, CMD_ACT_SET,
			CMD_OPTION_WAITFORRSP, 0, &adapter->monitormode);
	return strlen(buf);
}

/**
 * lbs_rtap attribute to be exported per mshX interface
 * through sysfs (/sys/class/net/mshX/libertas-rtap)
 */
static DEVICE_ATTR(lbs_rtap, 0644, lbs_rtap_get,
		lbs_rtap_set );

/**
 * anycast_mask attribute to be exported per mshX interface
 * through sysfs (/sys/class/net/mshX/anycast_mask)
 */
static DEVICE_ATTR(anycast_mask, 0644, lbs_anycast_get, lbs_anycast_set);

static ssize_t lbs_autostart_enabled_get(struct device *dev,
		struct device_attribute *attr, char * buf)
{
	struct cmd_ds_mesh_access mesh_access;

	memset(&mesh_access, 0, sizeof(mesh_access));
	lbs_prepare_and_send_command(to_net_dev(dev)->priv,
			CMD_MESH_ACCESS,
			CMD_ACT_MESH_GET_AUTOSTART_ENABLED,
			CMD_OPTION_WAITFORRSP, 0, (void *)&mesh_access);

	return sprintf(buf, "%d\n", le32_to_cpu(mesh_access.data[0]));
}

static ssize_t lbs_autostart_enabled_set(struct device *dev,
		struct device_attribute *attr, const char * buf, size_t count)
{
	struct cmd_ds_mesh_access mesh_access;
	uint32_t datum;
	lbs_private *priv = (to_net_dev(dev))->priv;
	int ret;

	memset(&mesh_access, 0, sizeof(mesh_access));
	sscanf(buf, "%d", &datum);
	mesh_access.data[0] = cpu_to_le32(datum);

	ret = lbs_prepare_and_send_command(priv,
			CMD_MESH_ACCESS,
			CMD_ACT_MESH_SET_AUTOSTART_ENABLED,
			CMD_OPTION_WAITFORRSP, 0, (void *)&mesh_access);
	if (ret == 0)
		priv->mesh_autostart_enabled = datum ? 1 : 0;

	return strlen(buf);
}

static DEVICE_ATTR(autostart_enabled, 0644,
		lbs_autostart_enabled_get, lbs_autostart_enabled_set);

static struct attribute *lbs_mesh_sysfs_entries[] = {
	&dev_attr_anycast_mask.attr,
	&dev_attr_autostart_enabled.attr,
	NULL,
};

static struct attribute_group lbs_mesh_attr_group = {
	.attrs = lbs_mesh_sysfs_entries,
};

/**
 *  @brief Check if the device can be open and wait if necessary.
 *
 *  @param dev     A pointer to net_device structure
 *  @return 	   0
 *
 * For USB adapter, on some systems the device open handler will be
 * called before FW ready. Use the following flag check and wait
 * function to work around the issue.
 *
 */
static int pre_open_check(struct net_device *dev)
{
	lbs_private *priv = (lbs_private *) dev->priv;
	lbs_adapter *adapter = priv->adapter;
	int i = 0;

	while (!adapter->fw_ready && i < 20) {
		i++;
		msleep_interruptible(100);
	}
	if (!adapter->fw_ready) {
		lbs_pr_err("firmware not ready\n");
		return -1;
	}

	return 0;
}

/**
 *  @brief This function opens the device
 *
 *  @param dev     A pointer to net_device structure
 *  @return 	   0
 */
static int lbs_dev_open(struct net_device *dev)
{
	lbs_private *priv = (lbs_private *) dev->priv;
	lbs_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_NET);

	priv->open = 1;

	if (adapter->connect_status == LBS_CONNECTED) {
		netif_carrier_on(priv->dev);
		if (priv->mesh_dev)
			netif_carrier_on(priv->mesh_dev);
	} else {
		netif_carrier_off(priv->dev);
		if (priv->mesh_dev)
			netif_carrier_off(priv->mesh_dev);
	}

	lbs_deb_leave(LBS_DEB_NET);
	return 0;
}
/**
 *  @brief This function opens the mshX interface
 *
 *  @param dev     A pointer to net_device structure
 *  @return 	   0
 */
static int lbs_mesh_open(struct net_device *dev)
{
	lbs_private *priv = (lbs_private *) dev->priv ;

	if (pre_open_check(dev) == -1)
		return -1;
	priv->mesh_open = 1 ;
	netif_wake_queue(priv->mesh_dev);
	if (priv->infra_open == 0)
		return lbs_dev_open(priv->dev) ;
	return 0;
}

/**
 *  @brief This function opens the ethX interface
 *
 *  @param dev     A pointer to net_device structure
 *  @return 	   0
 */
static int lbs_open(struct net_device *dev)
{
	lbs_private *priv = (lbs_private *) dev->priv ;

	if(pre_open_check(dev) == -1)
		return -1;
	priv->infra_open = 1 ;
	netif_wake_queue(priv->dev);
	if (priv->open == 0)
		return lbs_dev_open(priv->dev) ;
	return 0;
}

static int lbs_dev_close(struct net_device *dev)
{
	lbs_private *priv = dev->priv;

	lbs_deb_enter(LBS_DEB_NET);

	netif_carrier_off(priv->dev);
	priv->open = 0;

	lbs_deb_leave(LBS_DEB_NET);
	return 0;
}

/**
 *  @brief This function closes the mshX interface
 *
 *  @param dev     A pointer to net_device structure
 *  @return 	   0
 */
static int lbs_mesh_close(struct net_device *dev)
{
	lbs_private *priv = (lbs_private *) (dev->priv);

	priv->mesh_open = 0;
	netif_stop_queue(priv->mesh_dev);
	if (priv->infra_open == 0)
		return lbs_dev_close(dev);
	else
		return 0;
}

/**
 *  @brief This function closes the ethX interface
 *
 *  @param dev     A pointer to net_device structure
 *  @return 	   0
 */
static int lbs_close(struct net_device *dev)
{
	lbs_private *priv = (lbs_private *) dev->priv;

	netif_stop_queue(dev);
	priv->infra_open = 0;
	if (priv->mesh_open == 0)
		return lbs_dev_close(dev);
	else
		return 0;
}


static int lbs_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	int ret = 0;
	lbs_private *priv = dev->priv;

	lbs_deb_enter(LBS_DEB_NET);

	if (priv->dnld_sent || priv->adapter->TxLockFlag) {
		priv->stats.tx_dropped++;
		goto done;
	}

	netif_stop_queue(priv->dev);
	if (priv->mesh_dev)
		netif_stop_queue(priv->mesh_dev);

	if (lbs_process_tx(priv, skb) == 0)
		dev->trans_start = jiffies;
done:
	lbs_deb_leave_args(LBS_DEB_NET, "ret %d", ret);
	return ret;
}

/**
 * @brief Mark mesh packets and handover them to lbs_hard_start_xmit
 *
 */
static int lbs_mesh_pre_start_xmit(struct sk_buff *skb,
		struct net_device *dev)
{
	lbs_private *priv = dev->priv;
	int ret;

	lbs_deb_enter(LBS_DEB_MESH);
	if (priv->adapter->monitormode != LBS_MONITOR_OFF) {
		netif_stop_queue(dev);
		return -EOPNOTSUPP;
	}

	SET_MESH_FRAME(skb);

	ret = lbs_hard_start_xmit(skb, priv->dev);
	lbs_deb_leave_args(LBS_DEB_MESH, "ret %d", ret);
	return ret;
}

/**
 * @brief Mark non-mesh packets and handover them to lbs_hard_start_xmit
 *
 */
static int lbs_pre_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	lbs_private *priv = dev->priv;
	int ret;

	lbs_deb_enter(LBS_DEB_NET);

	if (priv->adapter->monitormode != LBS_MONITOR_OFF) {
		netif_stop_queue(dev);
		return -EOPNOTSUPP;
	}

	UNSET_MESH_FRAME(skb);

	ret = lbs_hard_start_xmit(skb, dev);
	lbs_deb_leave_args(LBS_DEB_NET, "ret %d", ret);
	return ret;
}

static void lbs_tx_timeout(struct net_device *dev)
{
	lbs_private *priv = (lbs_private *) dev->priv;

	lbs_deb_enter(LBS_DEB_TX);

	lbs_pr_err("tx watch dog timeout\n");

	priv->dnld_sent = DNLD_RES_RECEIVED;
	dev->trans_start = jiffies;

	if (priv->adapter->currenttxskb) {
		if (priv->adapter->monitormode != LBS_MONITOR_OFF) {
			/* If we are here, we have not received feedback from
			   the previous packet.  Assume TX_FAIL and move on. */
			priv->adapter->eventcause = 0x01000000;
			lbs_send_tx_feedback(priv);
		} else
			wake_up_interruptible(&priv->waitq);
	} else if (priv->adapter->connect_status == LBS_CONNECTED) {
		netif_wake_queue(priv->dev);
		if (priv->mesh_dev)
			netif_wake_queue(priv->mesh_dev);
	}

	lbs_deb_leave(LBS_DEB_TX);
}

/**
 *  @brief This function returns the network statistics
 *
 *  @param dev     A pointer to lbs_private structure
 *  @return 	   A pointer to net_device_stats structure
 */
static struct net_device_stats *lbs_get_stats(struct net_device *dev)
{
	lbs_private *priv = (lbs_private *) dev->priv;

	return &priv->stats;
}

static int lbs_set_mac_address(struct net_device *dev, void *addr)
{
	int ret = 0;
	lbs_private *priv = (lbs_private *) dev->priv;
	lbs_adapter *adapter = priv->adapter;
	struct sockaddr *phwaddr = addr;

	lbs_deb_enter(LBS_DEB_NET);

	/* In case it was called from the mesh device */
	dev = priv->dev ;

	memset(adapter->current_addr, 0, ETH_ALEN);

	/* dev->dev_addr is 8 bytes */
	lbs_deb_hex(LBS_DEB_NET, "dev->dev_addr", dev->dev_addr, ETH_ALEN);

	lbs_deb_hex(LBS_DEB_NET, "addr", phwaddr->sa_data, ETH_ALEN);
	memcpy(adapter->current_addr, phwaddr->sa_data, ETH_ALEN);

	ret = lbs_prepare_and_send_command(priv, CMD_802_11_MAC_ADDRESS,
				    CMD_ACT_SET,
				    CMD_OPTION_WAITFORRSP, 0, NULL);

	if (ret) {
		lbs_deb_net("set MAC address failed\n");
		ret = -1;
		goto done;
	}

	lbs_deb_hex(LBS_DEB_NET, "adapter->macaddr", adapter->current_addr, ETH_ALEN);
	memcpy(dev->dev_addr, adapter->current_addr, ETH_ALEN);
	if (priv->mesh_dev)
		memcpy(priv->mesh_dev->dev_addr, adapter->current_addr, ETH_ALEN);

done:
	lbs_deb_leave_args(LBS_DEB_NET, "ret %d", ret);
	return ret;
}

static int lbs_copy_multicast_address(lbs_adapter *adapter,
				     struct net_device *dev)
{
	int i = 0;
	struct dev_mc_list *mcptr = dev->mc_list;

	for (i = 0; i < dev->mc_count; i++) {
		memcpy(&adapter->multicastlist[i], mcptr->dmi_addr, ETH_ALEN);
		mcptr = mcptr->next;
	}

	return i;

}

static void lbs_set_multicast_list(struct net_device *dev)
{
	lbs_private *priv = dev->priv;
	lbs_adapter *adapter = priv->adapter;
	int oldpacketfilter;
	DECLARE_MAC_BUF(mac);

	lbs_deb_enter(LBS_DEB_NET);

	oldpacketfilter = adapter->currentpacketfilter;

	if (dev->flags & IFF_PROMISC) {
		lbs_deb_net("enable promiscuous mode\n");
		adapter->currentpacketfilter |=
		    CMD_ACT_MAC_PROMISCUOUS_ENABLE;
		adapter->currentpacketfilter &=
		    ~(CMD_ACT_MAC_ALL_MULTICAST_ENABLE |
		      CMD_ACT_MAC_MULTICAST_ENABLE);
	} else {
		/* Multicast */
		adapter->currentpacketfilter &=
		    ~CMD_ACT_MAC_PROMISCUOUS_ENABLE;

		if (dev->flags & IFF_ALLMULTI || dev->mc_count >
		    MRVDRV_MAX_MULTICAST_LIST_SIZE) {
			lbs_deb_net( "enabling all multicast\n");
			adapter->currentpacketfilter |=
			    CMD_ACT_MAC_ALL_MULTICAST_ENABLE;
			adapter->currentpacketfilter &=
			    ~CMD_ACT_MAC_MULTICAST_ENABLE;
		} else {
			adapter->currentpacketfilter &=
			    ~CMD_ACT_MAC_ALL_MULTICAST_ENABLE;

			if (!dev->mc_count) {
				lbs_deb_net("no multicast addresses, "
				       "disabling multicast\n");
				adapter->currentpacketfilter &=
				    ~CMD_ACT_MAC_MULTICAST_ENABLE;
			} else {
				int i;

				adapter->currentpacketfilter |=
				    CMD_ACT_MAC_MULTICAST_ENABLE;

				adapter->nr_of_multicastmacaddr =
				    lbs_copy_multicast_address(adapter, dev);

				lbs_deb_net("multicast addresses: %d\n",
				       dev->mc_count);

				for (i = 0; i < dev->mc_count; i++) {
					lbs_deb_net("Multicast address %d:%s\n",
					       i, print_mac(mac,
					       adapter->multicastlist[i]));
				}
				/* send multicast addresses to firmware */
				lbs_prepare_and_send_command(priv,
						      CMD_MAC_MULTICAST_ADR,
						      CMD_ACT_SET, 0, 0,
						      NULL);
			}
		}
	}

	if (adapter->currentpacketfilter != oldpacketfilter) {
		lbs_set_mac_packet_filter(priv);
	}

	lbs_deb_leave(LBS_DEB_NET);
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
	lbs_private *priv = dev->priv;
	lbs_adapter *adapter = priv->adapter;
	wait_queue_t wait;
	u8 ireg = 0;

	lbs_deb_enter(LBS_DEB_THREAD);

	init_waitqueue_entry(&wait, current);

	set_freezable();
	for (;;) {
		lbs_deb_thread( "main-thread 111: intcounter=%d "
		       "currenttxskb=%p dnld_sent=%d\n",
		       adapter->intcounter,
		       adapter->currenttxskb, priv->dnld_sent);

		add_wait_queue(&priv->waitq, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock_irq(&adapter->driver_lock);
		if ((adapter->psstate == PS_STATE_SLEEP) ||
		    (!adapter->intcounter
		     && (priv->dnld_sent || adapter->cur_cmd ||
			 list_empty(&adapter->cmdpendingq)))) {
			lbs_deb_thread(
			       "main-thread sleeping... Conn=%d IntC=%d PS_mode=%d PS_State=%d\n",
			       adapter->connect_status, adapter->intcounter,
			       adapter->psmode, adapter->psstate);
			spin_unlock_irq(&adapter->driver_lock);
			schedule();
		} else
			spin_unlock_irq(&adapter->driver_lock);

		lbs_deb_thread(
		       "main-thread 222 (waking up): intcounter=%d currenttxskb=%p "
		       "dnld_sent=%d\n", adapter->intcounter,
		       adapter->currenttxskb, priv->dnld_sent);

		set_current_state(TASK_RUNNING);
		remove_wait_queue(&priv->waitq, &wait);
		try_to_freeze();

		lbs_deb_thread("main-thread 333: intcounter=%d currenttxskb=%p "
		       "dnld_sent=%d\n",
		       adapter->intcounter,
		       adapter->currenttxskb, priv->dnld_sent);

		if (kthread_should_stop()
		    || adapter->surpriseremoved) {
			lbs_deb_thread(
			       "main-thread: break from main thread: surpriseremoved=0x%x\n",
			       adapter->surpriseremoved);
			break;
		}


		spin_lock_irq(&adapter->driver_lock);
		if (adapter->intcounter) {
			u8 int_status;
			adapter->intcounter = 0;
			int_status = priv->hw_get_int_status(priv, &ireg);

			if (int_status) {
				lbs_deb_thread(
				       "main-thread: reading HOST_INT_STATUS_REG failed\n");
				spin_unlock_irq(&adapter->driver_lock);
				continue;
			}
			adapter->hisregcpy |= ireg;
		}

		lbs_deb_thread("main-thread 444: intcounter=%d currenttxskb=%p "
		       "dnld_sent=%d\n",
		       adapter->intcounter,
		       adapter->currenttxskb, priv->dnld_sent);

		/* command response? */
		if (adapter->hisregcpy & MRVDRV_CMD_UPLD_RDY) {
			lbs_deb_thread("main-thread: cmd response ready\n");

			adapter->hisregcpy &= ~MRVDRV_CMD_UPLD_RDY;
			spin_unlock_irq(&adapter->driver_lock);
			lbs_process_rx_command(priv);
			spin_lock_irq(&adapter->driver_lock);
		}

		/* Any Card Event */
		if (adapter->hisregcpy & MRVDRV_CARDEVENT) {
			lbs_deb_thread("main-thread: Card Event Activity\n");

			adapter->hisregcpy &= ~MRVDRV_CARDEVENT;

			if (priv->hw_read_event_cause(priv)) {
				lbs_pr_alert(
				       "main-thread: hw_read_event_cause failed\n");
				spin_unlock_irq(&adapter->driver_lock);
				continue;
			}
			spin_unlock_irq(&adapter->driver_lock);
			lbs_process_event(priv);
		} else
			spin_unlock_irq(&adapter->driver_lock);

		/* Check if we need to confirm Sleep Request received previously */
		if (adapter->psstate == PS_STATE_PRE_SLEEP) {
			if (!priv->dnld_sent && !adapter->cur_cmd) {
				if (adapter->connect_status ==
				    LBS_CONNECTED) {
					lbs_deb_thread(
					       "main_thread: PRE_SLEEP--intcounter=%d currenttxskb=%p "
					       "dnld_sent=%d cur_cmd=%p, confirm now\n",
					       adapter->intcounter,
					       adapter->currenttxskb,
					       priv->dnld_sent,
					       adapter->cur_cmd);

					lbs_ps_confirm_sleep(priv,
						       (u16) adapter->psmode);
				} else {
					/* workaround for firmware sending
					 * deauth/linkloss event immediately
					 * after sleep request, remove this
					 * after firmware fixes it
					 */
					adapter->psstate = PS_STATE_AWAKE;
					lbs_pr_alert(
					       "main-thread: ignore PS_SleepConfirm in non-connected state\n");
				}
			}
		}

		/* The PS state is changed during processing of Sleep Request
		 * event above
		 */
		if ((priv->adapter->psstate == PS_STATE_SLEEP) ||
		    (priv->adapter->psstate == PS_STATE_PRE_SLEEP))
			continue;

		/* Execute the next command */
		if (!priv->dnld_sent && !priv->adapter->cur_cmd)
			lbs_execute_next_command(priv);

		/* Wake-up command waiters which can't sleep in
		 * lbs_prepare_and_send_command
		 */
		if (!adapter->nr_cmd_pending)
			wake_up_all(&adapter->cmd_pending);

		lbs_tx_runqueue(priv);
	}

	del_timer(&adapter->command_timer);
	adapter->nr_cmd_pending = 0;
	wake_up_all(&adapter->cmd_pending);

	lbs_deb_leave(LBS_DEB_THREAD);
	return 0;
}

/**
 *  @brief This function downloads firmware image, gets
 *  HW spec from firmware and set basic parameters to
 *  firmware.
 *
 *  @param priv    A pointer to lbs_private structure
 *  @return 	   0 or -1
 */
static int lbs_setup_firmware(lbs_private *priv)
{
	int ret = -1;
	lbs_adapter *adapter = priv->adapter;
	struct cmd_ds_mesh_access mesh_access;

	lbs_deb_enter(LBS_DEB_FW);

	/*
	 * Read MAC address from HW
	 */
	memset(adapter->current_addr, 0xff, ETH_ALEN);

	ret = lbs_prepare_and_send_command(priv, CMD_GET_HW_SPEC,
				    0, CMD_OPTION_WAITFORRSP, 0, NULL);

	if (ret) {
		ret = -1;
		goto done;
	}

	lbs_set_mac_packet_filter(priv);

	/* Get the supported Data rates */
	ret = lbs_prepare_and_send_command(priv, CMD_802_11_DATA_RATE,
				    CMD_ACT_GET_TX_RATE,
				    CMD_OPTION_WAITFORRSP, 0, NULL);

	if (ret) {
		ret = -1;
		goto done;
	}

	/* Disable mesh autostart */
	if (priv->mesh_dev) {
		memset(&mesh_access, 0, sizeof(mesh_access));
		mesh_access.data[0] = cpu_to_le32(0);
		ret = lbs_prepare_and_send_command(priv,
				CMD_MESH_ACCESS,
				CMD_ACT_MESH_SET_AUTOSTART_ENABLED,
				CMD_OPTION_WAITFORRSP, 0, (void *)&mesh_access);
		if (ret) {
			ret = -1;
			goto done;
		}
		priv->mesh_autostart_enabled = 0;
	}

       /* Set the boot2 version in firmware */
       ret = lbs_prepare_and_send_command(priv, CMD_SET_BOOT2_VER,
                                   0, CMD_OPTION_WAITFORRSP, 0, NULL);

	ret = 0;
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
	lbs_private *priv = (lbs_private *)data;
	lbs_adapter *adapter = priv->adapter;
	struct cmd_ctrl_node *ptempnode;
	struct cmd_ds_command *cmd;
	unsigned long flags;

	ptempnode = adapter->cur_cmd;
	if (ptempnode == NULL) {
		lbs_deb_fw("ptempnode empty\n");
		return;
	}

	cmd = (struct cmd_ds_command *)ptempnode->bufvirtualaddr;
	if (!cmd) {
		lbs_deb_fw("cmd is NULL\n");
		return;
	}

	lbs_deb_fw("command_timer_fn fired, cmd %x\n", cmd->command);

	if (!adapter->fw_ready)
		return;

	spin_lock_irqsave(&adapter->driver_lock, flags);
	adapter->cur_cmd = NULL;
	spin_unlock_irqrestore(&adapter->driver_lock, flags);

	lbs_deb_fw("re-sending same command because of timeout\n");
	lbs_queue_cmd(adapter, ptempnode, 0);

	wake_up_interruptible(&priv->waitq);

	return;
}

static int lbs_init_adapter(lbs_private *priv)
{
	lbs_adapter *adapter = priv->adapter;
	size_t bufsize;
	int i, ret = 0;

	/* Allocate buffer to store the BSSID list */
	bufsize = MAX_NETWORK_COUNT * sizeof(struct bss_descriptor);
	adapter->networks = kzalloc(bufsize, GFP_KERNEL);
	if (!adapter->networks) {
		lbs_pr_err("Out of memory allocating beacons\n");
		ret = -1;
		goto out;
	}

	/* Initialize scan result lists */
	INIT_LIST_HEAD(&adapter->network_free_list);
	INIT_LIST_HEAD(&adapter->network_list);
	for (i = 0; i < MAX_NETWORK_COUNT; i++) {
		list_add_tail(&adapter->networks[i].list,
			      &adapter->network_free_list);
	}

	adapter->lbs_ps_confirm_sleep.seqnum = cpu_to_le16(++adapter->seqnum);
	adapter->lbs_ps_confirm_sleep.command =
	    cpu_to_le16(CMD_802_11_PS_MODE);
	adapter->lbs_ps_confirm_sleep.size =
	    cpu_to_le16(sizeof(struct PS_CMD_ConfirmSleep));
	adapter->lbs_ps_confirm_sleep.action =
	    cpu_to_le16(CMD_SUBCMD_SLEEP_CONFIRMED);

	memset(adapter->current_addr, 0xff, ETH_ALEN);

	adapter->connect_status = LBS_DISCONNECTED;
	adapter->secinfo.auth_mode = IW_AUTH_ALG_OPEN_SYSTEM;
	adapter->mode = IW_MODE_INFRA;
	adapter->curbssparams.channel = DEFAULT_AD_HOC_CHANNEL;
	adapter->currentpacketfilter = CMD_ACT_MAC_RX_ON | CMD_ACT_MAC_TX_ON;
	adapter->radioon = RADIO_ON;
	adapter->auto_rate = 1;
	adapter->capability = WLAN_CAPABILITY_SHORT_PREAMBLE;
	adapter->psmode = LBS802_11POWERMODECAM;
	adapter->psstate = PS_STATE_FULL_POWER;

	mutex_init(&adapter->lock);

	memset(&adapter->tx_queue_ps, 0, NR_TX_QUEUE*sizeof(struct sk_buff*));
	adapter->tx_queue_idx = 0;
	spin_lock_init(&adapter->txqueue_lock);

	setup_timer(&adapter->command_timer, command_timer_fn,
	            (unsigned long)priv);

	INIT_LIST_HEAD(&adapter->cmdfreeq);
	INIT_LIST_HEAD(&adapter->cmdpendingq);

	spin_lock_init(&adapter->driver_lock);
	init_waitqueue_head(&adapter->cmd_pending);
	adapter->nr_cmd_pending = 0;

	/* Allocate the command buffers */
	if (lbs_allocate_cmd_buffer(priv)) {
		lbs_pr_err("Out of memory allocating command buffers\n");
		ret = -1;
	}

out:
	return ret;
}

static void lbs_free_adapter(lbs_private *priv)
{
	lbs_adapter *adapter = priv->adapter;

	if (!adapter) {
		lbs_deb_fw("why double free adapter?\n");
		return;
	}

	lbs_deb_fw("free command buffer\n");
	lbs_free_cmd_buffer(priv);

	lbs_deb_fw("free command_timer\n");
	del_timer(&adapter->command_timer);

	lbs_deb_fw("free scan results table\n");
	kfree(adapter->networks);
	adapter->networks = NULL;

	/* Free the adapter object itself */
	lbs_deb_fw("free adapter\n");
	kfree(adapter);
	priv->adapter = NULL;
}

/**
 * @brief This function adds the card. it will probe the
 * card, allocate the lbs_priv and initialize the device.
 *
 *  @param card    A pointer to card
 *  @return 	   A pointer to lbs_private structure
 */
lbs_private *lbs_add_card(void *card, struct device *dmdev)
{
	struct net_device *dev = NULL;
	lbs_private *priv = NULL;

	lbs_deb_enter(LBS_DEB_NET);

	/* Allocate an Ethernet device and register it */
	if (!(dev = alloc_etherdev(sizeof(lbs_private)))) {
		lbs_pr_err("init ethX device failed\n");
		goto done;
	}
	priv = dev->priv;

	/* allocate buffer for lbs_adapter */
	if (!(priv->adapter = kzalloc(sizeof(lbs_adapter), GFP_KERNEL))) {
		lbs_pr_err("allocate buffer for lbs_adapter failed\n");
		goto err_kzalloc;
	}

	if (lbs_init_adapter(priv)) {
		lbs_pr_err("failed to initialize adapter structure.\n");
		goto err_init_adapter;
	}

	priv->dev = dev;
	priv->card = card;
	priv->mesh_open = 0;
	priv->infra_open = 0;
	priv->hotplug_device = dmdev;

	/* Setup the OS Interface to our functions */
	dev->open = lbs_open;
	dev->hard_start_xmit = lbs_pre_start_xmit;
	dev->stop = lbs_close;
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
	if (device_create_file(dmdev, &dev_attr_lbs_rtap))
		goto err_init_adapter;

	lbs_deb_thread("Starting main thread...\n");
	init_waitqueue_head(&priv->waitq);
	priv->main_thread = kthread_run(lbs_thread, dev, "lbs_main");
	if (IS_ERR(priv->main_thread)) {
		lbs_deb_thread("Error creating main thread.\n");
		goto err_kthread_run;
	}

	priv->work_thread = create_singlethread_workqueue("lbs_worker");
	INIT_DELAYED_WORK(&priv->assoc_work, lbs_association_worker);
	INIT_DELAYED_WORK(&priv->scan_work, lbs_scan_worker);
	INIT_WORK(&priv->sync_channel, lbs_sync_channel);

	goto done;

err_kthread_run:
	device_remove_file(dmdev, &dev_attr_lbs_rtap);

err_init_adapter:
	lbs_free_adapter(priv);

err_kzalloc:
	free_netdev(dev);
	priv = NULL;

done:
	lbs_deb_leave_args(LBS_DEB_NET, "priv %p", priv);
	return priv;
}
EXPORT_SYMBOL_GPL(lbs_add_card);


int lbs_remove_card(lbs_private *priv)
{
	lbs_adapter *adapter = priv->adapter;
	struct net_device *dev = priv->dev;
	union iwreq_data wrqu;

	lbs_deb_enter(LBS_DEB_MAIN);

	lbs_remove_rtap(priv);

	dev = priv->dev;
	device_remove_file(priv->hotplug_device, &dev_attr_lbs_rtap);

	cancel_delayed_work(&priv->scan_work);
	cancel_delayed_work(&priv->assoc_work);
	destroy_workqueue(priv->work_thread);

	if (adapter->psmode == LBS802_11POWERMODEMAX_PSP) {
		adapter->psmode = LBS802_11POWERMODECAM;
		lbs_ps_wakeup(priv, CMD_OPTION_WAITFORRSP);
	}

	memset(wrqu.ap_addr.sa_data, 0xaa, ETH_ALEN);
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(priv->dev, SIOCGIWAP, &wrqu, NULL);

	/* Stop the thread servicing the interrupts */
	adapter->surpriseremoved = 1;
	kthread_stop(priv->main_thread);

	lbs_free_adapter(priv);

	priv->dev = NULL;
	free_netdev(dev);

	lbs_deb_leave(LBS_DEB_MAIN);
	return 0;
}
EXPORT_SYMBOL_GPL(lbs_remove_card);


int lbs_start_card(lbs_private *priv)
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

	lbs_debugfs_init_one(priv, dev);

	lbs_pr_info("%s: Marvell WLAN 802.11 adapter\n", dev->name);

	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_MAIN, "ret %d", ret);
	return ret;
}
EXPORT_SYMBOL_GPL(lbs_start_card);


int lbs_stop_card(lbs_private *priv)
{
	struct net_device *dev = priv->dev;
	int ret = -1;
	struct cmd_ctrl_node *cmdnode;
	unsigned long flags;

	lbs_deb_enter(LBS_DEB_MAIN);

	netif_stop_queue(priv->dev);
	netif_carrier_off(priv->dev);

	lbs_debugfs_remove_one(priv);

	/* Flush pending command nodes */
	spin_lock_irqsave(&priv->adapter->driver_lock, flags);
	list_for_each_entry(cmdnode, &priv->adapter->cmdpendingq, list) {
		cmdnode->cmdwaitqwoken = 1;
		wake_up_interruptible(&cmdnode->cmdwait_q);
	}
	spin_unlock_irqrestore(&priv->adapter->driver_lock, flags);

	unregister_netdev(dev);

	lbs_deb_leave_args(LBS_DEB_MAIN, "ret %d", ret);
	return ret;
}
EXPORT_SYMBOL_GPL(lbs_stop_card);


/**
 * @brief This function adds mshX interface
 *
 *  @param priv    A pointer to the lbs_private structure
 *  @return 	   0 if successful, -X otherwise
 */
int lbs_add_mesh(lbs_private *priv, struct device *dev)
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

	mesh_dev->open = lbs_mesh_open;
	mesh_dev->hard_start_xmit = lbs_mesh_pre_start_xmit;
	mesh_dev->stop = lbs_mesh_close;
	mesh_dev->get_stats = lbs_get_stats;
	mesh_dev->set_mac_address = lbs_set_mac_address;
	mesh_dev->ethtool_ops = &lbs_ethtool_ops;
	memcpy(mesh_dev->dev_addr, priv->dev->dev_addr,
			sizeof(priv->dev->dev_addr));

	SET_NETDEV_DEV(priv->mesh_dev, dev);

#ifdef	WIRELESS_EXT
	mesh_dev->wireless_handlers = (struct iw_handler_def *)&mesh_handler_def;
#endif
	/* Register virtual mesh interface */
	ret = register_netdev(mesh_dev);
	if (ret) {
		lbs_pr_err("cannot register mshX virtual interface\n");
		goto err_free;
	}

	ret = sysfs_create_group(&(mesh_dev->dev.kobj), &lbs_mesh_attr_group);
	if (ret)
		goto err_unregister;

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
EXPORT_SYMBOL_GPL(lbs_add_mesh);


void lbs_remove_mesh(lbs_private *priv)
{
	struct net_device *mesh_dev;

	lbs_deb_enter(LBS_DEB_MAIN);

	if (!priv)
		goto out;

	mesh_dev = priv->mesh_dev;

	netif_stop_queue(mesh_dev);
	netif_carrier_off(priv->mesh_dev);

	sysfs_remove_group(&(mesh_dev->dev.kobj), &lbs_mesh_attr_group);
	unregister_netdev(mesh_dev);

	priv->mesh_dev = NULL ;
	free_netdev(mesh_dev);

out:
	lbs_deb_leave(LBS_DEB_MAIN);
}
EXPORT_SYMBOL_GPL(lbs_remove_mesh);

/**
 *  @brief This function finds the CFP in
 *  region_cfp_table based on region and band parameter.
 *
 *  @param region  The region code
 *  @param band	   The band
 *  @param cfp_no  A pointer to CFP number
 *  @return 	   A pointer to CFP
 */
struct chan_freq_power *lbs_get_region_cfp_table(u8 region, u8 band, int *cfp_no)
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

int lbs_set_regiontable(lbs_private *priv, u8 region, u8 band)
{
	lbs_adapter *adapter = priv->adapter;
	int ret = 0;
	int i = 0;

	struct chan_freq_power *cfp;
	int cfp_no;

	lbs_deb_enter(LBS_DEB_MAIN);

	memset(adapter->region_channel, 0, sizeof(adapter->region_channel));

	{
		cfp = lbs_get_region_cfp_table(region, band, &cfp_no);
		if (cfp != NULL) {
			adapter->region_channel[i].nrcfp = cfp_no;
			adapter->region_channel[i].CFP = cfp;
		} else {
			lbs_deb_main("wrong region code %#x in band B/G\n",
			       region);
			ret = -1;
			goto out;
		}
		adapter->region_channel[i].valid = 1;
		adapter->region_channel[i].region = region;
		adapter->region_channel[i].band = band;
		i++;
	}
out:
	lbs_deb_leave_args(LBS_DEB_MAIN, "ret %d", ret);
	return ret;
}

/**
 *  @brief This function handles the interrupt. it will change PS
 *  state if applicable. it will wake up main_thread to handle
 *  the interrupt event as well.
 *
 *  @param dev     A pointer to net_device structure
 *  @return 	   n/a
 */
void lbs_interrupt(struct net_device *dev)
{
	lbs_private *priv = dev->priv;

	lbs_deb_enter(LBS_DEB_THREAD);

	lbs_deb_thread("lbs_interrupt: intcounter=%d\n",
	       priv->adapter->intcounter);

	priv->adapter->intcounter++;

	if (priv->adapter->psstate == PS_STATE_SLEEP) {
		priv->adapter->psstate = PS_STATE_AWAKE;
		netif_wake_queue(dev);
		if (priv->mesh_dev)
			netif_wake_queue(priv->mesh_dev);
	}

	wake_up_interruptible(&priv->waitq);

	lbs_deb_leave(LBS_DEB_THREAD);
}
EXPORT_SYMBOL_GPL(lbs_interrupt);

int lbs_reset_device(lbs_private *priv)
{
	int ret;

	lbs_deb_enter(LBS_DEB_MAIN);
	ret = lbs_prepare_and_send_command(priv, CMD_802_11_RESET,
				    CMD_ACT_HALT, 0, 0, NULL);
	msleep_interruptible(10);

	lbs_deb_leave_args(LBS_DEB_MAIN, "ret %d", ret);
	return ret;
}
EXPORT_SYMBOL_GPL(lbs_reset_device);

static int lbs_init_module(void)
{
	lbs_deb_enter(LBS_DEB_MAIN);
	lbs_debugfs_init();
	lbs_deb_leave(LBS_DEB_MAIN);
	return 0;
}

static void lbs_exit_module(void)
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
        netif_carrier_off(dev);
        netif_stop_queue(dev);
        return 0;
}

static int lbs_rtap_stop(struct net_device *dev)
{
        return 0;
}

static int lbs_rtap_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
        netif_stop_queue(dev);
        return -EOPNOTSUPP;
}

static struct net_device_stats *lbs_rtap_get_stats(struct net_device *dev)
{
	lbs_private *priv = dev->priv;
	return &priv->ieee->stats;
}


void lbs_remove_rtap(lbs_private *priv)
{
	if (priv->rtap_net_dev == NULL)
		return;
	unregister_netdev(priv->rtap_net_dev);
	free_ieee80211(priv->rtap_net_dev);
	priv->rtap_net_dev = NULL;
}

int lbs_add_rtap(lbs_private *priv)
{
	int rc = 0;

	if (priv->rtap_net_dev)
		return -EPERM;

	priv->rtap_net_dev = alloc_ieee80211(0);
	if (priv->rtap_net_dev == NULL)
		return -ENOMEM;


	priv->ieee = netdev_priv(priv->rtap_net_dev);

	strcpy(priv->rtap_net_dev->name, "rtap%d");

	priv->rtap_net_dev->type = ARPHRD_IEEE80211_RADIOTAP;
	priv->rtap_net_dev->open = lbs_rtap_open;
	priv->rtap_net_dev->stop = lbs_rtap_stop;
	priv->rtap_net_dev->get_stats = lbs_rtap_get_stats;
	priv->rtap_net_dev->hard_start_xmit = lbs_rtap_hard_start_xmit;
	priv->rtap_net_dev->set_multicast_list = lbs_set_multicast_list;
	priv->rtap_net_dev->priv = priv;

	priv->ieee->iw_mode = IW_MODE_MONITOR;

	rc = register_netdev(priv->rtap_net_dev);
	if (rc) {
		free_ieee80211(priv->rtap_net_dev);
		priv->rtap_net_dev = NULL;
		return rc;
	}

	return 0;
}


module_init(lbs_init_module);
module_exit(lbs_exit_module);

MODULE_DESCRIPTION("Libertas WLAN Driver Library");
MODULE_AUTHOR("Marvell International Ltd.");
MODULE_LICENSE("GPL");
