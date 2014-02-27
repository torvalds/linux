/**
  * This file contains the major functions in WLAN
  * driver. It includes init, exit, open, close and main thread etc..
  */

#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/kthread.h>
#include <linux/kfifo.h>
#include <linux/stddef.h>
#include <linux/ieee80211.h>
#include <linux/pm.h>
#include <linux/suspend.h>
#include <asm/atomic.h>

#include <net/iw_handler.h>

#include "host.h"
#include "decl.h"
#include "dev.h"
#include "wext.h"
#include "scan.h"
#include "assoc.h"
#include "cmd.h"

#include "wifi_power.h"

/* Module parameters */
unsigned int lbs_debug = 0xFFFFFFFF;
EXPORT_SYMBOL_GPL(lbs_debug);

extern int wifi_customized_mac_addr(u8 *mac);

struct lbs_private *lbs_wifi_priv = NULL;

/* This global structure is used to send the confirm_sleep command as
 * fast as possible down to the firmware. */
struct cmd_confirm_sleep confirm_sleep;

/*
 * When we receive a deauthenticated event, we may get to 
 * re-association to the previous AP. This atom will be set to 1 when
 * we are doing association, else it will be set to 1.
 */
atomic_t reassoc_flag;

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
	{11, 2462, LBS_TX_PWR_US_DEFAULT},
	{12, 2467, LBS_TX_PWR_US_DEFAULT},
	{13, 2472, LBS_TX_PWR_US_DEFAULT},
	{14, 2484, LBS_TX_PWR_US_DEFAULT}
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
	{13, 2472, LBS_TX_PWR_EMEA_DEFAULT},
	{14, 2484, LBS_TX_PWR_EMEA_DEFAULT}
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

/*
 * the structure for channel, frequency and power
 */
struct region_cfp_table 
{
	u8 region;
	struct chan_freq_power *cfp_BG;
	int cfp_no_BG;
};

/**
 * the structure for the mapping between region and CFP
 */
