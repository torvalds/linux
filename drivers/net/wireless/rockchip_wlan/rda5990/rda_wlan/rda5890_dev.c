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
#include <linux/mmc/sdio_func.h>


#include <net/iw_handler.h>
#include <linux/jiffies.h>


#include "rda5890_defs.h"
#include "rda5890_dev.h"
#include "rda5890_ioctl.h"
#include "rda5890_wid.h"
#include "rda5890_wext.h"
#include "rda5890_txrx.h"
#include "rda5890_if_sdio.h"

int rda5890_sleep_flags = RDA_SLEEP_ENABLE | RDA_SLEEP_PREASSO;

#ifdef WIFI_UNLOCK_SYSTEM
atomic_t   wake_lock_counter;
struct wake_lock sleep_worker_wake_lock;
#endif


int rda5890_init_pm(struct rda5890_private *priv)
{
#ifdef WIFI_POWER_MANAGER
    int ret = 0;
    struct if_sdio_card  *card =  (struct if_sdio_card  *)priv->card;
    
#ifdef WIFI_TEST_MODE
    if(rda_5990_wifi_in_test_mode())
        return 0;
#endif
    if (rda5890_sleep_flags & RDA_SLEEP_ENABLE)
    {
        ret = rda5890_set_pm_mode(priv, 2);
        if(ret < 0)
            goto err;
    }
    if (rda5890_sleep_flags & RDA_SLEEP_PREASSO)
    {
        ret = rda5890_set_preasso_sleep(priv, 0x00800080);
        if(ret < 0)
            goto err;
    }

    sdio_claim_host(card->func);
    sdio_writeb(card->func, 1, IF_SDIO_FUN1_INT_TO_DEV, &ret);
    sdio_release_host(card->func);
    if (ret) {
                RDA5890_ERRP("write FUN1_INT_TO_DEV reg fail\n");
        }

    atomic_inc(&priv->sleep_flag);

err:
    return ret;
#else
    return 0;
#endif
}


int rda5890_disable_self_cts(struct rda5890_private *priv)
{
    int ret = 0;
    
	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
	"Set rda5890_disable_self_cts 0x%02x\n", 0);
    
	ret = rda5890_generic_set_uchar(priv, WID_PTA_MODE, 0);
        if(ret < 0)
            goto err;
	return 0;
  
err:
    return ret;
}

int rda5890_disable_block_bt(struct rda5890_private *priv)
{
    int ret = 0;

    RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
	"Set rda5890_disable_block_bt 0x%02x\n", 0);
    
	ret = rda5890_generic_set_uchar(priv, WID_PTA_BLOCK_BT, 0);
        if(ret < 0)
            goto err;
        
	return 0;
  
err:
    return ret;
}

//rda5890_set_scan_timeout has defined WID_ACTIVE_SCAN_TIME so if you call that func, not need call this
int rda5890_set_active_scan_time(struct rda5890_private *priv)
{
    int ret = 0;
	ret= rda5890_generic_set_ushort(priv, WID_ACTIVE_SCAN_TIME, 200);
        if(ret < 0)
            goto err;
	return 0;
  err:
    return ret;
}

/**
 *  @brief This function opens the ethX or mshX interface
 *
 *  @param dev     A pointer to net_device structure
 *  @return 	   0 or -EBUSY if monitor mode active
 */
static int rda5890_dev_open(struct net_device *dev)
{
	struct rda5890_private *priv = (struct rda5890_private *)netdev_priv(dev);
	int ret = 0;

	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s >>>\n", __func__);

	if (priv->connect_status == MAC_CONNECTED)
		netif_carrier_on(dev);
	else
		netif_carrier_off(dev);

	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s <<<\n", __func__);
	return ret;
}

/**
 *  @brief This function closes the ethX interface
 *
 *  @param dev     A pointer to net_device structure
 *  @return 	   0
 */
static int rda5890_eth_stop(struct net_device *dev)
{
	//struct rda5890_private *priv = (struct rda5890_private *) dev->priv;

	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s >>>\n", __func__);

	netif_stop_queue(dev);

	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s <<<\n", __func__);
	return 0;
}

static void rda5890_tx_timeout(struct net_device *dev)
{
	//struct rda5890_private *priv = (struct rda5890_private *) dev->priv;

	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s >>>\n", __func__);

	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s <<<\n", __func__);
}

/**
 *  @brief This function returns the network statistics
 *
 *  @param dev     A pointer to struct lbs_private structure
 *  @return 	   A pointer to net_device_stats structure
 */
static struct net_device_stats *rda5890_get_stats(struct net_device *dev)
{
	struct rda5890_private *priv = (struct rda5890_private *) netdev_priv(dev);

	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s >>>\n", __func__);

	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s <<<\n", __func__);
	return &priv->stats;
}

