#include "wilc_wfi_cfgoperations.h"
#include "linux_wlan_common.h"
#include "wilc_wlan_if.h"
#include "wilc_wlan.h"

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>

#include <linux/kthread.h>
#include <linux/firmware.h>
#include <linux/delay.h>

#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>

#include <linux/semaphore.h>

static int dev_state_ev_handler(struct notifier_block *this, unsigned long event, void *ptr);

static struct notifier_block g_dev_notifier = {
	.notifier_call = dev_state_ev_handler
};

#define IRQ_WAIT	1
#define IRQ_NO_WAIT	0
static struct semaphore close_exit_sync;

static int wlan_deinit_locks(struct net_device *dev);
static void wlan_deinitialize_threads(struct net_device *dev);

static void linux_wlan_tx_complete(void *priv, int status);
static int  mac_init_fn(struct net_device *ndev);
static struct net_device_stats *mac_stats(struct net_device *dev);
static int  mac_ioctl(struct net_device *ndev, struct ifreq *req, int cmd);
static void wilc_set_multicast_list(struct net_device *dev);

bool wilc_enable_ps = true;

static const struct net_device_ops wilc_netdev_ops = {
	.ndo_init = mac_init_fn,
	.ndo_open = wilc_mac_open,
	.ndo_stop = wilc_mac_close,
	.ndo_start_xmit = wilc_mac_xmit,
	.ndo_do_ioctl = mac_ioctl,
	.ndo_get_stats = mac_stats,
	.ndo_set_rx_mode  = wilc_set_multicast_list,

};

static int dev_state_ev_handler(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct in_ifaddr *dev_iface = (struct in_ifaddr *)ptr;
	struct wilc_priv *priv;
	struct host_if_drv *hif_drv;
	struct net_device *dev;
	u8 *ip_addr_buf;
	struct wilc_vif *vif;
	u8 null_ip[4] = {0};
	char wlan_dev_name[5] = "wlan0";

	if (!dev_iface || !dev_iface->ifa_dev || !dev_iface->ifa_dev->dev) {
		PRINT_D(GENERIC_DBG, "dev_iface = NULL\n");
		return NOTIFY_DONE;
	}

	if (memcmp(dev_iface->ifa_label, "wlan0", 5) &&
	    memcmp(dev_iface->ifa_label, "p2p0", 4)) {
		PRINT_D(GENERIC_DBG, "Interface is neither WLAN0 nor P2P0\n");
		return NOTIFY_DONE;
	}

	dev  = (struct net_device *)dev_iface->ifa_dev->dev;
	if (!dev->ieee80211_ptr || !dev->ieee80211_ptr->wiphy) {
		PRINT_D(GENERIC_DBG, "No Wireless registerd\n");
		return NOTIFY_DONE;
	}
	priv = wiphy_priv(dev->ieee80211_ptr->wiphy);
	if (!priv) {
		PRINT_D(GENERIC_DBG, "No Wireless Priv\n");
		return NOTIFY_DONE;
	}
	hif_drv = (struct host_if_drv *)priv->hWILCWFIDrv;
	vif = netdev_priv(dev);
	if (!vif || !hif_drv) {
		PRINT_D(GENERIC_DBG, "No Wireless Priv\n");
		return NOTIFY_DONE;
	}

	PRINT_INFO(GENERIC_DBG, "dev_state_ev_handler +++\n");

	switch (event) {
	case NETDEV_UP:
		PRINT_D(GENERIC_DBG, "dev_state_ev_handler event=NETDEV_UP %p\n", dev);

		PRINT_INFO(GENERIC_DBG, "\n ============== IP Address Obtained ===============\n\n");

		if (vif->iftype == STATION_MODE || vif->iftype == CLIENT_MODE) {
			hif_drv->IFC_UP = 1;
			wilc_optaining_ip = false;
			del_timer(&wilc_during_ip_timer);
			PRINT_D(GENERIC_DBG, "IP obtained , enable scan\n");
		}

		if (wilc_enable_ps)
			wilc_set_power_mgmt(vif, 1, 0);

		PRINT_D(GENERIC_DBG, "[%s] Up IP\n", dev_iface->ifa_label);

		ip_addr_buf = (char *)&dev_iface->ifa_address;
		PRINT_D(GENERIC_DBG, "IP add=%d:%d:%d:%d\n",
			ip_addr_buf[0], ip_addr_buf[1],
			ip_addr_buf[2], ip_addr_buf[3]);
		wilc_setup_ipaddress(vif, ip_addr_buf, vif->u8IfIdx);

		break;

	case NETDEV_DOWN:
		PRINT_D(GENERIC_DBG, "dev_state_ev_handler event=NETDEV_DOWN %p\n", dev);

		PRINT_INFO(GENERIC_DBG, "\n ============== IP Address Released ===============\n\n");
		if (vif->iftype == STATION_MODE || vif->iftype == CLIENT_MODE) {
			hif_drv->IFC_UP = 0;
			wilc_optaining_ip = false;
		}

		if (memcmp(dev_iface->ifa_label, wlan_dev_name, 5) == 0)
			wilc_set_power_mgmt(vif, 0, 0);

		wilc_resolve_disconnect_aberration(vif);

		PRINT_D(GENERIC_DBG, "[%s] Down IP\n", dev_iface->ifa_label);

		ip_addr_buf = null_ip;
		PRINT_D(GENERIC_DBG, "IP add=%d:%d:%d:%d\n",
			ip_addr_buf[0], ip_addr_buf[1],
			ip_addr_buf[2], ip_addr_buf[3]);

		wilc_setup_ipaddress(vif, ip_addr_buf, vif->u8IfIdx);

		break;

	default:
		PRINT_INFO(GENERIC_DBG, "dev_state_ev_handler event=default\n");
		PRINT_INFO(GENERIC_DBG, "[%s] unknown dev event: %lu\n", dev_iface->ifa_label, event);

		break;
	}

	return NOTIFY_DONE;
}