static struct region_cfp_table region_cfp_table[] = 
{
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

#if (ANDROID_POWER_SAVE == 1)
extern unsigned long driver_ps_timeout;

#include <linux/android_power.h>
android_suspend_lock_t wifi_android_lock;
#endif

int wifi_ps_status = 0;

#if (ANDROID_POWER_SAVE == 1)

int wifi_real_suspend(int state)
{
	int ret = 0, i;
	struct lbs_private *priv = lbs_wifi_priv;

	printk("System want WIFI to suspend.\n");

	if (priv == NULL)
	{
		printk("lbs_wifi_priv is NULL.\n");
		return 0;
	}

	if (priv->deepsleep == true)
	{
		printk("Wifi is in deepsleep already.\n");
		return 0;
	}

	wifi_ps_status = WIFI_PS_PRE_SLEEP;

	/*
	 * Block data.
	 */
	netif_stop_queue(priv->dev);
	netif_carrier_off(priv->dev);
	
	/*
	 * Block command is done via lbs_queue_command.
	 */
	
	/*
	 * Waiting for current scan command to be done.
	 */
	for (i = 0; i < 20; i++)
	{
		if (priv->scan_channel == 0)
			break;
		printk("Waiting for scan to be done.\n");
		msleep(500);
	}
	if (i >= 20)
	{
		printk("Waiting for scan to be done timeout.\n");
		ret = -1;
		goto out;
	}
		
	/*
	 * Make sure command queue is empty.
	 */
	for (i = 0; i < 50; i++)
	{
		if (!list_empty(&priv->cmdpendingq))
		{
			printk("Command queue is not empty.\n");
			wake_up_interruptible(&priv->waitq);
		}
		else
			break;
		msleep(100);
	}
	if (i >= 50)
	{
		printk("Waiting for command queue to empty timeout.\n");
		ret = -1;
		goto out;
	}
	
	/*
	 * Make sure no command is being run. 
	 */
	for (i = 0; i < 50; i++)
	{
		if (priv->cur_cmd)
		{
			printk("Command is in process!!!\n");
			msleep(100);
		}
		else
			break;
	}
	if (i >= 50)
	{
		printk("Waiting for current command timeout.\n");
		ret = -1;
		goto out;
	}

    if (priv->surpriseremoved)
    {
        printk("We are going to be removed.\n");
        ret = -1;
        goto out;
    }
    
	ret = lbs_prepare_and_send_command(priv, CMD_802_11_DEEP_SLEEP, 0, 0, 0, NULL);
	if (ret)
	{
		printk("Send DEEP_SLEEP command failed.\n");
		wifi_ps_status = 0;
		goto out;
	}
	wake_up_interruptible(&priv->waitq); //Wake up main thread.
	
	priv->deepsleep = true;
	msleep(800);

    wifi_ps_status = WIFI_PS_DRV_SLEEP;
    
    android_unlock_suspend(&wifi_android_lock);
out:
	netif_carrier_on(priv->dev);
	netif_wake_queue(priv->dev);

	return ret;
}
#endif

/*
 * Suspend request from android power manager.
 */
#if (ANDROID_POWER_SAVE == 1)
int wifi_suspend(suspend_state_t state)
{
	struct lbs_private *priv = lbs_wifi_priv;
	
	if (priv == NULL)
	{
	    printk("WiFi is disabled for now.\n");
	    return 0;
	}
	
	if (wifi_ps_status == WIFI_PS_DRV_SLEEP)
	{
		printk("WiFi is in driver sleep already.\n");
		return 0;
	}
	else /* This should be never occured. */
	{
	    printk("ERROR: WiFi is active for now!!!!!!!!!!\n");
	    return 1;
	}

	return 0;
}
#elif (NEW_MV8686_PS == 1)
int wifi_suspend(suspend_state_t state)
{
	int ret = 0, i;
	struct lbs_private *priv = lbs_wifi_priv;
	
	printk("%s: NEW_MV8686_PS\n", __func__);

	/*
	 * Since system want to be IDLE, we will make WIFI be 
	 * deep sleep here. --Yongle Lai
	 */

	//printk("System want WIFI to suspend.\n");
	
	if (priv == NULL)
	{
		printk("lbs_wifi_priv is NULL.\n");
		return 0;
	}
	
	if (priv->deepsleep == true)
	{
		printk("Wifi is in deepsleep already.\n");
		return 0;
	}
	
	wifi_ps_status = WIFI_PS_PRE_SLEEP;
	
	/*
	 * Block data.
	 */
	netif_stop_queue(priv->dev);
	netif_carrier_off(priv->dev);
		
	/*
	 * Block command is done via lbs_queue_command.
	 */
		
	/*
	 * Waiting for current scan command to be done.
	 */
	for (i = 0; i < 20; i++)
	{
		if (priv->scan_channel == 0)
			break;
		printk("Waiting for scan to be done.\n");
		msleep(500);
	}
	if (i >= 20)
	{
		printk("Waiting for scan to be done timeout.\n");
		ret = -1;
		goto out;
	}
			
	/*
	 * Make sure command queue is empty.
	 */
	for (i = 0; i < 50; i++)
	{
		if (!list_empty(&priv->cmdpendingq))
		{
			printk("Command queue is not empty.\n");
			wake_up_interruptible(&priv->waitq);
		}
		else
			break;
		msleep(100);
	}
	if (i >= 50)
	{
		printk("Waiting for command queue to empty timeout.\n");
		ret = -1;
		goto out;
	}
		
	/*
	 * Make sure no command is being run. 
	 */
	for (i = 0; i < 50; i++)
	{
		if (priv->cur_cmd)
		{
			printk("Command is in process!!!\n");
			msleep(100);
		}
		else
			break;
	}
	if (i >= 50)
	{
		printk("Waiting for current command timeout.\n");
		ret = -1;
		goto out;
	}
	
	if (priv->surpriseremoved)
	{
		printk("We are going to be removed.\n");
		ret = -1;
		goto out;
	}
	/*
	 * Before we go to deep sleep, we should leave PS mode if we are.
	 */
	priv->psmode = LBS802_11POWERMODECAM;
	if (priv->psstate != PS_STATE_FULL_POWER) {
		lbs_ps_wakeup(priv, CMD_OPTION_WAITFORRSP);
	}

	ret = lbs_cmd_80211_deauthenticate(priv,
							   priv->curbssparams.bssid,
							   WLAN_REASON_DEAUTH_LEAVING);
	if (ret)
		printk("%s: Disconnect fail.\n", __func__);
	
	ret = lbs_prepare_and_send_command(priv, CMD_802_11_DEEP_SLEEP, 0, 0, 0, NULL);
	if (ret)
	{
		printk("Send DEEP_SLEEP command failed.\n");
		wifi_ps_status = 0;
		goto out;
	}
	wake_up_interruptible(&priv->waitq); //Wake up main thread.
		
	priv->deepsleep = true;
	msleep(800);
	
	wifi_ps_status = WIFI_PS_DRV_SLEEP;
out:
	netif_carrier_on(priv->dev);
	netif_wake_queue(priv->dev);
	
	return ret;
}
#else
int wifi_suspend(suspend_state_t state)
{
	return 0;
}
#endif

#if (ANDROID_POWER_SAVE == 1)
int wifi_driver_suspend(suspend_state_t state)
{
	int ret;
	
	if ((wifi_ps_status == WIFI_PS_SYS_SLEEP) || 
		  (wifi_ps_status == WIFI_PS_DRV_SLEEP))
	{
		printk("WiFi is in deep sleep already.\n");
		return 0;
	}

	ret = wifi_real_suspend(WIFI_PS_DRV_PRE_SLEEP);
	if (ret == 0)
	{
		wifi_ps_status = WIFI_PS_DRV_SLEEP;
	}
	else
	{
		wifi_ps_status = WIFI_PS_AWAKE;
	}
	
	return ret;
}
#endif

#if (NEW_MV8686_PS == 1)
void wifi_resume(void)
{
	union iwreq_data wrqu;
	struct lbs_private *priv = lbs_wifi_priv;
	
	printk("%s: NEW_MV8686_PS\n", __func__);
	
	if (priv == NULL)
	{
		printk("lbs_wifi_priv is NULL.\n");
		return;
	}
	
	if (priv->deepsleep == false)
	{
		printk("Wifi isn't in deepsleep yet.\n");
		return;
	}

	if ((wifi_ps_status != WIFI_PS_SYS_SLEEP) &&
		  (wifi_ps_status != WIFI_PS_DRV_SLEEP))
	{
		printk("WiFi is not in deep sleep yet.\n");
		return;
	}

	wifi_power_save_suspend();
	
	memset(wrqu.ap_addr.sa_data, 0x00, ETH_ALEN);
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(priv->dev, SIOCGIWAP, &wrqu, NULL);
	wireless_send_event(priv->dev, SIOCGIWAP, &wrqu, NULL);

	lbs_exit_deep_sleep(priv);
	msleep(100);
	priv->deepsleep = false;
	//msleep(600);

	wifi_ps_status = WIFI_PS_AWAKE;
	
	netif_carrier_on(priv->dev);
	netif_wake_queue(priv->dev);
	
	wake_up_interruptible(&priv->waitq); //Wake up main thread.

	wifi_power_save_resume();
}
#else
void wifi_resume(void)
{
	printk("Android want wifi to be resumed.\n");
}
#endif

/*
 * Wakeup Wifi from deepsleep.
 */
#if (ANDROID_POWER_SAVE == 1)

void wifi_driver_resume(void)
{

	struct lbs_private *priv = lbs_wifi_priv;

	printk("System want WIFI to resume.\n");
	
	if (priv == NULL)
	{
		printk("lbs_wifi_priv is NULL.\n");
		return;
	}
	
	if (priv->deepsleep == false)
	{
		printk("Wifi isn't in deepsleep yet.\n");
		return;
	}

	if ((wifi_ps_status != WIFI_PS_SYS_SLEEP) &&
		  (wifi_ps_status != WIFI_PS_DRV_SLEEP))
	{
		printk("WiFi is not in deep sleep yet.\n");
		return;
	}
	
	/*
	 * Update timer.
	 */
	lbs_update_ps_timer();
	
	lbs_exit_deep_sleep(priv);
	priv->deepsleep = false;
	msleep(600);

	wifi_ps_status = WIFI_PS_AWAKE;
	
	android_lock_suspend(&wifi_android_lock);
	
	netif_carrier_on(priv->dev);
	netif_wake_queue(priv->dev);
	
	wake_up_interruptible(&priv->waitq); //Wake up main thread.
}

void lbs_update_ps_timer(void)
{
    struct lbs_private *priv = lbs_wifi_priv;

    if (priv == NULL)
        return;

    if (priv->surpriseremoved)
        return;

    if (wifi_ps_status != WIFI_PS_AWAKE)
        return;

    if (priv->ps_timer.function == NULL)
        return;

    mod_timer(&priv->ps_timer, jiffies + msecs_to_jiffies(driver_ps_timeout));
}

void lbs_ps_worker(struct work_struct *work)
{
	struct lbs_private *priv = lbs_wifi_priv;
	
	printk("lbs_ps_worker\n");
	
	if (priv->wifi_ps_work_req == WIFI_PS_PRE_WAKE)
		wifi_driver_resume();
	else
		wifi_driver_suspend(0);
	
	priv->wifi_ps_work_req = 0;
}

static void ps_timer_fn(unsigned long data)
{
	struct lbs_private *priv = (struct lbs_private *)data;
	
	printk("ps_timer_function.\n");
	
	priv->wifi_ps_work_req = WIFI_PS_DRV_PRE_SLEEP;
	
	queue_delayed_work(priv->work_thread, &priv->ps_work,
					   msecs_to_jiffies(5));
}
#endif

#if (NEW_MV8686_PS == 1)

static void disconnect_timer_fn(unsigned long data)
{
	struct lbs_private *priv = (struct lbs_private *)data;

	if (priv->surpriseremoved)
		return;

	if (priv == NULL)
	{
		printk("lbs_wifi_priv is NULL.\n");
		return;
	}

	/* Here we check the previous association status. */
	if (priv->connect_status == LBS_CONNECTED)
	{
		priv->need_reassoc = 1;
		priv->reassoc_count = 0;
	}
	else
		priv->need_reassoc = 0;
}

extern int wifi_turnoff_timeout; //from wifi_config.c

static void turnoff_timer_fn(unsigned long data)
{
	union iwreq_data wrqu;
	struct lbs_private *priv = (struct lbs_private *)data;
	//static u32 trans_pkts = 0;

	if (priv->surpriseremoved)
		return;
	
	if (priv->connect_status == LBS_CONNECTED)
	{
		//printk("%s: wifi is LBS_CONNECTED.\n", __func__);
		priv->turnoff_idle_count = 0;
	}
	else
	{
		//printk("%s: wifi is LBS_DISCONNECTED.\n", __func__);
		priv->turnoff_idle_count += 1;
	}

	if (priv->turnoff_idle_count > wifi_turnoff_timeout)
	{
		//printk("%s: Send request to Android for turn-off WiFi.\n", __func__);
		memset(wrqu.ap_addr.sa_data, 0xAB, ETH_ALEN);
		wrqu.ap_addr.sa_family = ARPHRD_ETHER;
		wireless_send_event(priv->dev, SIOCGIWAP, &wrqu, NULL);
	}

#if 0
	/*
	 * Monitor WIFI throughput.
	 */
	if (trans_pkts > (priv->dev->stats.tx_packets + 
		              priv->dev->stats.rx_packets))
	{
		trans_pkts = priv->dev->stats.tx_packets + 
		             priv->dev->stats.rx_packets;
	}
	else if (trans_pkts < (priv->dev->stats.tx_packets + 
		                   priv->dev->stats.rx_packets))
	{
		printk("%s: we are serving...\n", __func__);
		trans_pkts = priv->dev->stats.tx_packets + 
		             priv->dev->stats.rx_packets;
	}
	else
	{
		printk("%s: we are idle...\n", __func__);
	}
#endif

    /*
	 * Even we have ask Android to turn off wifi,
	 * we keep timer still for send request again.
	 */
	mod_timer(&priv->turnoff_timer, jiffies + msecs_to_jiffies(10 * 1000));
}

/*
 * Because we are interrupt context, to make sure all
 * functions can be callbacked, we use workqueue to 
 * call handler.
 */

void mv8686_ps_real_callback(struct work_struct *work)
{
	struct lbs_private *priv = lbs_wifi_priv;

	if (priv->surpriseremoved)
		return;

	if (priv == NULL)
	{
		printk("lbs_wifi_priv is NULL.\n");
		return;
	}
	
	if (priv->wifi_state == WIFI_NETWORK_IDLE)
	{
		//printk("%s: we are idle...\n", __func__);

		priv->wifi_state = WIFI_NETWORK_IDLE;
		
		if (priv->psmode != LBS802_11POWERMODECAM)
			return;

		priv->psmode = LBS802_11POWERMODEMAX_PSP;

		if (priv->connect_status == LBS_CONNECTED) {
			lbs_ps_sleep(priv, CMD_OPTION_WAITFORRSP);
		}
	}
	else
	{
		//printk("%s: we are serving...\n", __func__);
		
		priv->psmode = LBS802_11POWERMODECAM;
		if (priv->psstate != PS_STATE_FULL_POWER) {
			lbs_ps_wakeup(priv, CMD_OPTION_WAITFORRSP);
		}
	}
}

int mv8686_ps_callback(int status)
{
	struct lbs_private *priv = lbs_wifi_priv;

	if (priv->surpriseremoved)
		return 0;

	if (priv == NULL)
	{
		printk("lbs_wifi_priv is NULL.\n");
		return 0;
	}

	priv->wifi_state = status;
	
	schedule_delayed_work(&priv->ps_work, msecs_to_jiffies(2));	
	
	return 0;
}
#endif

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