static int rda5890_set_mac_address(struct net_device *dev, void *addr)
{
	int ret = 0;
	struct rda5890_private *priv = (struct rda5890_private *) netdev_priv(dev);
	struct sockaddr *phwaddr = addr;

	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s >>>\n", __func__);

	ret = rda5890_set_mac_addr(priv, phwaddr->sa_data);
	if (ret) {
		goto done;
	}
	memcpy(priv->dev->dev_addr, phwaddr->sa_data, ETH_ALEN); 

done:
	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s <<<\n", __func__);
	return ret;
}

static void rda5890_set_multicast_list(struct net_device *dev)
{
	//struct rda5890_private *priv = dev->priv;

	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s >>>\n", __func__);

	//schedule_work(&priv->mcast_work);

	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s <<<\n", __func__);
}

/**
 *  @brief This function checks the conditions and sends packet to IF
 *  layer if everything is ok.
 *
 *  @param priv    A pointer to struct lbs_private structure
 *  @param skb     A pointer to skb which includes TX packet
 *  @return 	   0 or -1
 */
int rda5890_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct rda5890_private *priv = (struct rda5890_private*)netdev_priv(dev);
#ifdef WIFI_TEST_MODE
  if(rda_5990_wifi_in_test_mode())
      return 0;
#endif  //end WIFI_TEST_MODE
	return rda5890_data_tx(priv, skb, dev);
}

static int rda5890_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	int ret = 0;
	struct rda5890_private *priv = netdev_priv(dev);
	unsigned long value;
	char in_buf[MAX_CMD_LEN + 4], out_buf[MAX_CMD_LEN + 4];
	unsigned short in_len, out_len, out_len_ret;

	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s >>>, cmd = %x\n", __func__, cmd);

	switch (cmd) {
	case IOCTL_RDA5890_GET_MAGIC:
		RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
			"IOCTL_RDA5890_GET_MAGIC\n");
		value = RDA5890_MAGIC;
		if (copy_to_user(rq->ifr_data, &value, sizeof(value)))
			ret = -EFAULT; 
		break;  
	case IOCTL_RDA5890_GET_DRIVER_VER:
		RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
			"IOCTL_RDA5890_GET_DRIVER_VER\n");
		value = RDA5890_SDIOWIFI_VER_MAJ << 16 | 
			RDA5890_SDIOWIFI_VER_MIN << 8 |
			RDA5890_SDIOWIFI_VER_BLD;
		if (copy_to_user(rq->ifr_data, &value, sizeof(value)))
			ret = -EFAULT; 
		break;
	case IOCTL_RDA5890_MAC_GET_FW_VER:
		RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
			"IOCTL_RDA5890_MAC_GET_FW_VER\n");
		ret = rda5890_get_fw_ver(priv, &value);
		if (ret)
			break;
		if (copy_to_user(rq->ifr_data, &value, sizeof(value)))
			ret = -EFAULT; 
		break;
	case IOCTL_RDA5890_MAC_WID:
		RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
			"IOCTL_RDA5890_MAC_WID\n");
		if (copy_from_user(in_buf, rq->ifr_data, 4)) {
			ret = -EFAULT;
			break;
		}
		in_len = (unsigned short)(in_buf[0] + (in_buf[1] << 8));
		out_len = (unsigned short)(in_buf[2] + (in_buf[3] << 8));
		RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
			" in_len = %d, out_len = %d\n", in_len, out_len);
		if (copy_from_user(in_buf, rq->ifr_data, 4 + in_len)) {
			ret = -EFAULT;
			break;
		}
		out_len_ret = MAX_CMD_LEN;
		ret = rda5890_wid_request(priv, in_buf + 4, in_len, out_buf + 4, &out_len_ret);
		if (ret) {
			RDA5890_ERRP("rda5890_wid_request, ret = %d\n", ret);
			break;
		}
		RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
			"  out_len_ret = %d\n", out_len_ret);
		if (out_len_ret > out_len) {
			RDA5890_ERRP("No enough buf for wid response\n");
			ret = -ENOMEM;
			break;
		}
		out_buf[2] = (char)(out_len_ret&0x00FF);
		out_buf[3] = (char)((out_len_ret&0xFF00) >> 8);
		if (copy_to_user(rq->ifr_data, out_buf, 4 + out_len_ret))
			ret = -EFAULT; 
		break;
	case IOCTL_RDA5890_SET_WAPI_ASSOC_IE:
		if(copy_from_user(in_buf, rq->ifr_data, 100)) {
			ret = -EFAULT;
			break;
		}
		RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
			"IOCTL_RDA5890_SET_WAPI_ASSOC_IE is  %x %x %x %x \n",
			in_buf[0], in_buf[1],in_buf[2],in_buf[3]);
		rda5890_generic_set_str(priv, WID_WAPI_ASSOC_IE, in_buf ,100);
		break;
	case IOCTL_RDA5890_GET_WAPI_ASSOC_IE:
		rda5890_generic_get_str(priv, WID_WAPI_ASSOC_IE, out_buf, 100);
		if (copy_to_user(rq->ifr_data, out_buf, 100))
		ret = -EFAULT; 
		break;
	default:
		RDA5890_ERRP("unknown cmd 0x%x\n", cmd);
		ret = -EFAULT; 
		break;
	}

	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s <<<\n", __func__);
	return ret;
} 