static irqreturn_t isr_uh_routine(int irq, void *user_data)
{
	struct wilc_vif *vif;
	struct wilc *wilc;
	struct net_device *dev = (struct net_device *)user_data;

	vif = netdev_priv(dev);
	wilc = vif->wilc;
	PRINT_D(INT_DBG, "Interrupt received UH\n");

	if (wilc->close) {
		PRINT_ER("Driver is CLOSING: Can't handle UH interrupt\n");
		return IRQ_HANDLED;
	}
	return IRQ_WAKE_THREAD;
}

static irqreturn_t isr_bh_routine(int irq, void *userdata)
{
	struct wilc_vif *vif;
	struct wilc *wilc;

	vif = netdev_priv(userdata);
	wilc = vif->wilc;

	if (wilc->close) {
		PRINT_ER("Driver is CLOSING: Can't handle BH interrupt\n");
		return IRQ_HANDLED;
	}

	PRINT_D(INT_DBG, "Interrupt received BH\n");
	wilc_handle_isr(wilc);

	return IRQ_HANDLED;
}

static int init_irq(struct net_device *dev)
{
	int ret = 0;
	struct wilc_vif *vif;
	struct wilc *wl;

	vif = netdev_priv(dev);
	wl = vif->wilc;

	if ((gpio_request(wl->gpio, "WILC_INTR") == 0) &&
	    (gpio_direction_input(wl->gpio) == 0)) {
		wl->dev_irq_num = gpio_to_irq(wl->gpio);
	} else {
		ret = -1;
		PRINT_ER("could not obtain gpio for WILC_INTR\n");
	}

	if (ret != -1 && request_threaded_irq(wl->dev_irq_num,
					      isr_uh_routine,
					      isr_bh_routine,
					      IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					      "WILC_IRQ", dev) < 0) {
		PRINT_ER("Failed to request IRQ for GPIO: %d\n", wl->gpio);
		gpio_free(wl->gpio);
		ret = -1;
	} else {
		PRINT_D(INIT_DBG, "IRQ request succeeded IRQ-NUM= %d on GPIO: %d\n",
			wl->dev_irq_num, wl->gpio);
	}

	return ret;
}

static void deinit_irq(struct net_device *dev)
{
	struct wilc_vif *vif;
	struct wilc *wilc;

	vif = netdev_priv(dev);
	wilc = vif->wilc;

	/* Deintialize IRQ */
	if (wilc->dev_irq_num) {
		free_irq(wilc->dev_irq_num, wilc);
		gpio_free(wilc->gpio);
	}
}

void wilc_dbg(u8 *buff)
{
	PRINT_D(INIT_DBG, "%d\n", *buff);
}

int wilc_lock_timeout(struct wilc *nic, void *vp, u32 timeout)
{
	/* FIXME: replace with mutex_lock or wait_for_completion */
	int error = -1;

	PRINT_D(LOCK_DBG, "Locking %p\n", vp);
	if (vp)
		error = down_timeout((struct semaphore *)vp,
				     msecs_to_jiffies(timeout));
	else
		PRINT_ER("Failed, mutex is NULL\n");
	return error;
}

void wilc_mac_indicate(struct wilc *wilc, int flag)
{
	int status;

	if (flag == WILC_MAC_INDICATE_STATUS) {
		wilc_wlan_cfg_get_val(WID_STATUS,
				      (unsigned char *)&status, 4);
		if (wilc->mac_status == WILC_MAC_STATUS_INIT) {
			wilc->mac_status = status;
			up(&wilc->sync_event);
		} else {
			wilc->mac_status = status;
		}
	} else if (flag == WILC_MAC_INDICATE_SCAN) {
		PRINT_D(GENERIC_DBG, "Scanning ...\n");
	}
}

static struct net_device *get_if_handler(struct wilc *wilc, u8 *mac_header)
{
	u8 *bssid, *bssid1;
	int i = 0;

	bssid = mac_header + 10;
	bssid1 = mac_header + 4;

	for (i = 0; i < wilc->vif_num; i++)
		if (!memcmp(bssid1, wilc->vif[i]->bssid, ETH_ALEN) ||
		    !memcmp(bssid, wilc->vif[i]->bssid, ETH_ALEN))
			return wilc->vif[i]->ndev;

	PRINT_INFO(INIT_DBG, "Invalide handle\n");
	for (i = 0; i < 25; i++)
		PRINT_D(INIT_DBG, "%02x ", mac_header[i]);
	bssid = mac_header + 18;
	bssid1 = mac_header + 12;
	for (i = 0; i < wilc->vif_num; i++)
		if (!memcmp(bssid1, wilc->vif[i]->bssid, ETH_ALEN) ||
		    !memcmp(bssid, wilc->vif[i]->bssid, ETH_ALEN))
			return wilc->vif[i]->ndev;

	PRINT_INFO(INIT_DBG, "\n");
	return NULL;
}

int wilc_wlan_set_bssid(struct net_device *wilc_netdev, u8 *bssid)
{
	int i = 0;
	int ret = -1;
	struct wilc_vif *vif;
	struct wilc *wilc;

	vif = netdev_priv(wilc_netdev);
	wilc = vif->wilc;

	for (i = 0; i < wilc->vif_num; i++)
		if (wilc->vif[i]->ndev == wilc_netdev) {
			memcpy(wilc->vif[i]->bssid, bssid, 6);
			ret = 0;
			break;
		}

	return ret;
}

int wilc_wlan_get_num_conn_ifcs(struct wilc *wilc)
{
	u8 i = 0;
	u8 null_bssid[6] = {0};
	u8 ret_val = 0;

	for (i = 0; i < wilc->vif_num; i++)
		if (memcmp(wilc->vif[i]->bssid, null_bssid, 6))
			ret_val++;

	return ret_val;
}

#define USE_TX_BACKOFF_DELAY_IF_NO_BUFFERS

