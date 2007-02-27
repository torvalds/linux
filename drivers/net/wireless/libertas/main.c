/**
  * This file contains the major functions in WLAN
  * driver. It includes init, exit, open, close and main
  * thread etc..
  */

#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>

#include <net/iw_handler.h>

#include "host.h"
#include "sbi.h"
#include "decl.h"
#include "dev.h"
#include "fw.h"
#include "wext.h"
#include "debugfs.h"
#include "assoc.h"

#ifdef ENABLE_PM
static struct pm_dev *wlan_pm_dev = NULL;
#endif

#define WLAN_TX_PWR_DEFAULT		20	/*100mW */
#define WLAN_TX_PWR_US_DEFAULT		20	/*100mW */
#define WLAN_TX_PWR_JP_DEFAULT		16	/*50mW */
#define WLAN_TX_PWR_FR_DEFAULT		20	/*100mW */
#define WLAN_TX_PWR_EMEA_DEFAULT	20	/*100mW */

/* Format { channel, frequency (MHz), maxtxpower } */
/* band: 'B/G', region: USA FCC/Canada IC */
static struct chan_freq_power channel_freq_power_US_BG[] = {
	{1, 2412, WLAN_TX_PWR_US_DEFAULT},
	{2, 2417, WLAN_TX_PWR_US_DEFAULT},
	{3, 2422, WLAN_TX_PWR_US_DEFAULT},
	{4, 2427, WLAN_TX_PWR_US_DEFAULT},
	{5, 2432, WLAN_TX_PWR_US_DEFAULT},
	{6, 2437, WLAN_TX_PWR_US_DEFAULT},
	{7, 2442, WLAN_TX_PWR_US_DEFAULT},
	{8, 2447, WLAN_TX_PWR_US_DEFAULT},
	{9, 2452, WLAN_TX_PWR_US_DEFAULT},
	{10, 2457, WLAN_TX_PWR_US_DEFAULT},
	{11, 2462, WLAN_TX_PWR_US_DEFAULT}
};

/* band: 'B/G', region: Europe ETSI */
static struct chan_freq_power channel_freq_power_EU_BG[] = {
	{1, 2412, WLAN_TX_PWR_EMEA_DEFAULT},
	{2, 2417, WLAN_TX_PWR_EMEA_DEFAULT},
	{3, 2422, WLAN_TX_PWR_EMEA_DEFAULT},
	{4, 2427, WLAN_TX_PWR_EMEA_DEFAULT},
	{5, 2432, WLAN_TX_PWR_EMEA_DEFAULT},
	{6, 2437, WLAN_TX_PWR_EMEA_DEFAULT},
	{7, 2442, WLAN_TX_PWR_EMEA_DEFAULT},
	{8, 2447, WLAN_TX_PWR_EMEA_DEFAULT},
	{9, 2452, WLAN_TX_PWR_EMEA_DEFAULT},
	{10, 2457, WLAN_TX_PWR_EMEA_DEFAULT},
	{11, 2462, WLAN_TX_PWR_EMEA_DEFAULT},
	{12, 2467, WLAN_TX_PWR_EMEA_DEFAULT},
	{13, 2472, WLAN_TX_PWR_EMEA_DEFAULT}
};

/* band: 'B/G', region: Spain */
static struct chan_freq_power channel_freq_power_SPN_BG[] = {
	{10, 2457, WLAN_TX_PWR_DEFAULT},
	{11, 2462, WLAN_TX_PWR_DEFAULT}
};

/* band: 'B/G', region: France */
static struct chan_freq_power channel_freq_power_FR_BG[] = {
	{10, 2457, WLAN_TX_PWR_FR_DEFAULT},
	{11, 2462, WLAN_TX_PWR_FR_DEFAULT},
	{12, 2467, WLAN_TX_PWR_FR_DEFAULT},
	{13, 2472, WLAN_TX_PWR_FR_DEFAULT}
};