static int rda5890_init_adapter(struct rda5890_private *priv)
{
	int ret = 0;
	size_t bufsize;
	int i;
    
	mutex_init(&priv->wid_lock);
#ifdef WIFI_POWER_MANAGER    
	atomic_set(&priv->sleep_flag, 0);
#endif
	priv->wid_pending = 0;
	priv->wid_msg_id = 0;

	/* Allocate buffer to store the BSSID list */
	bufsize = RDA5890_MAX_NETWORK_NUM * sizeof(struct bss_descriptor);
	priv->networks = kzalloc(bufsize, GFP_KERNEL);
	if (!priv->networks) {
		RDA5890_ERRP("Out of memory allocating beacons\n");
		ret = -1;
		goto out;
	}

	/* Initialize scan result lists */
	INIT_LIST_HEAD(&priv->network_free_list);
	INIT_LIST_HEAD(&priv->network_list);
	for (i = 0; i < RDA5890_MAX_NETWORK_NUM; i++) {
		list_add_tail(&priv->networks[i].list,
			      &priv->network_free_list);
	}
	priv->scan_running = 0;

	/* Initialize delayed work workers and thread */
	priv->work_thread = create_singlethread_workqueue("rda5890_worker");
	INIT_DELAYED_WORK(&priv->scan_work, rda5890_scan_worker);
	INIT_DELAYED_WORK(&priv->assoc_work, rda5890_assoc_worker);
	INIT_DELAYED_WORK(&priv->assoc_done_work, rda5890_assoc_done_worker);
    INIT_DELAYED_WORK(&priv->wlan_connect_work, rda5890_wlan_connect_worker);

	/* Initialize status */
	priv->connect_status = MAC_DISCONNECTED;
	memset(&priv->curbssparams, 0, sizeof(priv->curbssparams));
	memset(&priv->wstats, 0, sizeof(priv->wstats));

	/* Initialize sec related status */
	priv->assoc_flags = 0;
	priv->secinfo.auth_mode = IW_AUTH_ALG_OPEN_SYSTEM;
	memset(&priv->wep_keys, 0, sizeof(priv->wep_keys));
	memset(&priv->wpa_ie[0], 0, sizeof(priv->wpa_ie));
	priv->wpa_ie_len = 0;
    priv->first_init = 1;

out:
	return ret;
}

static void rda5890_free_adapter(struct rda5890_private *priv)
{
	cancel_delayed_work_sync(&priv->assoc_done_work);
	cancel_delayed_work_sync(&priv->assoc_work);
	cancel_delayed_work_sync(&priv->scan_work);
    cancel_delayed_work_sync(&priv->wlan_connect_work);
	destroy_workqueue(priv->work_thread);

	if(priv->networks)
	    kfree(priv->networks);
	priv->networks = NULL;
}

static const struct net_device_ops rda_netdev_ops = {
	.ndo_open 		= rda5890_dev_open,
	.ndo_stop		= rda5890_eth_stop,
	.ndo_start_xmit		= rda5890_hard_start_xmit,
	.ndo_set_mac_address	= rda5890_set_mac_address,
	.ndo_tx_timeout 	= rda5890_tx_timeout,
	.ndo_get_stats = rda5890_get_stats,
	.ndo_do_ioctl = rda5890_ioctl
};

/**
 * @brief This function adds the card. it will probe the
 * card, allocate the lbs_priv and initialize the device.
 *
 *  @param card    A pointer to card
 *  @return 	   A pointer to struct lbs_private structure
 */
struct rda5890_private *rda5890_add_card(void *card)
{
	struct net_device *dev = NULL;
	struct rda5890_private *priv = NULL;

	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s >>>\n", __func__);

	/* Allocate an Ethernet device and register it */
	dev = alloc_netdev_mq(sizeof(struct rda5890_private), "wlan%d", ether_setup, 1);
	if (!dev) {
		RDA5890_ERRP("alloc_etherdev failed\n");
		goto done;
	}
	priv = netdev_priv(dev);