static int linux_wlan_txq_task(void *vp)
{
	int ret, txq_count;
	struct wilc_vif *vif;
	struct wilc *wl;
	struct net_device *dev = vp;
#if defined USE_TX_BACKOFF_DELAY_IF_NO_BUFFERS
#define TX_BACKOFF_WEIGHT_INCR_STEP (1)
#define TX_BACKOFF_WEIGHT_DECR_STEP (1)
#define TX_BACKOFF_WEIGHT_MAX (7)
#define TX_BACKOFF_WEIGHT_MIN (0)
#define TX_BACKOFF_WEIGHT_UNIT_MS (10)
	int backoff_weight = TX_BACKOFF_WEIGHT_MIN;
#endif

	vif = netdev_priv(dev);
	wl = vif->wilc;

	up(&wl->txq_thread_started);
	while (1) {
		PRINT_D(TX_DBG, "txq_task Taking a nap :)\n");
		down(&wl->txq_event);
		PRINT_D(TX_DBG, "txq_task Who waked me up :$\n");

		if (wl->close) {
			up(&wl->txq_thread_started);

			while (!kthread_should_stop())
				schedule();

			PRINT_D(TX_DBG, "TX thread stopped\n");
			break;
		}
		PRINT_D(TX_DBG, "txq_task handle the sending packet and let me go to sleep.\n");
#if !defined USE_TX_BACKOFF_DELAY_IF_NO_BUFFERS
		ret = wilc_wlan_handle_txq(dev, &txq_count);
#else
		do {
			ret = wilc_wlan_handle_txq(dev, &txq_count);
			if (txq_count < FLOW_CONTROL_LOWER_THRESHOLD) {
				PRINT_D(TX_DBG, "Waking up queue\n");

				if (netif_queue_stopped(wl->vif[0]->ndev))
					netif_wake_queue(wl->vif[0]->ndev);
				if (netif_queue_stopped(wl->vif[1]->ndev))
					netif_wake_queue(wl->vif[1]->ndev);
			}

			if (ret == WILC_TX_ERR_NO_BUF) {
				do {
					msleep(TX_BACKOFF_WEIGHT_UNIT_MS << backoff_weight);
				} while (0);
				backoff_weight += TX_BACKOFF_WEIGHT_INCR_STEP;
				if (backoff_weight > TX_BACKOFF_WEIGHT_MAX)
					backoff_weight = TX_BACKOFF_WEIGHT_MAX;
			} else {
				if (backoff_weight > TX_BACKOFF_WEIGHT_MIN) {
					backoff_weight -= TX_BACKOFF_WEIGHT_DECR_STEP;
					if (backoff_weight < TX_BACKOFF_WEIGHT_MIN)
						backoff_weight = TX_BACKOFF_WEIGHT_MIN;
				}
			}
		} while (ret == WILC_TX_ERR_NO_BUF && !wl->close);
#endif
	}
	return 0;
}

void wilc_rx_complete(struct wilc *nic)
{
	PRINT_D(RX_DBG, "RX completed\n");
}

int wilc_wlan_get_firmware(struct net_device *dev)
{
	struct wilc_vif *vif;
	struct wilc *wilc;
	int ret = 0;
	const struct firmware *wilc_firmware;
	char *firmware;

	vif = netdev_priv(dev);
	wilc = vif->wilc;

	if (vif->iftype == AP_MODE) {
		firmware = AP_FIRMWARE;
	} else if (vif->iftype == STATION_MODE) {
		firmware = STA_FIRMWARE;
	} else {
		PRINT_D(INIT_DBG, "Get P2P_CONCURRENCY_FIRMWARE\n");
		firmware = P2P_CONCURRENCY_FIRMWARE;
	}

	if (!vif) {
		PRINT_ER("vif is NULL\n");
		goto _fail_;
	}

	if (!(&vif->ndev->dev)) {
		PRINT_ER("&vif->ndev->dev  is NULL\n");
		goto _fail_;
	}

	if (request_firmware(&wilc_firmware, firmware, wilc->dev) != 0) {
		PRINT_ER("%s - firmare not available\n", firmware);
		ret = -1;
		goto _fail_;
	}
	wilc->firmware = wilc_firmware;

_fail_:

	return ret;
}

static int linux_wlan_start_firmware(struct net_device *dev)
{
	struct wilc_vif *vif;
	struct wilc *wilc;
	int ret = 0;

	vif = netdev_priv(dev);
	wilc = vif->wilc;

	PRINT_D(INIT_DBG, "Starting Firmware ...\n");
	ret = wilc_wlan_start(wilc);
	if (ret < 0) {
		PRINT_ER("Failed to start Firmware\n");
		return ret;
	}

	PRINT_D(INIT_DBG, "Waiting for Firmware to get ready ...\n");
	ret = wilc_lock_timeout(wilc, &wilc->sync_event, 5000);
	if (ret) {
		PRINT_D(INIT_DBG, "Firmware start timed out");
		return ret;
	}
	PRINT_D(INIT_DBG, "Firmware successfully started\n");

	return 0;
}

static int wilc1000_firmware_download(struct net_device *dev)
{
	struct wilc_vif *vif;
	struct wilc *wilc;
	int ret = 0;

	vif = netdev_priv(dev);
	wilc = vif->wilc;

	if (!wilc->firmware) {
		PRINT_ER("Firmware buffer is NULL\n");
		return -ENOBUFS;
	}
	PRINT_D(INIT_DBG, "Downloading Firmware ...\n");
	ret = wilc_wlan_firmware_download(wilc, wilc->firmware->data,
					  wilc->firmware->size);
	if (ret < 0)
		return ret;

	PRINT_D(INIT_DBG, "Freeing FW buffer ...\n");
	PRINT_D(INIT_DBG, "Releasing firmware\n");
	release_firmware(wilc->firmware);
	wilc->firmware = NULL;

	PRINT_D(INIT_DBG, "Download Succeeded\n");

	return 0;
}

static int linux_wlan_init_test_config(struct net_device *dev,
				       struct wilc *wilc)
{
	unsigned char c_val[64];
	unsigned char mac_add[] = {0x00, 0x80, 0xC2, 0x5E, 0xa2, 0xff};

	struct wilc_priv *priv;
	struct host_if_drv *hif_drv;

	PRINT_D(TX_DBG, "Start configuring Firmware\n");
	get_random_bytes(&mac_add[5], 1);
	get_random_bytes(&mac_add[4], 1);
	priv = wiphy_priv(dev->ieee80211_ptr->wiphy);
	hif_drv = (struct host_if_drv *)priv->hWILCWFIDrv;
	PRINT_D(INIT_DBG, "Host = %p\n", hif_drv);