/* band: 'B/G', region: Japan */
static struct chan_freq_power channel_freq_power_JPN_BG[] = {
	{1, 2412, WLAN_TX_PWR_JP_DEFAULT},
	{2, 2417, WLAN_TX_PWR_JP_DEFAULT},
	{3, 2422, WLAN_TX_PWR_JP_DEFAULT},
	{4, 2427, WLAN_TX_PWR_JP_DEFAULT},
	{5, 2432, WLAN_TX_PWR_JP_DEFAULT},
	{6, 2437, WLAN_TX_PWR_JP_DEFAULT},
	{7, 2442, WLAN_TX_PWR_JP_DEFAULT},
	{8, 2447, WLAN_TX_PWR_JP_DEFAULT},
	{9, 2452, WLAN_TX_PWR_JP_DEFAULT},
	{10, 2457, WLAN_TX_PWR_JP_DEFAULT},
	{11, 2462, WLAN_TX_PWR_JP_DEFAULT},
	{12, 2467, WLAN_TX_PWR_JP_DEFAULT},
	{13, 2472, WLAN_TX_PWR_JP_DEFAULT},
	{14, 2484, WLAN_TX_PWR_JP_DEFAULT}
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
	 sizeof(channel_freq_power_US_BG) / sizeof(struct chan_freq_power),
	 }
	,
	{0x20,			/*CANADA IC */
	 channel_freq_power_US_BG,
	 sizeof(channel_freq_power_US_BG) / sizeof(struct chan_freq_power),
	 }
	,
	{0x30, /*EU*/ channel_freq_power_EU_BG,
	 sizeof(channel_freq_power_EU_BG) / sizeof(struct chan_freq_power),
	 }
	,
	{0x31, /*SPAIN*/ channel_freq_power_SPN_BG,
	 sizeof(channel_freq_power_SPN_BG) / sizeof(struct chan_freq_power),
	 }
	,
	{0x32, /*FRANCE*/ channel_freq_power_FR_BG,
	 sizeof(channel_freq_power_FR_BG) / sizeof(struct chan_freq_power),
	 }
	,
	{0x40, /*JAPAN*/ channel_freq_power_JPN_BG,
	 sizeof(channel_freq_power_JPN_BG) / sizeof(struct chan_freq_power),
	 }
	,
/*Add new region here */
};

/**
 * the rates supported by the card
 */
u8 libertas_wlan_data_rates[WLAN_SUPPORTED_RATES] =
    { 0x02, 0x04, 0x0B, 0x16, 0x00, 0x0C, 0x12,
	0x18, 0x24, 0x30, 0x48, 0x60, 0x6C, 0x00
};

/**
 * the rates supported
 */
u8 libertas_supported_rates[G_SUPPORTED_RATES] =
    { 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c,
0 };

/**
 * the rates supported for ad-hoc G mode
 */
u8 libertas_adhoc_rates_g[G_SUPPORTED_RATES] =
    { 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c,
0 };

/**
 * the rates supported for ad-hoc B mode
 */
u8 libertas_adhoc_rates_b[4] = { 0x82, 0x84, 0x8b, 0x96 };

/**
 * the global variable of a pointer to wlan_private
 * structure variable
 */
static wlan_private *wlanpriv = NULL;

#define MAX_DEVS 5
static struct net_device *libertas_devs[MAX_DEVS];
static int libertas_found = 0;

/**
 * the table to keep region code
 */
u16 libertas_region_code_to_index[MRVDRV_MAX_REGION_CODE] =
    { 0x10, 0x20, 0x30, 0x31, 0x32, 0x40 };

static u8 *default_fw_name = "usb8388.bin";

/**
 * Attributes exported through sysfs
 */

/**
 * @brief Get function for sysfs attribute libertas_mpp
 */
static ssize_t libertas_mpp_get(struct device * dev,
		struct device_attribute *attr, char * buf) {
	struct cmd_ds_mesh_access mesh_access;

	memset(&mesh_access, 0, sizeof(mesh_access));
	libertas_prepare_and_send_command(to_net_dev(dev)->priv,
			cmd_mesh_access,
			cmd_act_mesh_get_mpp,
			cmd_option_waitforrsp, 0, (void *)&mesh_access);

	return snprintf(buf, 3, "%d\n", mesh_access.data[0]);
}

/**
 * @brief Set function for sysfs attribute libertas_mpp
 */
static ssize_t libertas_mpp_set(struct device * dev,
		struct device_attribute *attr, const char * buf, size_t count) {
	struct cmd_ds_mesh_access mesh_access;


	memset(&mesh_access, 0, sizeof(mesh_access));
	sscanf(buf, "%d", &(mesh_access.data[0]));
	libertas_prepare_and_send_command((to_net_dev(dev))->priv,
			cmd_mesh_access,
			cmd_act_mesh_set_mpp,
			cmd_option_waitforrsp, 0, (void *)&mesh_access);
	return strlen(buf);
}

/**
 * libertas_mpp attribute to be exported per mshX interface
 * through sysfs (/sys/class/net/mshX/libertas-mpp)
 */