	for (i = 0; i < sizeof(fw_data_rates); i++) 
	{
		if (rate == fw_data_rates[i])
			return i;
	}
	return 0;
}

/**
 *  @brief This function opens the ethX or mshX interface
 *
 *  @param dev     A pointer to net_device structure
 *  @return 	   0 or -EBUSY if monitor mode active
 */
static int lbs_dev_open(struct net_device *dev)
{
	struct lbs_private *priv = GET_PRIV_FROM_NETDEV(dev);
	int ret = 0;

	lbs_deb_enter(LBS_DEB_NET);

	spin_lock_irq(&priv->driver_lock);

	if (priv->monitormode) {
		ret = -EBUSY;
		goto out;
	}

    priv->infra_open = 1;

    if (priv->connect_status == LBS_CONNECTED)
        netif_carrier_on(dev);
    else
        netif_carrier_off(dev);

	if (!priv->tx_pending_len)
		netif_wake_queue(dev);
 out:

	spin_unlock_irq(&priv->driver_lock);
	lbs_deb_leave_args(LBS_DEB_NET, "ret %d", ret);
	
	return ret;
}

/**
 *  @brief This function closes the ethX interface
 *
 *  @param dev     A pointer to net_device structure
 *  @return 	   0
 */
static int lbs_eth_stop(struct net_device *dev)
{
	struct lbs_private *priv = GET_PRIV_FROM_NETDEV(dev);

	lbs_deb_enter(LBS_DEB_NET);

	spin_lock_irq(&priv->driver_lock);
	priv->infra_open = 0;
	netif_stop_queue(dev);
	spin_unlock_irq(&priv->driver_lock);

	//schedule_work(&priv->mcast_work);

	lbs_deb_leave(LBS_DEB_NET);
	return 0;
}