	PRINT_D(INIT_DBG, "MAC address is : %02x-%02x-%02x-%02x-%02x-%02x\n",
		mac_add[0], mac_add[1], mac_add[2],
		mac_add[3], mac_add[4], mac_add[5]);
	wilc_get_chipid(wilc, 0);

	*(int *)c_val = 1;

	if (!wilc_wlan_cfg_set(wilc, 1, WID_SET_DRV_HANDLER, c_val, 4, 0, 0))
		goto _fail_;

	c_val[0] = 0;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_PC_TEST_MODE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = INFRASTRUCTURE;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_BSS_TYPE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = RATE_AUTO;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_CURRENT_TX_RATE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = G_MIXED_11B_2_MODE;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_11G_OPERATING_MODE, c_val, 1, 0,
			       0))
		goto _fail_;

	c_val[0] = 1;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_CURRENT_CHANNEL, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = G_SHORT_PREAMBLE;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_PREAMBLE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = AUTO_PROT;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_11N_PROT_MECH, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = ACTIVE_SCAN;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_SCAN_TYPE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = SITE_SURVEY_OFF;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_SITE_SURVEY, c_val, 1, 0, 0))
		goto _fail_;

	*((int *)c_val) = 0xffff;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_RTS_THRESHOLD, c_val, 2, 0, 0))
		goto _fail_;

	*((int *)c_val) = 2346;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_FRAG_THRESHOLD, c_val, 2, 0, 0))
		goto _fail_;

	c_val[0] = 0;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_BCAST_SSID, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = 1;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_QOS_ENABLE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = NO_POWERSAVE;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_POWER_MANAGEMENT, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = NO_SECURITY; /* NO_ENCRYPT, 0x79 */
	if (!wilc_wlan_cfg_set(wilc, 0, WID_11I_MODE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = OPEN_SYSTEM;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_AUTH_TYPE, c_val, 1, 0, 0))
		goto _fail_;

	strcpy(c_val, "123456790abcdef1234567890");
	if (!wilc_wlan_cfg_set(wilc, 0, WID_WEP_KEY_VALUE, c_val,
			       (strlen(c_val) + 1), 0, 0))
		goto _fail_;

	strcpy(c_val, "12345678");
	if (!wilc_wlan_cfg_set(wilc, 0, WID_11I_PSK, c_val, (strlen(c_val)), 0,
			       0))
		goto _fail_;

	strcpy(c_val, "password");
	if (!wilc_wlan_cfg_set(wilc, 0, WID_1X_KEY, c_val, (strlen(c_val) + 1),
			       0, 0))
		goto _fail_;

	c_val[0] = 192;
	c_val[1] = 168;
	c_val[2] = 1;
	c_val[3] = 112;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_1X_SERV_ADDR, c_val, 4, 0, 0))
		goto _fail_;

	c_val[0] = 3;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_LISTEN_INTERVAL, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = 3;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_DTIM_PERIOD, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = NORMAL_ACK;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_ACK_POLICY, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = 0;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_USER_CONTROL_ON_TX_POWER, c_val, 1,
			       0, 0))
		goto _fail_;

	c_val[0] = 48;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_TX_POWER_LEVEL_11A, c_val, 1, 0,
			       0))
		goto _fail_;

	c_val[0] = 28;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_TX_POWER_LEVEL_11B, c_val, 1, 0,
			       0))
		goto _fail_;

	*((int *)c_val) = 100;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_BEACON_INTERVAL, c_val, 2, 0, 0))
		goto _fail_;

	c_val[0] = REKEY_DISABLE;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_REKEY_POLICY, c_val, 1, 0, 0))
		goto _fail_;

	*((int *)c_val) = 84600;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_REKEY_PERIOD, c_val, 4, 0, 0))
		goto _fail_;

	*((int *)c_val) = 500;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_REKEY_PACKET_COUNT, c_val, 4, 0,
			       0))
		goto _fail_;

	c_val[0] = 1;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_SHORT_SLOT_ALLOWED, c_val, 1, 0,
			       0))
		goto _fail_;

	c_val[0] = G_SELF_CTS_PROT;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_11N_ERP_PROT_TYPE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = 1;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_11N_ENABLE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = HT_MIXED_MODE;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_11N_OPERATING_MODE, c_val, 1, 0,
			       0))
		goto _fail_;

	c_val[0] = 1;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_11N_TXOP_PROT_DISABLE, c_val, 1, 0,
			       0))
		goto _fail_;

	memcpy(c_val, mac_add, 6);

	if (!wilc_wlan_cfg_set(wilc, 0, WID_MAC_ADDR, c_val, 6, 0, 0))
		goto _fail_;

	c_val[0] = DETECT_PROTECT_REPORT;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_11N_OBSS_NONHT_DETECTION, c_val, 1,
			       0, 0))
		goto _fail_;

	c_val[0] = RTS_CTS_NONHT_PROT;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_11N_HT_PROT_TYPE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = 0;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_11N_RIFS_PROT_ENABLE, c_val, 1, 0,
			       0))
		goto _fail_;

	c_val[0] = MIMO_MODE;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_11N_SMPS_MODE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = 7;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_11N_CURRENT_TX_MCS, c_val, 1, 0,
			       0))
		goto _fail_;

	c_val[0] = 1;
	if (!wilc_wlan_cfg_set(wilc, 0, WID_11N_IMMEDIATE_BA_ENABLED, c_val, 1,
			       1, 1))
		goto _fail_;

	return 0;

_fail_:
	return -1;
}