	if(rda5890_init_adapter(priv)){
		RDA5890_ERRP("rda5890_init_adapter failed\n");
		goto err_init_adapter;
	}

	priv->dev = dev;
	priv->card = card;

	/* Setup the OS Interface to our functions */
	dev->netdev_ops = &rda_netdev_ops;
	dev->watchdog_timeo = msecs_to_jiffies(450); //450ms

	//dev->ethtool_ops = &lbs_ethtool_ops;
#ifdef	WIRELESS_EXT
	dev->wireless_handlers = (struct iw_handler_def *)&rda5890_wext_handler_def;
#endif
	dev->flags |= IFF_BROADCAST | IFF_MULTICAST;

	goto done;

err_init_adapter:
    if(priv)
	    rda5890_free_adapter(priv);
    if(dev)
	    free_netdev(dev);
	priv = NULL;

done:
	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s <<<\n", __func__);
	return priv;
}

void rda5890_remove_card(struct rda5890_private *priv)
{
	struct net_device *dev = priv->dev;

	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s >>>\n", __func__);

	dev = priv->dev;

	rda5890_free_adapter(priv);

	priv->dev = NULL;
    if(dev)
	    free_netdev(dev);

	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s <<<\n", __func__);
}

int rda5890_start_card(struct rda5890_private *priv)
{
	struct net_device *dev = priv->dev;
	int ret = 0;
    unsigned char mac_addr[ETH_ALEN];

    RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
    	"%s <<<\n", __func__);

#ifdef WIFI_TEST_MODE
    if(!rda_5990_wifi_in_test_mode())
    {
#endif

#ifdef USE_MAC_DYNAMIC_ONCE
        if(rda5890_read_mac(mac_addr) != ETH_ALEN)
        {
            random_ether_addr(mac_addr);
            rda5890_write_mac(mac_addr);
        }

#else
        mac_addr[0] = 0x00;
        mac_addr[1] = 0xc0;
        mac_addr[2] = 0x52;

        mac_addr[3] = 0x00;
        mac_addr[4] = 0xc0;
        mac_addr[5] = 0x53;	
#endif
    	ret = rda5890_set_mac_addr(priv, mac_addr);
    	if (ret) 
        {
            goto done;
        }        

    	ret = rda5890_get_mac_addr(priv, mac_addr);
    	if (ret) {
    		goto done;
    	}
    	memcpy(priv->dev->dev_addr, mac_addr, ETH_ALEN); 

#ifdef WIFI_TEST_MODE
    }
#endif

	if (register_netdev(dev)) {
		RDA5890_ERRP("register_netdev failed\n");
		goto done;
	}
    
#ifdef WIFI_TEST_MODE
    if(!rda_5990_wifi_in_test_mode())
    {
#endif

        rda5890_set_preamble(priv, G_AUTO_PREAMBLE);
        
    	rda5890_indicate_disconnected(priv);
#ifdef WIFI_TEST_MODE
    }
#endif


done:
	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s >>>\n", __func__);
	return ret;
}

void rda5890_stop_card(struct rda5890_private *priv)
{
	struct net_device *dev = priv->dev;

	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s >>>\n", __func__);

	rda5890_indicate_disconnected(priv);
	unregister_netdev(dev);

	RDA5890_DBGLAP(RDA5890_DA_ETHER, RDA5890_DL_DEBUG,
		"%s <<<\n", __func__);
}

void rda5890_shedule_timeout(int msecs)
{
    int timeout = 0, expires = 0;
    expires = jiffies + msecs_to_jiffies(msecs);
    timeout = msecs;

    while(timeout)
    {
        timeout = schedule_timeout(timeout);
        
        if(time_after(jiffies, expires))
            break;
    }
}

#ifdef WIFI_UNLOCK_SYSTEM

void rda5990_wakeLock(void)
{
    if(atomic_read(&wake_lock_counter) == 0)
        wake_lock(&sleep_worker_wake_lock);
    atomic_inc(&wake_lock_counter);
}

void rda5990_wakeUnlock(void)
{

    if(atomic_read(&wake_lock_counter) == 1)
    {
        atomic_set(&wake_lock_counter, 0);
        wake_unlock(&sleep_worker_wake_lock);        
    }
    else if(atomic_read(&wake_lock_counter) > 0)
    {
        atomic_dec(&wake_lock_counter);
    }
    
}

void rda5990_wakeLock_destroy(void)
{
    if(atomic_read(&wake_lock_counter) > 0)
    {
        atomic_set(&wake_lock_counter, 0);
        wake_unlock(&sleep_worker_wake_lock);  
    }
    
    wake_lock_destroy(&sleep_worker_wake_lock);
    
}

#endif