static void lbs_tx_timeout(struct net_device *dev)
{
	struct lbs_private *priv = GET_PRIV_FROM_NETDEV(dev);

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

static int lbs_set_mac_address(struct net_device *dev, void *addr)
{
	int ret = 0;
	struct lbs_private *priv = GET_PRIV_FROM_NETDEV(dev);
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
		printk("set MAC address failed\n");
		goto done;
	}

	memcpy(priv->current_addr, phwaddr->sa_data, ETH_ALEN);
	memcpy(dev->dev_addr, phwaddr->sa_data, ETH_ALEN);

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

int lbs_can_enter_deep_sleep(struct lbs_private *priv)
{
    if (priv->cur_cmd)
	{
        printk("Current command is running.\n");
        return false;
    }

	if (priv->connect_status == LBS_CONNECTED)
	{
		printk("Media is connected.\n");
		//return false;
	}
	
	//if (priv->resp_len[priv->resp_idx])
	//	return false;
	
	return true;	
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
	struct lbs_private *priv = GET_PRIV_FROM_NETDEV(dev);
	wait_queue_t wait;

	lbs_deb_enter(LBS_DEB_THREAD);

	init_waitqueue_entry(&wait, current);

#if (ANDROID_POWER_SAVE == 1)
	mod_timer(&priv->ps_timer, jiffies + msecs_to_jiffies(driver_ps_timeout) + 10 * HZ);
	
	wifi_android_lock.name = "wifidev";
	android_init_suspend_lock(&wifi_android_lock);
	
	android_lock_suspend(&wifi_android_lock);
#endif

#if (NEW_MV8686_PS == 1)
	mod_timer(&priv->turnoff_timer, jiffies + msecs_to_jiffies(10 * 1000));
#endif

	for (;;) 
	{
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
#if (NEW_MV8686_PS == 1)
		else if (priv->deepsleep == true)
			shouldsleep = 1;	/* Deep Sleep mode. Nothing we can do till it wakes */
#endif
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

			if ((++priv->nr_retries > 3) || (priv->surpriseremoved == 1))
            {
				lbs_pr_info("Excessive timeouts submitting command 0x%04x\n",
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
			}
		}
		spin_unlock_irq(&priv->driver_lock);
	}

	del_timer(&priv->command_timer);
	wake_up_all(&priv->cmd_pending);

	lbs_deb_leave(LBS_DEB_THREAD);
	
	return 0;
}

#if 0

static int lbs_suspend_callback(struct lbs_private *priv, unsigned long dummy,
				struct cmd_header *cmd)
{
	lbs_deb_enter(LBS_DEB_FW);

	netif_device_detach(priv->dev);

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

	lbs_deb_leave(LBS_DEB_FW);
}
EXPORT_SYMBOL_GPL(lbs_resume);
#endif

/**
 * @brief This function gets the HW spec from the firmware and sets
 *        some basic parameters.
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

	{
		struct sockaddr addr;
		u8 * mac = (u8 *)(addr.sa_data);
		
		if (wifi_customized_mac_addr(mac) == 1)
		{
			lbs_set_mac_address(priv->dev, &addr);
		}
	}

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

    /* Allocate buffer to store the scan request list */
	bufsize = MAX_SCAN_REQ * sizeof(struct lbs_scan_req);
	priv->scan_req = kzalloc(bufsize, GFP_KERNEL);
	if (!priv->scan_req) {
		lbs_pr_err("Out of memory allocating scan request\n");
		ret = -1;
		goto out;
	}
    
    /* Initialize scan result lists */
	INIT_LIST_HEAD(&priv->scan_req_free_list);
	INIT_LIST_HEAD(&priv->scan_req_list);
	for (i = 0; i < MAX_SCAN_REQ; i++) {
		list_add_tail(&priv->scan_req[i].list,
			      &priv->scan_req_free_list);
	}
	
	memset(priv->current_addr, 0xff, ETH_ALEN);

	priv->connect_status = LBS_DISCONNECTED;
	priv->secinfo.auth_mode = IW_AUTH_ALG_OPEN_SYSTEM;
	priv->mode = IW_MODE_AUTO; //IW_MODE_INFRA; -- Yongle Lai
	priv->curbssparams.channel = DEFAULT_AD_HOC_CHANNEL;
	priv->mac_control = CMD_ACT_MAC_RX_ON | CMD_ACT_MAC_TX_ON;
	priv->radio_on = 1;
	priv->enablehwauto = 1;
	priv->capability = WLAN_CAPABILITY_SHORT_PREAMBLE;
	priv->psmode = LBS802_11POWERMODECAM;
	priv->psstate = PS_STATE_FULL_POWER;

	mutex_init(&priv->lock);

	setup_timer(&priv->command_timer, command_timer_fn,	(unsigned long)priv);

#if (NEW_MV8686_PS == 1)
	setup_timer(&priv->turnoff_timer, turnoff_timer_fn,	(unsigned long)priv);
	priv->turnoff_idle_count = 0;
	wifi_power_save_register_callback(mv8686_ps_callback);
	setup_timer(&priv->disconnect_timer, disconnect_timer_fn,	(unsigned long)priv);
#endif

#if (ANDROID_POWER_SAVE == 1)
	setup_timer(&priv->ps_timer, ps_timer_fn,	(unsigned long)priv);
#endif

	INIT_LIST_HEAD(&priv->cmdfreeq);
	INIT_LIST_HEAD(&priv->cmdpendingq);

	spin_lock_init(&priv->driver_lock);
	init_waitqueue_head(&priv->cmd_pending);

	/* Allocate the command buffers */
	if (lbs_allocate_cmd_buffer(priv)) 
	{
		lbs_pr_err("Out of memory allocating command buffers\n");
		ret = -ENOMEM;
		goto out;
	}
	priv->resp_idx = 0;
	priv->resp_len[0] = priv->resp_len[1] = 0;

	/* Create the event FIFO */
	priv->event_fifo = kfifo_alloc(sizeof(u32) * 16, GFP_KERNEL, NULL);
	if (IS_ERR(priv->event_fifo)) 
	{
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
	
	if (priv->networks)
	    kfree(priv->networks);
    priv->networks = NULL;
    
    if (priv->scan_req)
        kfree(priv->scan_req);
    priv->scan_req = NULL;
    
	lbs_deb_leave(LBS_DEB_MAIN);
}

#if (MV8686_SUPPORT_MCAST == 1)
static int mv8686_add_mcast_addrs(struct cmd_ds_mac_multicast_adr *cmd,			       
	struct net_device *dev, int nr_addrs)
{	
	int i = nr_addrs;	
	struct dev_mc_list *mc_list;	

	if ((dev->flags & (IFF_UP|IFF_MULTICAST)) != (IFF_UP|IFF_MULTICAST))		
		return nr_addrs;	

	//printk("Warning lock is commeted...........\n");	
	//netif_addr_lock_bh(dev);	
	for (mc_list = dev->mc_list; mc_list; mc_list = mc_list->next) 
	{		
		if (mac_in_list(cmd->maclist, nr_addrs, mc_list->dmi_addr)) 
		{			
			lbs_deb_net("mcast address %s:%pM skipped\n", dev->name, mc_list->dmi_addr);			
			continue;		
		}		
		if (i == MRVDRV_MAX_MULTICAST_LIST_SIZE)			
			break;		
		memcpy(&cmd->maclist[6*i], mc_list->dmi_addr, ETH_ALEN);		
		lbs_deb_net("mcast address %s:%pM added to filter\n", dev->name, mc_list->dmi_addr);		
		i++;	
	}	
	//netif_addr_unlock_bh(dev);	

	if (mc_list)		
		return -EOVERFLOW;	

	return i;
}

static void mv8686_set_mcast_worker(struct work_struct *work)
{	
	struct lbs_private *priv = container_of(work, struct lbs_private, mcast_work);	
	struct cmd_ds_mac_multicast_adr mcast_cmd;	
	int dev_flags;	int nr_addrs;	
	int old_mac_control = priv->mac_control;	

	lbs_deb_enter(LBS_DEB_NET);	

	dev_flags = priv->dev->flags;	

	if (dev_flags & IFF_PROMISC) 
	{		
		priv->mac_control |= CMD_ACT_MAC_PROMISCUOUS_ENABLE;		
		priv->mac_control &= ~(CMD_ACT_MAC_ALL_MULTICAST_ENABLE |				       
			                 CMD_ACT_MAC_MULTICAST_ENABLE);		
		goto out_set_mac_control;	
	} 
	else if (dev_flags & IFF_ALLMULTI) 
	{	
do_allmulti:		
		priv->mac_control |= CMD_ACT_MAC_ALL_MULTICAST_ENABLE;		
		priv->mac_control &= ~(CMD_ACT_MAC_PROMISCUOUS_ENABLE |				       
			                 CMD_ACT_MAC_MULTICAST_ENABLE);		
		goto out_set_mac_control;	
	}	

	/* Once for priv->dev, again for priv->mesh_dev if it exists */	
	nr_addrs = mv8686_add_mcast_addrs(&mcast_cmd, priv->dev, 0);	

	if (nr_addrs < 0)		
		goto do_allmulti;	

	if (nr_addrs) 
	{		
		int size = offsetof(struct cmd_ds_mac_multicast_adr, maclist[6*nr_addrs]);		
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

static void mv8686_set_multicast_list(struct net_device *dev)
{	
	struct lbs_private *priv = GET_PRIV_FROM_NETDEV(dev);	

	schedule_work(&priv->mcast_work);
}
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,25)
static const struct net_device_ops lbs_netdev_ops = {
	.ndo_open       = lbs_dev_open,
	.ndo_stop       = lbs_eth_stop,
	.ndo_start_xmit     = lbs_hard_start_xmit,
	.ndo_set_mac_address    = lbs_set_mac_address,
	.ndo_tx_timeout     = lbs_tx_timeout,
#if (MV8686_SUPPORT_MCAST == 1)
	.ndo_set_multicast_list = mv8686_set_multicast_list,
#endif
	//.ndo_change_mtu     = eth_change_mtu,
	//.ndo_validate_addr  = eth_validate_addr,
};
#endif

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
	if (!dev) 
	{
		lbs_pr_err("init ethX device failed\n");
		goto done;
	}
	priv = netdev_priv(dev);

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
	dev->priv = priv;
#else
	dev->ml_priv = priv;
#endif

	if (lbs_init_adapter(priv)) 
	{
		lbs_pr_err("failed to initialize adapter structure.\n");
		goto err_init_adapter;
	}
	
	//We don't like the name as ethN for wireless. Yongle Lai
	dev_alloc_name(dev, "wlan%d");
	
	priv->dev = dev;
	priv->card = card;
	priv->infra_open = 0;

	/* Setup the OS Interface to our functions */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
	dev->open = lbs_dev_open;
	dev->hard_start_xmit = lbs_hard_start_xmit;
	dev->stop = lbs_eth_stop;
	dev->set_mac_address = lbs_set_mac_address;
	dev->tx_timeout = lbs_tx_timeout;
	
	//dev->ethtool_ops = &lbs_ethtool_ops;
	
	//dev->do_ioctl = lbs_do_ioctl; /* Yongle Lai */

#if (MV8686_SUPPORT_MCAST == 1)
	dev->set_multicast_list = mv8686_set_multicast_list;
#endif

#else /* LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25) */
	dev->netdev_ops = &lbs_netdev_ops;
#endif

	dev->watchdog_timeo = 5 * HZ;

#ifdef	WIRELESS_EXT
	dev->wireless_handlers = &lbs_handler_def;
#endif
	dev->flags |= IFF_BROADCAST | IFF_MULTICAST;

	SET_NETDEV_DEV(dev, dmdev);

	lbs_deb_thread("Starting main thread...\n");
	init_waitqueue_head(&priv->waitq);
	
	init_waitqueue_head(&priv->ds_waitq);
	priv->ps_supported = true;
	
#if (AUTO_REASSOC == 1)
	priv->prev_assoc_req = kzalloc(sizeof(struct assoc_request), GFP_KERNEL);
	if (priv->prev_assoc_req == NULL)
	{
		printk("Alloc for prev_assoc_req failed.\n");
	}
#endif

	priv->main_thread = kthread_run(lbs_thread, dev, "lbs_main");
	if (IS_ERR(priv->main_thread)) 
	{
		lbs_deb_thread("Error creating main thread.\n");
		goto err_init_adapter;
	}

	priv->work_thread = create_singlethread_workqueue("lbs_worker");
	INIT_DELAYED_WORK(&priv->assoc_work, lbs_association_worker);
	INIT_DELAYED_WORK(&priv->scan_work, lbs_scan_worker);
#if (NEW_MV8686_PS == 1)
	INIT_DELAYED_WORK(&priv->ps_work, mv8686_ps_real_callback);
#endif

#if (ANDROID_POWER_SAVE == 1)
	INIT_DELAYED_WORK(&priv->ps_work, lbs_ps_worker);
#endif

#if (MV8686_SUPPORT_MCAST == 1)
	INIT_WORK(&priv->mcast_work, mv8686_set_mcast_worker);
#endif
	INIT_WORK(&priv->sync_channel, lbs_sync_channel_worker);

	priv->wol_criteria = 0xffffffff;
	priv->wol_gpio = 0xff;

	lbs_wifi_priv = priv;
	
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

extern int lbs_exit_deep_sleep(struct lbs_private * priv);

void lbs_remove_card(struct lbs_private *priv)
{
	struct net_device *dev = priv->dev;
#if (NEW_MV8686_PS == 0)
	union iwreq_data wrqu;
#endif

	printk("Before lbs_remove_card.\n");
    
	lbs_deb_enter(LBS_DEB_MAIN);

	lbs_wifi_priv = NULL;
	
	dev = priv->dev;

	cancel_delayed_work_sync(&priv->scan_work);
	cancel_delayed_work_sync(&priv->assoc_work);
	//cancel_work_sync(&priv->mcast_work);
#if (ANDROID_POWER_SAVE == 1)
	cancel_delayed_work_sync(&priv->ps_work);
#endif

	/* worker thread destruction blocks on the in-flight command which
	 * should have been cleared already in lbs_stop_card().
	 */
	lbs_deb_main("destroying worker thread\n");
	
	//if (priv->work_thread == NULL)
	//	printk("You want to destroy NULL workqueue.\n");

	if (priv->work_thread != NULL)
		destroy_workqueue(priv->work_thread);
		
	lbs_deb_main("done destroying worker thread\n");

	if (priv->psmode == LBS802_11POWERMODEMAX_PSP) 
	{
		priv->psmode = LBS802_11POWERMODECAM;
		lbs_ps_wakeup(priv, CMD_OPTION_WAITFORRSP);
	}

	/*
	 * In Android, all 0xaa as AP BSSID should be considered
	 * as DRIVER_HUNG, it will restart WIFI.
	 *
	 * Since we are turn-off WiFi, so this event shouldn't be sent.
	 */
#if (NEW_MV8686_PS == 0)
	memset(wrqu.ap_addr.sa_data, 0xaa, ETH_ALEN);
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(priv->dev, SIOCGIWAP, &wrqu, NULL);
#endif

	/* Stop the thread servicing the interrupts */
	priv->surpriseremoved = 1;
	kthread_stop(priv->main_thread);

	lbs_free_adapter(priv);

	priv->dev = NULL;

#if (AUTO_REASSOC == 1)
	if (priv->prev_assoc_req)
		kfree(priv->prev_assoc_req);
#endif

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

//#if (NEW_MV8686_PS == 1)
#if 0
	/*
	 * Kick off a scan to speed up AP list is showing.
	 */
	queue_delayed_work(priv->work_thread, &priv->scan_work, msecs_to_jiffies(1));
#endif

	if (register_netdev(dev)) {
		lbs_pr_err("cannot register ethX device\n");
		goto done;
	}
	
	lbs_update_channel(priv);

	lbs_pr_info("%s: Marvell WLAN 802.11 adapter\n", dev->name);

	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_MAIN, "ret %d", ret);
	return ret;
}
EXPORT_SYMBOL_GPL(lbs_start_card);

void lbs_stop_card(struct lbs_private *priv)
{
	struct net_device *dev;
	struct cmd_ctrl_node *cmdnode;
	unsigned long flags;

	lbs_deb_enter(LBS_DEB_MAIN);

	printk("Before lbs_stop_card.\n");
    
	if (!priv)
		goto out;
	dev = priv->dev;

    netif_stop_queue(dev);
	//netif_carrier_off(dev);

#if (NEW_MV8686_PS == 1)
	del_timer_sync(&priv->turnoff_timer);
	del_timer_sync(&priv->disconnect_timer);
#endif

#if (ANDROID_POWER_SAVE == 1)
    del_timer_sync(&priv->ps_timer);
    
    cancel_delayed_work_sync(&priv->ps_work);
    
    android_unlock_suspend(&wifi_android_lock);
    android_uninit_suspend_lock(&wifi_android_lock);
#endif

#if (MV8686_SUPPORT_MCAST == 1)
	cancel_work_sync(&priv->mcast_work);
#endif

	/* Delete the timeout of the currently processing command */
	del_timer_sync(&priv->command_timer);

	/* Flush pending command nodes */
	spin_lock_irqsave(&priv->driver_lock, flags);
	lbs_deb_main("clearing pending commands\n");
	
	list_for_each_entry(cmdnode, &priv->cmdpendingq, list) 
	{
		cmdnode->result = -ENOENT;
		cmdnode->cmdwaitqwoken = 1;
		wake_up_interruptible(&cmdnode->cmdwait_q);
	}

	/* Flush the command the card is currently processing */
	if (priv->cur_cmd) 
	{
	    printk("Current command is being processed.\n");
		lbs_deb_main("clearing current command\n");
		priv->cur_cmd->result = -ENOENT;
		priv->cur_cmd->cmdwaitqwoken = 1;
		wake_up_interruptible(&priv->cur_cmd->cmdwait_q);
	}
	lbs_deb_main("done clearing commands\n");
	spin_unlock_irqrestore(&priv->driver_lock, flags);

	unregister_netdev(dev);

out:
	lbs_deb_leave(LBS_DEB_MAIN);
}
EXPORT_SYMBOL_GPL(lbs_stop_card);

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

//static int __init lbs_init_module(void)
int mv8686_main_init(void)
{
	lbs_deb_enter(LBS_DEB_MAIN);
	memset(&confirm_sleep, 0, sizeof(confirm_sleep));
	confirm_sleep.hdr.command = cpu_to_le16(CMD_802_11_PS_MODE);
	confirm_sleep.hdr.size = cpu_to_le16(sizeof(confirm_sleep));
	confirm_sleep.action = cpu_to_le16(CMD_SUBCMD_SLEEP_CONFIRMED);

	atomic_set(&reassoc_flag, 0);
	
	lbs_deb_leave(LBS_DEB_MAIN);
	return 0;
}

//static void __exit lbs_exit_module(void)
void mv8686_main_exit(void)
{
	lbs_deb_enter(LBS_DEB_MAIN);

	atomic_set(&reassoc_flag, 0);
	
	lbs_deb_leave(LBS_DEB_MAIN);
}

//module_init(lbs_init_module);
//module_exit(lbs_exit_module);

MODULE_DESCRIPTION("Libertas WLAN Driver Library");
MODULE_AUTHOR("Marvell International Ltd.");
MODULE_LICENSE("GPL");