void wilc1000_wlan_deinit(struct net_device *dev)
{
	struct wilc_vif *vif;
	struct wilc *wl;

	vif = netdev_priv(dev);
	wl = vif->wilc;

	if (!wl) {
		netdev_err(dev, "wl is NULL\n");
		return;
	}

	if (wl->initialized)	{
		netdev_info(dev, "Deinitializing wilc1000...\n");

		PRINT_D(INIT_DBG, "Disabling IRQ\n");
		if (!wl->dev_irq_num &&
		    wl->hif_func->disable_interrupt) {
			mutex_lock(&wl->hif_cs);
			wl->hif_func->disable_interrupt(wl);
			mutex_unlock(&wl->hif_cs);
		}
		if (&wl->txq_event)
			up(&wl->txq_event);

		PRINT_D(INIT_DBG, "Deinitializing Threads\n");
		wlan_deinitialize_threads(dev);

		PRINT_D(INIT_DBG, "Deinitializing IRQ\n");
		deinit_irq(dev);

		wilc_wlan_stop(wl);

		PRINT_D(INIT_DBG, "Deinitializing WILC Wlan\n");
		wilc_wlan_cleanup(dev);
#if defined(PLAT_ALLWINNER_A20) || defined(PLAT_ALLWINNER_A23) || defined(PLAT_ALLWINNER_A31)
		if (!wl->dev_irq_num &&
		    wl->hif_func->disable_interrupt) {

			PRINT_D(INIT_DBG, "Disabling IRQ 2\n");

			mutex_lock(&wl->hif_cs);
			wl->hif_func->disable_interrupt(wl);
			mutex_unlock(&wl->hif_cs);
		}
#endif

		PRINT_D(INIT_DBG, "Deinitializing Locks\n");
		wlan_deinit_locks(dev);

		wl->initialized = false;

		PRINT_D(INIT_DBG, "wilc1000 deinitialization Done\n");

	} else {
		PRINT_D(INIT_DBG, "wilc1000 is not initialized\n");
	}
}

static int wlan_init_locks(struct net_device *dev)
{
	struct wilc_vif *vif;
	struct wilc *wl;

	vif = netdev_priv(dev);
	wl = vif->wilc;

	PRINT_D(INIT_DBG, "Initializing Locks ...\n");

	mutex_init(&wl->hif_cs);
	mutex_init(&wl->rxq_cs);

	spin_lock_init(&wl->txq_spinlock);
	sema_init(&wl->txq_add_to_head_cs, 1);

	sema_init(&wl->txq_event, 0);

	sema_init(&wl->cfg_event, 0);
	sema_init(&wl->sync_event, 0);

	sema_init(&wl->txq_thread_started, 0);

	return 0;
}

static int wlan_deinit_locks(struct net_device *dev)
{
	struct wilc_vif *vif;
	struct wilc *wilc;

	vif = netdev_priv(dev);
	wilc = vif->wilc;

	PRINT_D(INIT_DBG, "De-Initializing Locks\n");

	if (&wilc->hif_cs)
		mutex_destroy(&wilc->hif_cs);

	if (&wilc->rxq_cs)
		mutex_destroy(&wilc->rxq_cs);

	return 0;
}

static int wlan_initialize_threads(struct net_device *dev)
{
	struct wilc_vif *vif;
	struct wilc *wilc;

	vif = netdev_priv(dev);
	wilc = vif->wilc;

	PRINT_D(INIT_DBG, "Initializing Threads ...\n");
	PRINT_D(INIT_DBG, "Creating kthread for transmission\n");
	wilc->txq_thread = kthread_run(linux_wlan_txq_task, (void *)dev,
				     "K_TXQ_TASK");
	if (!wilc->txq_thread) {
		PRINT_ER("couldn't create TXQ thread\n");
		wilc->close = 0;
		return -ENOBUFS;
	}
	down(&wilc->txq_thread_started);

	return 0;
}

static void wlan_deinitialize_threads(struct net_device *dev)
{
	struct wilc_vif *vif;
	struct wilc *wl;
	vif = netdev_priv(dev);
	wl = vif->wilc;

	wl->close = 1;
	PRINT_D(INIT_DBG, "Deinitializing Threads\n");

	if (&wl->txq_event)
		up(&wl->txq_event);

	if (wl->txq_thread) {
		kthread_stop(wl->txq_thread);
		wl->txq_thread = NULL;
	}
}

int wilc1000_wlan_init(struct net_device *dev, struct wilc_vif *vif)
{
	int ret = 0;
	struct wilc *wl = vif->wilc;

	if (!wl->initialized) {
		wl->mac_status = WILC_MAC_STATUS_INIT;
		wl->close = 0;

		wlan_init_locks(dev);

		ret = wilc_wlan_init(dev);
		if (ret < 0) {
			PRINT_ER("Initializing WILC_Wlan FAILED\n");
			ret = -EIO;
			goto _fail_locks_;
		}

		if (wl->gpio >= 0 && init_irq(dev)) {
			PRINT_ER("couldn't initialize IRQ\n");
			ret = -EIO;
			goto _fail_locks_;
		}

		ret = wlan_initialize_threads(dev);
		if (ret < 0) {
			PRINT_ER("Initializing Threads FAILED\n");
			ret = -EIO;
			goto _fail_wilc_wlan_;
		}

		if (!wl->dev_irq_num &&
		    wl->hif_func->enable_interrupt &&
		    wl->hif_func->enable_interrupt(wl)) {
			PRINT_ER("couldn't initialize IRQ\n");
			ret = -EIO;
			goto _fail_irq_init_;
		}

		if (wilc_wlan_get_firmware(dev)) {
			PRINT_ER("Can't get firmware\n");
			ret = -EIO;
			goto _fail_irq_enable_;
		}

		ret = wilc1000_firmware_download(dev);
		if (ret < 0) {
			PRINT_ER("Failed to download firmware\n");
			ret = -EIO;
			goto _fail_irq_enable_;
		}

		ret = linux_wlan_start_firmware(dev);
		if (ret < 0) {
			PRINT_ER("Failed to start firmware\n");
			ret = -EIO;
			goto _fail_irq_enable_;
		}

		if (wilc_wlan_cfg_get(wl, 1, WID_FIRMWARE_VERSION, 1, 0)) {
			int size;
			char Firmware_ver[20];

			size = wilc_wlan_cfg_get_val(
					WID_FIRMWARE_VERSION,
					Firmware_ver, sizeof(Firmware_ver));
			Firmware_ver[size] = '\0';
			PRINT_D(INIT_DBG, "***** Firmware Ver = %s  *******\n", Firmware_ver);
		}
		ret = linux_wlan_init_test_config(dev, wl);

		if (ret < 0) {
			PRINT_ER("Failed to configure firmware\n");
			ret = -EIO;
			goto _fail_fw_start_;
		}

		wl->initialized = true;
		return 0;

_fail_fw_start_:
		wilc_wlan_stop(wl);

_fail_irq_enable_:
		if (!wl->dev_irq_num &&
		    wl->hif_func->disable_interrupt)
			wl->hif_func->disable_interrupt(wl);
_fail_irq_init_:
		if (wl->dev_irq_num)
			deinit_irq(dev);

		wlan_deinitialize_threads(dev);
_fail_wilc_wlan_:
		wilc_wlan_cleanup(dev);
_fail_locks_:
		wlan_deinit_locks(dev);
		PRINT_ER("WLAN Iinitialization FAILED\n");
	} else {
		PRINT_D(INIT_DBG, "wilc1000 already initialized\n");
	}
	return ret;
}