static DEVICE_ATTR(libertas_mpp, 0644, libertas_mpp_get,
		libertas_mpp_set );

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
static int pre_open_check(struct net_device *dev) {
	wlan_private *priv = (wlan_private *) dev->priv;
	wlan_adapter *adapter = priv->adapter;
	int i = 0;

	while (!adapter->fw_ready && i < 20) {
		i++;
		msleep_interruptible(100);
	}
	if (!adapter->fw_ready) {
		lbs_pr_info("FW not ready, pre_open_check() return failure\n");
		LEAVE();
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
static int wlan_dev_open(struct net_device *dev)
{
	wlan_private *priv = (wlan_private *) dev->priv;
	wlan_adapter *adapter = priv->adapter;

	ENTER();


	priv->open = 1;

	if (adapter->connect_status == libertas_connected) {
		netif_carrier_on(priv->wlan_dev.netdev);
	} else
		netif_carrier_off(priv->wlan_dev.netdev);

	LEAVE();
	return 0;
}
/**
 *  @brief This function opens the mshX interface
 *
 *  @param dev     A pointer to net_device structure
 *  @return 	   0
 */
static int mesh_open(struct net_device *dev)
{
	wlan_private *priv = (wlan_private *) dev->priv ;

	if(pre_open_check(dev) == -1)
		return -1;
	priv->mesh_open = 1 ;
	netif_start_queue(priv->mesh_dev);
	if (priv->infra_open == 0)
		return wlan_dev_open(priv->wlan_dev.netdev) ;
	return 0;
}

/**
 *  @brief This function opens the ethX interface
 *
 *  @param dev     A pointer to net_device structure
 *  @return 	   0
 */
static int wlan_open(struct net_device *dev)
{
	wlan_private *priv = (wlan_private *) dev->priv ;

	if(pre_open_check(dev) == -1)
		return -1;
	priv->infra_open = 1 ;
	netif_wake_queue(priv->wlan_dev.netdev);
	if (priv->open == 0)
		return wlan_dev_open(priv->wlan_dev.netdev) ;
	return 0;
}

static int wlan_dev_close(struct net_device *dev)
{
	wlan_private *priv = dev->priv;

	ENTER();

	netif_carrier_off(priv->wlan_dev.netdev);
	priv->open = 0;

	LEAVE();
	return 0;
}

/**
 *  @brief This function closes the mshX interface
 *
 *  @param dev     A pointer to net_device structure
 *  @return 	   0
 */
static int mesh_close(struct net_device *dev)
{
	wlan_private *priv = (wlan_private *) (dev->priv);

	priv->mesh_open = 0;
	netif_stop_queue(priv->mesh_dev);
	if (priv->infra_open == 0)
		return wlan_dev_close( ((wlan_private *) dev->priv)->wlan_dev.netdev) ;
	else
		return 0;
}

/**
 *  @brief This function closes the ethX interface
 *
 *  @param dev     A pointer to net_device structure
 *  @return 	   0
 */
static int wlan_close(struct net_device *dev) {
	wlan_private *priv = (wlan_private *) dev->priv;

	netif_stop_queue(priv->wlan_dev.netdev);
	priv->infra_open = 0;
	if (priv->mesh_open == 0)
		return wlan_dev_close( ((wlan_private *) dev->priv)->wlan_dev.netdev) ;
	else
		return 0;
}


#ifdef ENABLE_PM

/**
 *  @brief This function is a callback function. it is called by
 *  kernel to enter or exit power saving mode.
 *
 *  @param pmdev   A pointer to pm_dev
 *  @param pmreq   pm_request_t
 *  @param pmdata  A pointer to pmdata
 *  @return 	   0 or -1
 */
static int wlan_pm_callback(struct pm_dev *pmdev, pm_request_t pmreq,
			    void *pmdata)
{
	wlan_private *priv = wlanpriv;
	wlan_adapter *adapter = priv->adapter;
	struct net_device *dev = priv->wlan_dev.netdev;

	lbs_pr_debug(1, "WPRM_PM_CALLBACK: pmreq = %d.\n", pmreq);

	switch (pmreq) {
	case PM_SUSPEND:
		lbs_pr_debug(1, "WPRM_PM_CALLBACK: enter PM_SUSPEND.\n");

		/* in associated mode */
		if (adapter->connect_status == libertas_connected) {
			if ((adapter->psstate != PS_STATE_SLEEP)
			    ) {
				lbs_pr_debug(1,
				       "wlan_pm_callback: can't enter sleep mode\n");
				return -1;
			} else {

				/*
				 * Detach the network interface
				 * if the network is running
				 */
				if (netif_running(dev)) {
					netif_device_detach(dev);
					lbs_pr_debug(1,
					       "netif_device_detach().\n");
				}
				libertas_sbi_suspend(priv);
			}
			break;
		}

		/* in non associated mode */

		/*
		 * Detach the network interface
		 * if the network is running
		 */
		if (netif_running(dev))
			netif_device_detach(dev);

		/*
		 * Storing and restoring of the regs be taken care
		 * at the driver rest will be done at wlan driver
		 * this makes driver independent of the card
		 */

		libertas_sbi_suspend(priv);

		break;

	case PM_RESUME:
		/* in associated mode */
		if (adapter->connect_status == libertas_connected) {
			{
				/*
				 * Bring the inteface up first
				 * This case should not happen still ...
				 */
				libertas_sbi_resume(priv);

				/*
				 * Attach the network interface
				 * if the network is running
				 */
				if (netif_running(dev)) {
					netif_device_attach(dev);
					lbs_pr_debug(1,
					       "after netif_device_attach().\n");
				}
				lbs_pr_debug(1,
				       "After netif attach, in associated mode.\n");
			}
			break;
		}

		/* in non associated mode */

		/*
		 * Bring the inteface up first
		 * This case should not happen still ...
		 */

		libertas_sbi_resume(priv);

		if (netif_running(dev))
			netif_device_attach(dev);

		lbs_pr_debug(1, "after netif attach, in NON associated mode.\n");
		break;
	}

	return 0;
}
#endif				/* ENABLE_PM */

static int wlan_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	int ret = 0;
	wlan_private *priv = dev->priv;

	ENTER();

	if (priv->wlan_dev.dnld_sent || priv->adapter->TxLockFlag) {
		priv->stats.tx_dropped++;
		goto done;
	}

	netif_stop_queue(priv->wlan_dev.netdev);

	if (libertas_process_tx(priv, skb) == 0)
		dev->trans_start = jiffies;
done:
	LEAVE();
	return ret;
}

/**
 * @brief Mark mesh packets and handover them to wlan_hard_start_xmit
 *
 */
static int mesh_pre_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	wlan_private *priv = dev->priv;
	ENTER();
	SET_MESH_FRAME(skb);
	LEAVE();

	return wlan_hard_start_xmit(skb, priv->wlan_dev.netdev);
}