static int mac_init_fn(struct net_device *ndev)
{
	netif_start_queue(ndev);
	netif_stop_queue(ndev);

	return 0;
}

int wilc_mac_open(struct net_device *ndev)
{
	struct wilc_vif *vif;
	struct wilc *wilc;

	unsigned char mac_add[ETH_ALEN] = {0};
	int ret = 0;
	int i = 0;
	struct wilc_priv *priv;
	struct wilc *wl;

	vif = netdev_priv(ndev);
	wl = vif->wilc;

	if (!wl|| !wl->dev) {
		netdev_err(ndev, "wilc1000: SPI device not ready\n");
		return -ENODEV;
	}

	vif = netdev_priv(ndev);
	wilc = vif->wilc;
	priv = wiphy_priv(vif->ndev->ieee80211_ptr->wiphy);
	PRINT_D(INIT_DBG, "MAC OPEN[%p]\n", ndev);

	ret = wilc_init_host_int(ndev);
	if (ret < 0) {
		PRINT_ER("Failed to initialize host interface\n");

		return ret;
	}

	PRINT_D(INIT_DBG, "*** re-init ***\n");
	ret = wilc1000_wlan_init(ndev, vif);
	if (ret < 0) {
		PRINT_ER("Failed to initialize wilc1000\n");
		wilc_deinit_host_int(ndev);
		return ret;
	}

	wilc_set_machw_change_vir_if(ndev, false);

	wilc_get_mac_address(vif, mac_add);
	PRINT_D(INIT_DBG, "Mac address: %pM\n", mac_add);

	for (i = 0; i < wl->vif_num; i++) {
		if (ndev == wl->vif[i]->ndev) {
			memcpy(wl->vif[i]->src_addr, mac_add, ETH_ALEN);
			break;
		}
	}

	memcpy(ndev->dev_addr, wl->vif[i]->src_addr, ETH_ALEN);

	if (!is_valid_ether_addr(ndev->dev_addr)) {
		PRINT_ER("Error: Wrong MAC address\n");
		wilc_deinit_host_int(ndev);
		wilc1000_wlan_deinit(ndev);
		return -EINVAL;
	}

	wilc_mgmt_frame_register(vif->ndev->ieee80211_ptr->wiphy,
				 vif->ndev->ieee80211_ptr,
				 vif->g_struct_frame_reg[0].frame_type,
				 vif->g_struct_frame_reg[0].reg);
	wilc_mgmt_frame_register(vif->ndev->ieee80211_ptr->wiphy,
				 vif->ndev->ieee80211_ptr,
				 vif->g_struct_frame_reg[1].frame_type,
				 vif->g_struct_frame_reg[1].reg);
	netif_wake_queue(ndev);
	wl->open_ifcs++;
	vif->mac_opened = 1;
	return 0;
}

static struct net_device_stats *mac_stats(struct net_device *dev)
{
	struct wilc_vif *vif= netdev_priv(dev);

	return &vif->netstats;
}

static void wilc_set_multicast_list(struct net_device *dev)
{
	struct netdev_hw_addr *ha;
	struct wilc_priv *priv;
	struct host_if_drv *hif_drv;
	struct wilc_vif *vif;
	int i = 0;

	priv = wiphy_priv(dev->ieee80211_ptr->wiphy);
	vif = netdev_priv(dev);
	hif_drv = (struct host_if_drv *)priv->hWILCWFIDrv;

	if (!dev)
		return;

	PRINT_D(INIT_DBG, "Setting Multicast List with count = %d.\n",
		dev->mc.count);

	if (dev->flags & IFF_PROMISC) {
		PRINT_D(INIT_DBG, "Set promiscuous mode ON, retrive all packets\n");
		return;
	}

	if ((dev->flags & IFF_ALLMULTI) ||
	    (dev->mc.count) > WILC_MULTICAST_TABLE_SIZE) {
		PRINT_D(INIT_DBG, "Disable multicast filter, retrive all multicast packets\n");
		wilc_setup_multicast_filter(vif, false, 0);
		return;
	}

	if ((dev->mc.count) == 0) {
		PRINT_D(INIT_DBG, "Enable multicast filter, retrive directed packets only.\n");
		wilc_setup_multicast_filter(vif, true, 0);
		return;
	}

	netdev_for_each_mc_addr(ha, dev) {
		memcpy(wilc_multicast_mac_addr_list[i], ha->addr, ETH_ALEN);
		PRINT_D(INIT_DBG, "Entry[%d]: %x:%x:%x:%x:%x:%x\n", i,
			wilc_multicast_mac_addr_list[i][0],
			wilc_multicast_mac_addr_list[i][1],
			wilc_multicast_mac_addr_list[i][2],
			wilc_multicast_mac_addr_list[i][3],
			wilc_multicast_mac_addr_list[i][4],
			wilc_multicast_mac_addr_list[i][5]);
		i++;
	}

	wilc_setup_multicast_filter(vif, true, (dev->mc.count));

	return;
}

static void linux_wlan_tx_complete(void *priv, int status)
{
	struct tx_complete_data *pv_data = (struct tx_complete_data *)priv;

	if (status == 1)
		PRINT_D(TX_DBG, "Packet sent successfully - Size = %d - Address = %p - SKB = %p\n", pv_data->size, pv_data->buff, pv_data->skb);
	else
		PRINT_D(TX_DBG, "Couldn't send packet - Size = %d - Address = %p - SKB = %p\n", pv_data->size, pv_data->buff, pv_data->skb);
	dev_kfree_skb(pv_data->skb);
	kfree(pv_data);
}

int wilc_mac_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct wilc_vif *vif;
	struct tx_complete_data *tx_data = NULL;
	int queue_count;
	char *udp_buf;
	struct iphdr *ih;
	struct ethhdr *eth_h;
	struct wilc *wilc;

	vif = netdev_priv(ndev);
	wilc = vif->wilc;

	PRINT_D(TX_DBG, "Sending packet just received from TCP/IP\n");

	if (skb->dev != ndev) {
		PRINT_ER("Packet not destined to this device\n");
		return 0;
	}

	tx_data = kmalloc(sizeof(*tx_data), GFP_ATOMIC);
	if (!tx_data) {
		PRINT_ER("Failed to allocate memory for tx_data structure\n");
		dev_kfree_skb(skb);
		netif_wake_queue(ndev);
		return 0;
	}

	tx_data->buff = skb->data;
	tx_data->size = skb->len;
	tx_data->skb  = skb;

	eth_h = (struct ethhdr *)(skb->data);
	if (eth_h->h_proto == 0x8e88)
		PRINT_D(INIT_DBG, "EAPOL transmitted\n");

	ih = (struct iphdr *)(skb->data + sizeof(struct ethhdr));

	udp_buf = (char *)ih + sizeof(struct iphdr);
	if ((udp_buf[1] == 68 && udp_buf[3] == 67) ||
	    (udp_buf[1] == 67 && udp_buf[3] == 68))
		PRINT_D(GENERIC_DBG, "DHCP Message transmitted, type:%x %x %x\n",
			udp_buf[248], udp_buf[249], udp_buf[250]);

	PRINT_D(TX_DBG, "Sending packet - Size = %d - Address = %p - SKB = %p\n", tx_data->size, tx_data->buff, tx_data->skb);
	PRINT_D(TX_DBG, "Adding tx packet to TX Queue\n");
	vif->netstats.tx_packets++;
	vif->netstats.tx_bytes += tx_data->size;
	tx_data->pBssid = wilc->vif[vif->u8IfIdx]->bssid;
	queue_count = wilc_wlan_txq_add_net_pkt(ndev, (void *)tx_data,
						tx_data->buff, tx_data->size,
						linux_wlan_tx_complete);

	if (queue_count > FLOW_CONTROL_UPPER_THRESHOLD) {
		netif_stop_queue(wilc->vif[0]->ndev);
		netif_stop_queue(wilc->vif[1]->ndev);
	}

	return 0;
}

int wilc_mac_close(struct net_device *ndev)
{
	struct wilc_priv *priv;
	struct wilc_vif *vif;
	struct host_if_drv *hif_drv;
	struct wilc *wl;

	vif = netdev_priv(ndev);

	if (!vif || !vif->ndev || !vif->ndev->ieee80211_ptr ||
	    !vif->ndev->ieee80211_ptr->wiphy) {
		PRINT_ER("vif = NULL\n");
		return 0;
	}

	priv = wiphy_priv(vif->ndev->ieee80211_ptr->wiphy);
	wl = vif->wilc;

	if (!priv) {
		PRINT_ER("priv = NULL\n");
		return 0;
	}

	hif_drv = (struct host_if_drv *)priv->hWILCWFIDrv;

	PRINT_D(GENERIC_DBG, "Mac close\n");

	if (!wl) {
		PRINT_ER("wl = NULL\n");
		return 0;
	}

	if (!hif_drv) {
		PRINT_ER("hif_drv = NULL\n");
		return 0;
	}

	if ((wl->open_ifcs) > 0) {
		wl->open_ifcs--;
	} else {
		PRINT_ER("ERROR: MAC close called while number of opened interfaces is zero\n");
		return 0;
	}

	if (vif->ndev) {
		netif_stop_queue(vif->ndev);

		wilc_deinit_host_int(vif->ndev);
	}

	if (wl->open_ifcs == 0) {
		PRINT_D(GENERIC_DBG, "Deinitializing wilc1000\n");
		wl->close = 1;
		wilc1000_wlan_deinit(ndev);
		WILC_WFI_deinit_mon_interface();
	}

	up(&close_exit_sync);
	vif->mac_opened = 0;

	return 0;
}

static int mac_ioctl(struct net_device *ndev, struct ifreq *req, int cmd)
{
	u8 *buff = NULL;
	s8 rssi;
	u32 size = 0, length = 0;
	struct wilc_vif *vif;
	struct wilc_priv *priv;
	s32 ret = 0;
	struct wilc *wilc;

	vif = netdev_priv(ndev);
	wilc = vif->wilc;

	if (!wilc->initialized)
		return 0;

	switch (cmd) {
	case SIOCSIWPRIV:
	{
		struct iwreq *wrq = (struct iwreq *) req;

		size = wrq->u.data.length;

		if (size && wrq->u.data.pointer) {
			buff = memdup_user(wrq->u.data.pointer,
					   wrq->u.data.length);
			if (IS_ERR(buff))
				return PTR_ERR(buff);

			if (strncasecmp(buff, "RSSI", length) == 0) {
				priv = wiphy_priv(vif->ndev->ieee80211_ptr->wiphy);
				ret = wilc_get_rssi(vif, &rssi);
				if (ret)
					PRINT_ER("Failed to send get rssi param's message queue ");
				PRINT_INFO(GENERIC_DBG, "RSSI :%d\n", rssi);

				rssi += 5;

				snprintf(buff, size, "rssi %d", rssi);

				if (copy_to_user(wrq->u.data.pointer, buff, size)) {
					PRINT_ER("%s: failed to copy data to user buffer\n", __func__);
					ret = -EFAULT;
					goto done;
				}
			}
		}
	}
	break;

	default:
	{
		PRINT_INFO(GENERIC_DBG, "Command - %d - has been received\n", cmd);
		ret = -EOPNOTSUPP;
		goto done;
	}
	}

done:

	kfree(buff);

	return ret;
}