/**
 * @brief Mark non-mesh packets and handover them to wlan_hard_start_xmit
 *
 */
static int wlan_pre_start_xmit(struct sk_buff *skb, struct net_device *dev) {
	ENTER();
	UNSET_MESH_FRAME(skb);
	LEAVE();
	return wlan_hard_start_xmit(skb, dev);
}

static void wlan_tx_timeout(struct net_device *dev)
{
	wlan_private *priv = (wlan_private *) dev->priv;

	ENTER();

	lbs_pr_err("tx watch dog timeout!\n");

	priv->wlan_dev.dnld_sent = DNLD_RES_RECEIVED;
	dev->trans_start = jiffies;

	if (priv->adapter->currenttxskb) {
		if (priv->adapter->radiomode == WLAN_RADIOMODE_RADIOTAP) {
			/* If we are here, we have not received feedback from
			   the previous packet.  Assume TX_FAIL and move on. */
			priv->adapter->eventcause = 0x01000000;
			libertas_send_tx_feedback(priv);
		} else
			wake_up_interruptible(&priv->mainthread.waitq);
	} else if (priv->adapter->connect_status == libertas_connected)
		netif_wake_queue(priv->wlan_dev.netdev);

	LEAVE();
}

/**
 *  @brief This function returns the network statistics
 *
 *  @param dev     A pointer to wlan_private structure
 *  @return 	   A pointer to net_device_stats structure
 */
static struct net_device_stats *wlan_get_stats(struct net_device *dev)
{
	wlan_private *priv = (wlan_private *) dev->priv;

	return &priv->stats;
}

static int wlan_set_mac_address(struct net_device *dev, void *addr)
{
	int ret = 0;
	wlan_private *priv = (wlan_private *) dev->priv;
	wlan_adapter *adapter = priv->adapter;
	struct sockaddr *phwaddr = addr;

	ENTER();

	memset(adapter->current_addr, 0, ETH_ALEN);

	/* dev->dev_addr is 8 bytes */
	lbs_dbg_hex("dev->dev_addr:", dev->dev_addr, ETH_ALEN);

	lbs_dbg_hex("addr:", phwaddr->sa_data, ETH_ALEN);
	memcpy(adapter->current_addr, phwaddr->sa_data, ETH_ALEN);

	ret = libertas_prepare_and_send_command(priv, cmd_802_11_mac_address,
				    cmd_act_set,
				    cmd_option_waitforrsp, 0, NULL);

	if (ret) {
		lbs_pr_debug(1, "set mac address failed.\n");
		ret = -1;
		goto done;
	}

	lbs_dbg_hex("adapter->macaddr:", adapter->current_addr, ETH_ALEN);
	memcpy(dev->dev_addr, adapter->current_addr, ETH_ALEN);
	memcpy(((wlan_private *) dev->priv)->mesh_dev->dev_addr, adapter->current_addr, ETH_ALEN);

done:
	LEAVE();
	return ret;
}

static int wlan_copy_multicast_address(wlan_adapter * adapter,
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

static void wlan_set_multicast_list(struct net_device *dev)
{
	wlan_private *priv = dev->priv;
	wlan_adapter *adapter = priv->adapter;
	int oldpacketfilter;

	ENTER();

	oldpacketfilter = adapter->currentpacketfilter;

	if (dev->flags & IFF_PROMISC) {
		lbs_pr_debug(1, "enable Promiscuous mode\n");
		adapter->currentpacketfilter |=
		    cmd_act_mac_promiscuous_enable;
		adapter->currentpacketfilter &=
		    ~(cmd_act_mac_all_multicast_enable |
		      cmd_act_mac_multicast_enable);
	} else {
		/* Multicast */
		adapter->currentpacketfilter &=
		    ~cmd_act_mac_promiscuous_enable;

		if (dev->flags & IFF_ALLMULTI || dev->mc_count >
		    MRVDRV_MAX_MULTICAST_LIST_SIZE) {
			lbs_pr_debug(1, "Enabling All Multicast!\n");
			adapter->currentpacketfilter |=
			    cmd_act_mac_all_multicast_enable;
			adapter->currentpacketfilter &=
			    ~cmd_act_mac_multicast_enable;
		} else {
			adapter->currentpacketfilter &=
			    ~cmd_act_mac_all_multicast_enable;

			if (!dev->mc_count) {
				lbs_pr_debug(1, "No multicast addresses - "
				       "disabling multicast!\n");
				adapter->currentpacketfilter &=
				    ~cmd_act_mac_multicast_enable;
			} else {
				int i;

				adapter->currentpacketfilter |=
				    cmd_act_mac_multicast_enable;

				adapter->nr_of_multicastmacaddr =
				    wlan_copy_multicast_address(adapter, dev);

				lbs_pr_debug(1, "Multicast addresses: %d\n",
				       dev->mc_count);

				for (i = 0; i < dev->mc_count; i++) {
					lbs_pr_debug(1, "Multicast address %d:"
					       "%x %x %x %x %x %x\n", i,
					       adapter->multicastlist[i][0],
					       adapter->multicastlist[i][1],
					       adapter->multicastlist[i][2],
					       adapter->multicastlist[i][3],
					       adapter->multicastlist[i][4],
					       adapter->multicastlist[i][5]);
				}
				/* set multicast addresses to firmware */
				libertas_prepare_and_send_command(priv,
						      cmd_mac_multicast_adr,
						      cmd_act_set, 0, 0,
						      NULL);
			}
		}
	}

	if (adapter->currentpacketfilter != oldpacketfilter) {
		libertas_set_mac_packet_filter(priv);
	}

	LEAVE();
}

/**
 *  @brief This function hanldes the major job in WLAN driver.
 *  it handles the event generated by firmware, rx data received
 *  from firmware and tx data sent from kernel.
 *
 *  @param data    A pointer to wlan_thread structure
 *  @return 	   0
 */
static int wlan_service_main_thread(void *data)
{
	struct wlan_thread *thread = data;
	wlan_private *priv = thread->priv;
	wlan_adapter *adapter = priv->adapter;
	wait_queue_t wait;
	u8 ireg = 0;

	ENTER();

	wlan_activate_thread(thread);

	init_waitqueue_entry(&wait, current);

	for (;;) {
		lbs_pr_debug(1, "main-thread 111: intcounter=%d "
		       "currenttxskb=%p dnld_sent=%d\n",
		       adapter->intcounter,
		       adapter->currenttxskb, priv->wlan_dev.dnld_sent);

		add_wait_queue(&thread->waitq, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
		spin_lock_irq(&adapter->driver_lock);
		if ((adapter->psstate == PS_STATE_SLEEP) ||
		    (!adapter->intcounter
		     && (priv->wlan_dev.dnld_sent || adapter->cur_cmd ||
			 list_empty(&adapter->cmdpendingq)))) {
			lbs_pr_debug(1,
			       "main-thread sleeping... Conn=%d IntC=%d PS_mode=%d PS_State=%d\n",
			       adapter->connect_status, adapter->intcounter,
			       adapter->psmode, adapter->psstate);
			spin_unlock_irq(&adapter->driver_lock);
			schedule();
		} else
			spin_unlock_irq(&adapter->driver_lock);


		lbs_pr_debug(1,
		       "main-thread 222 (waking up): intcounter=%d currenttxskb=%p "
		       "dnld_sent=%d\n", adapter->intcounter,
		       adapter->currenttxskb, priv->wlan_dev.dnld_sent);

		set_current_state(TASK_RUNNING);
		remove_wait_queue(&thread->waitq, &wait);
		try_to_freeze();

		lbs_pr_debug(1, "main-thread 333: intcounter=%d currenttxskb=%p "
		       "dnld_sent=%d\n",
		       adapter->intcounter,
		       adapter->currenttxskb, priv->wlan_dev.dnld_sent);

		if (kthread_should_stop()
		    || adapter->surpriseremoved) {
			lbs_pr_debug(1,
			       "main-thread: break from main thread: surpriseremoved=0x%x\n",
			       adapter->surpriseremoved);
			break;
		}


		spin_lock_irq(&adapter->driver_lock);
		if (adapter->intcounter) {
			u8 int_status;
			adapter->intcounter = 0;
			int_status = libertas_sbi_get_int_status(priv, &ireg);

			if (int_status) {
				lbs_pr_debug(1,
				       "main-thread: reading HOST_INT_STATUS_REG failed\n");
				spin_unlock_irq(&adapter->driver_lock);
				continue;
			}
			adapter->hisregcpy |= ireg;
		}

		lbs_pr_debug(1, "main-thread 444: intcounter=%d currenttxskb=%p "
		       "dnld_sent=%d\n",
		       adapter->intcounter,
		       adapter->currenttxskb, priv->wlan_dev.dnld_sent);

		/* command response? */
		if (adapter->hisregcpy & his_cmdupldrdy) {
			lbs_pr_debug(1, "main-thread: cmd response ready.\n");

			adapter->hisregcpy &= ~his_cmdupldrdy;
			spin_unlock_irq(&adapter->driver_lock);
			libertas_process_rx_command(priv);
			spin_lock_irq(&adapter->driver_lock);
		}

		/* Any Card Event */
		if (adapter->hisregcpy & his_cardevent) {
			lbs_pr_debug(1, "main-thread: Card Event Activity.\n");

			adapter->hisregcpy &= ~his_cardevent;

			if (libertas_sbi_read_event_cause(priv)) {
				lbs_pr_alert(
				       "main-thread: libertas_sbi_read_event_cause failed.\n");
				spin_unlock_irq(&adapter->driver_lock);
				continue;
			}
			spin_unlock_irq(&adapter->driver_lock);
			libertas_process_event(priv);
		} else
			spin_unlock_irq(&adapter->driver_lock);

		/* Check if we need to confirm Sleep Request received previously */
		if (adapter->psstate == PS_STATE_PRE_SLEEP) {
			if (!priv->wlan_dev.dnld_sent && !adapter->cur_cmd) {
				if (adapter->connect_status ==
				    libertas_connected) {
					lbs_pr_debug(1,
					       "main_thread: PRE_SLEEP--intcounter=%d currenttxskb=%p "
					       "dnld_sent=%d cur_cmd=%p, confirm now\n",
					       adapter->intcounter,
					       adapter->currenttxskb,
					       priv->wlan_dev.dnld_sent,
					       adapter->cur_cmd);

					libertas_ps_confirm_sleep(priv,
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
		if (!priv->wlan_dev.dnld_sent && !priv->adapter->cur_cmd)
			libertas_execute_next_command(priv);

		/* Wake-up command waiters which can't sleep in
		 * libertas_prepare_and_send_command
		 */
		if (!adapter->nr_cmd_pending)
			wake_up_all(&adapter->cmd_pending);

		libertas_tx_runqueue(priv);
	}

	del_timer(&adapter->command_timer);
	adapter->nr_cmd_pending = 0;
	wake_up_all(&adapter->cmd_pending);
	wlan_deactivate_thread(thread);

	LEAVE();
	return 0;
}

/**
 * @brief This function adds the card. it will probe the
 * card, allocate the wlan_priv and initialize the device.
 *
 *  @param card    A pointer to card
 *  @return 	   A pointer to wlan_private structure
 */
wlan_private *wlan_add_card(void *card)
{
	struct net_device *dev = NULL;
	struct net_device *mesh_dev = NULL;
	wlan_private *priv = NULL;

	ENTER();

	/* Allocate an Ethernet device and register it */
	if (!(dev = alloc_etherdev(sizeof(wlan_private)))) {
		lbs_pr_alert( "Init ethernet device failed!\n");
		return NULL;
	}

	priv = dev->priv;

	/* allocate buffer for wlan_adapter */
	if (!(priv->adapter = kmalloc(sizeof(wlan_adapter), GFP_KERNEL))) {
		lbs_pr_alert( "Allocate buffer for wlan_adapter failed!\n");
		goto err_kmalloc;
	}

	/* Allocate a virtual mesh device */
	if (!(mesh_dev = alloc_netdev(0, "msh%d", ether_setup))) {
		lbs_pr_debug(1, "Init ethernet device failed!\n");
		return NULL;
	}

	/* Both intervaces share the priv structure */
	mesh_dev->priv = priv;

	/* init wlan_adapter */
	memset(priv->adapter, 0, sizeof(wlan_adapter));

	priv->wlan_dev.netdev = dev;
	priv->wlan_dev.card = card;
	priv->mesh_open = 0;
	priv->infra_open = 0;
	priv->mesh_dev = mesh_dev;
	wlanpriv = priv;

	SET_MODULE_OWNER(dev);
	SET_MODULE_OWNER(mesh_dev);

	/* Setup the OS Interface to our functions */
	dev->open = wlan_open;
	dev->hard_start_xmit = wlan_pre_start_xmit;
	dev->stop = wlan_close;
	dev->do_ioctl = libertas_do_ioctl;
	dev->set_mac_address = wlan_set_mac_address;
	mesh_dev->open = mesh_open;
	mesh_dev->hard_start_xmit = mesh_pre_start_xmit;
	mesh_dev->stop = mesh_close;
	mesh_dev->do_ioctl = libertas_do_ioctl;
	memcpy(mesh_dev->dev_addr, wlanpriv->wlan_dev.netdev->dev_addr,
			sizeof(wlanpriv->wlan_dev.netdev->dev_addr));

#define	WLAN_WATCHDOG_TIMEOUT	(5 * HZ)

	dev->tx_timeout = wlan_tx_timeout;
	dev->get_stats = wlan_get_stats;
	dev->watchdog_timeo = WLAN_WATCHDOG_TIMEOUT;
	dev->ethtool_ops = &libertas_ethtool_ops;
	mesh_dev->get_stats = wlan_get_stats;
	mesh_dev->ethtool_ops = &libertas_ethtool_ops;

#ifdef	WIRELESS_EXT
	dev->wireless_handlers = (struct iw_handler_def *)&libertas_handler_def;
	mesh_dev->wireless_handlers = (struct iw_handler_def *)&libertas_handler_def;
#endif
#define NETIF_F_DYNALLOC 16
	dev->features |= NETIF_F_DYNALLOC;
	dev->flags |= IFF_BROADCAST | IFF_MULTICAST;
	dev->set_multicast_list = wlan_set_multicast_list;

	INIT_LIST_HEAD(&priv->adapter->cmdfreeq);
	INIT_LIST_HEAD(&priv->adapter->cmdpendingq);

	spin_lock_init(&priv->adapter->driver_lock);
	init_waitqueue_head(&priv->adapter->cmd_pending);
	priv->adapter->nr_cmd_pending = 0;

	lbs_pr_debug(1, "Starting kthread...\n");
	priv->mainthread.priv = priv;
	wlan_create_thread(wlan_service_main_thread,
			   &priv->mainthread, "wlan_main_service");

	priv->assoc_thread =
		create_singlethread_workqueue("libertas_assoc");
	INIT_DELAYED_WORK(&priv->assoc_work, wlan_association_worker);

	/*
	 * Register the device. Fillup the private data structure with
	 * relevant information from the card and request for the required
	 * IRQ.
	 */
	if (libertas_sbi_register_dev(priv) < 0) {
		lbs_pr_info("failed to register wlan device!\n");
		goto err_registerdev;
	}

	/* init FW and HW */
	if (libertas_init_fw(priv)) {
		lbs_pr_debug(1, "Firmware Init failed\n");
		goto err_registerdev;
	}

	if (register_netdev(dev)) {
		lbs_pr_err("Cannot register network device!\n");
		goto err_init_fw;
	}

	/* Register virtual mesh interface */
	if (register_netdev(mesh_dev)) {
		lbs_pr_info("Cannot register mesh virtual interface!\n");
		goto err_init_fw;
	}

	lbs_pr_info("%s: Marvell Wlan 802.11 adapter ", dev->name);

	libertas_debugfs_init_one(priv, dev);

	if (libertas_found == MAX_DEVS)
		goto err_init_fw;
	libertas_devs[libertas_found] = dev;
	libertas_found++;
#ifdef ENABLE_PM
	if (!(wlan_pm_dev = pm_register(PM_UNKNOWN_DEV, 0, wlan_pm_callback)))
		lbs_pr_alert( "failed to register PM callback\n");
#endif
	if (device_create_file(&(mesh_dev->dev), &dev_attr_libertas_mpp))
		goto err_create_file;

	LEAVE();
	return priv;

err_create_file:
	device_remove_file(&(mesh_dev->dev), &dev_attr_libertas_mpp);
err_init_fw:
	libertas_sbi_unregister_dev(priv);
err_registerdev:
	destroy_workqueue(priv->assoc_thread);
	/* Stop the thread servicing the interrupts */
	wake_up_interruptible(&priv->mainthread.waitq);
	wlan_terminate_thread(&priv->mainthread);
	kfree(priv->adapter);
err_kmalloc:
	free_netdev(dev);
	free_netdev(mesh_dev);
	wlanpriv = NULL;

	LEAVE();
	return NULL;
}

static void wake_pending_cmdnodes(wlan_private *priv)
{
	struct cmd_ctrl_node *cmdnode;
	unsigned long flags;

	spin_lock_irqsave(&priv->adapter->driver_lock, flags);
	list_for_each_entry(cmdnode, &priv->adapter->cmdpendingq, list) {
		cmdnode->cmdwaitqwoken = 1;
		wake_up_interruptible(&cmdnode->cmdwait_q);
	}
	spin_unlock_irqrestore(&priv->adapter->driver_lock, flags);
}


int wlan_remove_card(void *card)
{
	wlan_private *priv = libertas_sbi_get_priv(card);
	wlan_adapter *adapter;
	struct net_device *dev;
	struct net_device *mesh_dev;
	union iwreq_data wrqu;
	int i;

	ENTER();

	if (!priv) {
		LEAVE();
		return 0;
	}

	adapter = priv->adapter;

	if (!adapter) {
		LEAVE();
		return 0;
	}

	dev = priv->wlan_dev.netdev;
	mesh_dev = priv->mesh_dev;

	netif_stop_queue(mesh_dev);
	netif_stop_queue(priv->wlan_dev.netdev);
	netif_carrier_off(priv->wlan_dev.netdev);

	wake_pending_cmdnodes(priv);

	device_remove_file(&(mesh_dev->dev), &dev_attr_libertas_mpp);
	unregister_netdev(mesh_dev);
	unregister_netdev(dev);

	cancel_delayed_work(&priv->assoc_work);
	destroy_workqueue(priv->assoc_thread);

	if (adapter->psmode == wlan802_11powermodemax_psp) {
		adapter->psmode = wlan802_11powermodecam;
		libertas_ps_wakeup(priv, cmd_option_waitforrsp);
	}

	memset(wrqu.ap_addr.sa_data, 0xaa, ETH_ALEN);
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(priv->wlan_dev.netdev, SIOCGIWAP, &wrqu, NULL);

#ifdef ENABLE_PM
	pm_unregister(wlan_pm_dev);
#endif

	adapter->surpriseremoved = 1;

	/* Stop the thread servicing the interrupts */
	wlan_terminate_thread(&priv->mainthread);

	libertas_debugfs_remove_one(priv);

	lbs_pr_debug(1, "Free adapter\n");
	libertas_free_adapter(priv);

	for (i = 0; i<libertas_found; i++) {
		if (libertas_devs[i]==priv->wlan_dev.netdev) {
			libertas_devs[i] = libertas_devs[--libertas_found];
			libertas_devs[libertas_found] = NULL ;
			break ;
		}
	}

	lbs_pr_debug(1, "Unregister finish\n");

	priv->wlan_dev.netdev = NULL;
	priv->mesh_dev = NULL ;
	free_netdev(mesh_dev);
	free_netdev(dev);
	wlanpriv = NULL;

	LEAVE();
	return 0;
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
struct chan_freq_power *libertas_get_region_cfp_table(u8 region, u8 band, int *cfp_no)
{
	int i, end;

	ENTER();

	end = sizeof(region_cfp_table)/sizeof(struct region_cfp_table);

	for (i = 0; i < end ; i++) {
		lbs_pr_debug(1, "region_cfp_table[i].region=%d\n",
			region_cfp_table[i].region);
		if (region_cfp_table[i].region == region) {
			*cfp_no = region_cfp_table[i].cfp_no_BG;
			LEAVE();
			return region_cfp_table[i].cfp_BG;
		}
	}

	LEAVE();
	return NULL;
}

int libertas_set_regiontable(wlan_private * priv, u8 region, u8 band)
{
	wlan_adapter *adapter = priv->adapter;
	int i = 0;

	struct chan_freq_power *cfp;
	int cfp_no;

	ENTER();

	memset(adapter->region_channel, 0, sizeof(adapter->region_channel));

	{
		cfp = libertas_get_region_cfp_table(region, band, &cfp_no);
		if (cfp != NULL) {
			adapter->region_channel[i].nrcfp = cfp_no;
			adapter->region_channel[i].CFP = cfp;
		} else {
			lbs_pr_debug(1, "wrong region code %#x in band B-G\n",
			       region);
			return -1;
		}
		adapter->region_channel[i].valid = 1;
		adapter->region_channel[i].region = region;
		adapter->region_channel[i].band = band;
		i++;
	}
	LEAVE();
	return 0;
}

/**
 *  @brief This function handles the interrupt. it will change PS
 *  state if applicable. it will wake up main_thread to handle
 *  the interrupt event as well.
 *
 *  @param dev     A pointer to net_device structure
 *  @return 	   n/a
 */
void libertas_interrupt(struct net_device *dev)
{
	wlan_private *priv = dev->priv;

	ENTER();

	lbs_pr_debug(1, "libertas_interrupt: intcounter=%d\n",
	       priv->adapter->intcounter);

	priv->adapter->intcounter++;

	if (priv->adapter->psstate == PS_STATE_SLEEP) {
		priv->adapter->psstate = PS_STATE_AWAKE;
		netif_wake_queue(dev);
	}

	wake_up_interruptible(&priv->mainthread.waitq);

	LEAVE();
}

static int wlan_init_module(void)
{
	int ret = 0;

	ENTER();

	if (libertas_fw_name == NULL) {
		libertas_fw_name = default_fw_name;
	}

	libertas_debugfs_init();

	if (libertas_sbi_register()) {
		ret = -1;
		libertas_debugfs_remove();
		goto done;
	}

done:
	LEAVE();
	return ret;
}

static void wlan_cleanup_module(void)
{
	int i;

	ENTER();

	for (i = 0; i<libertas_found; i++) {
		wlan_private *priv = libertas_devs[i]->priv;
		reset_device(priv);
	}

	libertas_sbi_unregister();
	libertas_debugfs_remove();

	LEAVE();
}

module_init(wlan_init_module);
module_exit(wlan_cleanup_module);

MODULE_DESCRIPTION("M-WLAN Driver");
MODULE_AUTHOR("Marvell International Ltd.");
MODULE_LICENSE("GPL");