void wilc_frmw_to_linux(struct wilc *wilc, u8 *buff, u32 size, u32 pkt_offset)
{
	unsigned int frame_len = 0;
	int stats;
	unsigned char *buff_to_send = NULL;
	struct sk_buff *skb;
	struct net_device *wilc_netdev;
	struct wilc_vif *vif;

	wilc_netdev = get_if_handler(wilc, buff);
	if (!wilc_netdev)
		return;

	buff += pkt_offset;
	vif = netdev_priv(wilc_netdev);

	if (size > 0) {
		frame_len = size;
		buff_to_send = buff;

		skb = dev_alloc_skb(frame_len);
		if (!skb) {
			PRINT_ER("Low memory - packet droped\n");
			return;
		}

		if (!wilc || !wilc_netdev)
			PRINT_ER("wilc_netdev in wilc is NULL");
		skb->dev = wilc_netdev;

		if (!skb->dev)
			PRINT_ER("skb->dev is NULL\n");

		memcpy(skb_put(skb, frame_len), buff_to_send, frame_len);

		skb->protocol = eth_type_trans(skb, wilc_netdev);
		vif->netstats.rx_packets++;
		vif->netstats.rx_bytes += frame_len;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		stats = netif_rx(skb);
		PRINT_D(RX_DBG, "netif_rx ret value is: %d\n", stats);
	}
}

void WILC_WFI_mgmt_rx(struct wilc *wilc, u8 *buff, u32 size)
{
	int i = 0;
	struct wilc_vif *vif;

	for (i = 0; i < wilc->vif_num; i++) {
		vif = netdev_priv(wilc->vif[i]->ndev);
		if (vif->monitor_flag) {
			WILC_WFI_monitor_rx(buff, size);
			return;
		}
	}

	vif = netdev_priv(wilc->vif[1]->ndev);
	if ((buff[0] == vif->g_struct_frame_reg[0].frame_type && vif->g_struct_frame_reg[0].reg) ||
	    (buff[0] == vif->g_struct_frame_reg[1].frame_type && vif->g_struct_frame_reg[1].reg))
		WILC_WFI_p2p_rx(wilc->vif[1]->ndev, buff, size);
}

void wilc_netdev_cleanup(struct wilc *wilc)
{
	int i = 0;
	struct wilc_vif *vif[NUM_CONCURRENT_IFC];

	if (wilc && (wilc->vif[0]->ndev || wilc->vif[1]->ndev)) {
		unregister_inetaddr_notifier(&g_dev_notifier);

		for (i = 0; i < NUM_CONCURRENT_IFC; i++)
			vif[i] = netdev_priv(wilc->vif[i]->ndev);
	}

	if (wilc && wilc->firmware)
		release_firmware(wilc->firmware);

	if (wilc && (wilc->vif[0]->ndev || wilc->vif[1]->ndev)) {
		wilc_lock_timeout(wilc, &close_exit_sync, 12 * 1000);

		for (i = 0; i < NUM_CONCURRENT_IFC; i++)
			if (wilc->vif[i]->ndev)
				if (vif[i]->mac_opened)
					wilc_mac_close(wilc->vif[i]->ndev);

		for (i = 0; i < NUM_CONCURRENT_IFC; i++) {
			unregister_netdev(wilc->vif[i]->ndev);
			wilc_free_wiphy(wilc->vif[i]->ndev);
			free_netdev(wilc->vif[i]->ndev);
		}
	}

	kfree(wilc);
}
EXPORT_SYMBOL_GPL(wilc_netdev_cleanup);

int wilc_netdev_init(struct wilc **wilc, struct device *dev, int io_type,
		     int gpio, const struct wilc_hif_func *ops)
{
	int i;
	struct wilc_vif *vif;
	struct net_device *ndev;
	struct wilc *wl;

	sema_init(&close_exit_sync, 0);

	wl = kzalloc(sizeof(*wl), GFP_KERNEL);
	if (!wl)
		return -ENOMEM;

	*wilc = wl;
	wl->io_type = io_type;
	wl->gpio = gpio;
	wl->hif_func = ops;

	register_inetaddr_notifier(&g_dev_notifier);

	for (i = 0; i < NUM_CONCURRENT_IFC; i++) {
		ndev = alloc_etherdev(sizeof(struct wilc_vif));
		if (!ndev) {
			PRINT_ER("Failed to allocate ethernet dev\n");
			return -1;
		}

		vif = netdev_priv(ndev);
		memset(vif, 0, sizeof(struct wilc_vif));

		if (i == 0)
			strcpy(ndev->name, "wlan%d");
		else
			strcpy(ndev->name, "p2p%d");

		vif->u8IfIdx = wl->vif_num;
		vif->wilc = *wilc;
		wl->vif[i] = vif;
		wl->vif[wl->vif_num]->ndev = ndev;
		wl->vif_num++;
		ndev->netdev_ops = &wilc_netdev_ops;

		{
			struct wireless_dev *wdev;
			wdev = wilc_create_wiphy(ndev, dev);

			if (dev)
				SET_NETDEV_DEV(ndev, dev);

			if (!wdev) {
				PRINT_ER("Can't register WILC Wiphy\n");
				return -1;
			}

			vif->ndev->ieee80211_ptr = wdev;
			vif->ndev->ml_priv = vif;
			wdev->netdev = vif->ndev;
			vif->netstats.rx_packets = 0;
			vif->netstats.tx_packets = 0;
			vif->netstats.rx_bytes = 0;
			vif->netstats.tx_bytes = 0;
		}

		if (register_netdev(ndev)) {
			PRINT_ER("Device couldn't be registered - %s\n",
				 ndev->name);
			return -1;
		}

		vif->iftype = STATION_MODE;
		vif->mac_opened = 0;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(wilc_netdev_init);

MODULE_LICENSE("GPL");
