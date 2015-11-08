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

#include <linux/version.h>
#include <linux/semaphore.h>

#ifdef WILC_SDIO
#include "linux_wlan_sdio.h"
#else
#include "linux_wlan_spi.h"
#endif

#if defined(CUSTOMER_PLATFORM)
/*
 TODO : Write power control functions as customer platform.
 */
#else

 #define _linux_wlan_device_power_on()		{}
 #define _linux_wlan_device_power_off()		{}

 #define _linux_wlan_device_detection()		{}
 #define _linux_wlan_device_removal()		{}
#endif

extern bool g_obtainingIP;
extern void resolve_disconnect_aberration(void *drvHandler);
extern u8 gau8MulticastMacAddrList[WILC_MULTICAST_TABLE_SIZE][ETH_ALEN];
extern struct timer_list hDuringIpTimer;

static int linux_wlan_device_power(int on_off)
{
	PRINT_D(INIT_DBG, "linux_wlan_device_power.. (%d)\n", on_off);

	if (on_off) {
		_linux_wlan_device_power_on();
	} else {
		_linux_wlan_device_power_off();
	}

	return 0;
}

static int linux_wlan_device_detection(int on_off)
{
	PRINT_D(INIT_DBG, "linux_wlan_device_detection.. (%d)\n", on_off);

#ifdef WILC_SDIO
	if (on_off) {
		_linux_wlan_device_detection();
	} else {
		_linux_wlan_device_removal();
	}
#endif

	return 0;
}

static int dev_state_ev_handler(struct notifier_block *this, unsigned long event, void *ptr);

static struct notifier_block g_dev_notifier = {
	.notifier_call = dev_state_ev_handler
};

#define IRQ_WAIT	1
#define IRQ_NO_WAIT	0
/*
 *      to sync between mac_close and module exit.
 *      don't initialize or de-initialize from init/deinitlocks
 *      to be initialized from module wilc_netdev_init and
 *      deinitialized from mdoule_exit
 */
static struct semaphore close_exit_sync;

static int wlan_deinit_locks(struct net_device *dev);
static void wlan_deinitialize_threads(struct net_device *dev);
extern void WILC_WFI_monitor_rx(u8 *buff, u32 size);
extern void WILC_WFI_p2p_rx(struct net_device *dev, u8 *buff, u32 size);

static void linux_wlan_tx_complete(void *priv, int status);
static int  mac_init_fn(struct net_device *ndev);
int  mac_xmit(struct sk_buff *skb, struct net_device *dev);
int  mac_open(struct net_device *ndev);
int  mac_close(struct net_device *ndev);
static struct net_device_stats *mac_stats(struct net_device *dev);
static int  mac_ioctl(struct net_device *ndev, struct ifreq *req, int cmd);
static void wilc_set_multicast_list(struct net_device *dev);

/*
 * for now - in frmw_to_linux there should be private data to be passed to it
 * and this data should be pointer to net device
 */
struct wilc *g_linux_wlan;
bool bEnablePS = true;

static const struct net_device_ops wilc_netdev_ops = {
	.ndo_init = mac_init_fn,
	.ndo_open = mac_open,
	.ndo_stop = mac_close,
	.ndo_start_xmit = mac_xmit,
	.ndo_do_ioctl = mac_ioctl,
	.ndo_get_stats = mac_stats,
	.ndo_set_rx_mode  = wilc_set_multicast_list,

};

static int dev_state_ev_handler(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct in_ifaddr *dev_iface = (struct in_ifaddr *)ptr;
	struct wilc_priv *priv;
	struct host_if_drv *pstrWFIDrv;
	struct net_device *dev;
	u8 *pIP_Add_buff;
	perInterface_wlan_t *nic;
	u8 null_ip[4] = {0};
	char wlan_dev_name[5] = "wlan0";

	if (dev_iface == NULL || dev_iface->ifa_dev == NULL || dev_iface->ifa_dev->dev == NULL)	{
		PRINT_D(GENERIC_DBG, "dev_iface = NULL\n");
		return NOTIFY_DONE;
	}

	if ((memcmp(dev_iface->ifa_label, "wlan0", 5)) && (memcmp(dev_iface->ifa_label, "p2p0", 4))) {
		PRINT_D(GENERIC_DBG, "Interface is neither WLAN0 nor P2P0\n");
		return NOTIFY_DONE;
	}

	dev  = (struct net_device *)dev_iface->ifa_dev->dev;
	if (dev->ieee80211_ptr == NULL || dev->ieee80211_ptr->wiphy == NULL) {
		PRINT_D(GENERIC_DBG, "No Wireless registerd\n");
		return NOTIFY_DONE;
	}
	priv = wiphy_priv(dev->ieee80211_ptr->wiphy);
	if (priv == NULL) {
		PRINT_D(GENERIC_DBG, "No Wireless Priv\n");
		return NOTIFY_DONE;
	}
	pstrWFIDrv = (struct host_if_drv *)priv->hWILCWFIDrv;
	nic = netdev_priv(dev);
	if (nic == NULL || pstrWFIDrv == NULL) {
		PRINT_D(GENERIC_DBG, "No Wireless Priv\n");
		return NOTIFY_DONE;
	}

	PRINT_INFO(GENERIC_DBG, "dev_state_ev_handler +++\n"); /* tony */

	switch (event) {
	case NETDEV_UP:
		PRINT_D(GENERIC_DBG, "dev_state_ev_handler event=NETDEV_UP %p\n", dev);       /* tony */

		PRINT_INFO(GENERIC_DBG, "\n ============== IP Address Obtained ===============\n\n");

		/*If we are in station mode or client mode*/
		if (nic->iftype == STATION_MODE || nic->iftype == CLIENT_MODE) {
			pstrWFIDrv->IFC_UP = 1;
			g_obtainingIP = false;
			del_timer(&hDuringIpTimer);
			PRINT_D(GENERIC_DBG, "IP obtained , enable scan\n");
		}

		if (bEnablePS)
			host_int_set_power_mgmt(pstrWFIDrv, 1, 0);

		PRINT_D(GENERIC_DBG, "[%s] Up IP\n", dev_iface->ifa_label);

		pIP_Add_buff = (char *) (&(dev_iface->ifa_address));
		PRINT_D(GENERIC_DBG, "IP add=%d:%d:%d:%d\n", pIP_Add_buff[0], pIP_Add_buff[1], pIP_Add_buff[2], pIP_Add_buff[3]);
		host_int_setup_ipaddress(pstrWFIDrv, pIP_Add_buff, nic->u8IfIdx);

		break;

	case NETDEV_DOWN:
		PRINT_D(GENERIC_DBG, "dev_state_ev_handler event=NETDEV_DOWN %p\n", dev);               /* tony */

		PRINT_INFO(GENERIC_DBG, "\n ============== IP Address Released ===============\n\n");
		if (nic->iftype == STATION_MODE || nic->iftype == CLIENT_MODE) {
			pstrWFIDrv->IFC_UP = 0;
			g_obtainingIP = false;
		}

		if (memcmp(dev_iface->ifa_label, wlan_dev_name, 5) == 0)
			host_int_set_power_mgmt(pstrWFIDrv, 0, 0);

		resolve_disconnect_aberration(pstrWFIDrv);

		PRINT_D(GENERIC_DBG, "[%s] Down IP\n", dev_iface->ifa_label);

		pIP_Add_buff = null_ip;
		PRINT_D(GENERIC_DBG, "IP add=%d:%d:%d:%d\n", pIP_Add_buff[0], pIP_Add_buff[1], pIP_Add_buff[2], pIP_Add_buff[3]);

		host_int_setup_ipaddress(pstrWFIDrv, pIP_Add_buff, nic->u8IfIdx);

		break;

	default:
		PRINT_INFO(GENERIC_DBG, "dev_state_ev_handler event=default\n");        /* tony */
		PRINT_INFO(GENERIC_DBG, "[%s] unknown dev event: %lu\n", dev_iface->ifa_label, event);

		break;
	}

	return NOTIFY_DONE;

}

#if (defined WILC_SPI) || (defined WILC_SDIO_IRQ_GPIO)
static irqreturn_t isr_uh_routine(int irq, void *user_data)
{
	perInterface_wlan_t *nic;
	struct wilc *wilc;
	struct net_device *dev = (struct net_device *)user_data;

	nic = netdev_priv(dev);
	wilc = nic->wilc;
	PRINT_D(INT_DBG, "Interrupt received UH\n");

	/*While mac is closing cacncel the handling of any interrupts received*/
	if (wilc->close) {
		PRINT_ER("Driver is CLOSING: Can't handle UH interrupt\n");
		return IRQ_HANDLED;
	}
	return IRQ_WAKE_THREAD;
}
#endif

irqreturn_t isr_bh_routine(int irq, void *userdata)
{
	perInterface_wlan_t *nic;
	struct wilc *wilc;

	nic = netdev_priv(userdata);
	wilc = nic->wilc;

	/*While mac is closing cacncel the handling of any interrupts received*/
	if (wilc->close) {
		PRINT_ER("Driver is CLOSING: Can't handle BH interrupt\n");
		return IRQ_HANDLED;
	}

	PRINT_D(INT_DBG, "Interrupt received BH\n");
	wilc_handle_isr(wilc);

	return IRQ_HANDLED;
}

#if (defined WILC_SPI) || (defined WILC_SDIO_IRQ_GPIO)
static int init_irq(struct net_device *dev)
{
	int ret = 0;
	perInterface_wlan_t *nic;
	struct wilc *wl;

	nic = netdev_priv(dev);
	wl = nic->wilc;

	/*initialize GPIO and register IRQ num*/
	/*GPIO request*/
	if ((gpio_request(GPIO_NUM, "WILC_INTR") == 0) &&
	    (gpio_direction_input(GPIO_NUM) == 0)) {
#if defined(CUSTOMER_PLATFORM)
/*
 TODO : save the registerd irq number to the private wilc context in kernel.
 *
 * ex) nic->dev_irq_num = gpio_to_irq(GPIO_NUM);
 */
#else
		wl->dev_irq_num = gpio_to_irq(GPIO_NUM);
#endif
	} else {
		ret = -1;
		PRINT_ER("could not obtain gpio for WILC_INTR\n");
	}

	if ((ret != -1) && (request_threaded_irq(wl->dev_irq_num, isr_uh_routine, isr_bh_routine,
						  IRQF_TRIGGER_LOW | IRQF_ONESHOT,               /*Without IRQF_ONESHOT the uh will remain kicked in and dont gave a chance to bh*/
						  "WILC_IRQ", dev)) < 0) {

		PRINT_ER("Failed to request IRQ for GPIO: %d\n", GPIO_NUM);
		ret = -1;
	} else {

		PRINT_D(INIT_DBG, "IRQ request succeeded IRQ-NUM= %d on GPIO: %d\n",
			wl->dev_irq_num, GPIO_NUM);
	}

	return ret;
}
#endif

static void deinit_irq(struct net_device *dev)
{
	perInterface_wlan_t *nic;
	struct wilc *wilc;

	nic = netdev_priv(dev);
	wilc = nic->wilc;

#if (defined WILC_SPI) || (defined WILC_SDIO_IRQ_GPIO)
	/* Deintialize IRQ */
	if (&wilc->dev_irq_num != 0) {
		free_irq(wilc->dev_irq_num, wilc);

		gpio_free(GPIO_NUM);
	}
#endif
}

/*
 *      OS functions
 */
void linux_wlan_dbg(u8 *buff)
{
	PRINT_D(INIT_DBG, "%d\n", *buff);
}

int linux_wlan_lock_timeout(void *vp, u32 timeout)
{
	int error = -1;

	PRINT_D(LOCK_DBG, "Locking %p\n", vp);
	if (vp != NULL)
		error = down_timeout((struct semaphore *)vp, msecs_to_jiffies(timeout));
	else
		PRINT_ER("Failed, mutex is NULL\n");
	return error;
}

void linux_wlan_mac_indicate(struct wilc *wilc, int flag)
{
	/*I have to do it that way becuase there is no mean to encapsulate device pointer
	 * as a parameter
	 */
	int status;

	if (flag == WILC_MAC_INDICATE_STATUS) {
		wilc_wlan_cfg_get_val(WID_STATUS, (unsigned char *)&status, 4);
		if (wilc->mac_status == WILC_MAC_STATUS_INIT) {
			wilc->mac_status = status;
			up(&wilc->sync_event);
		} else {
			wilc->mac_status = status;
		}

		if (wilc->mac_status == WILC_MAC_STATUS_CONNECT) {        /* Connect */
		}

	} else if (flag == WILC_MAC_INDICATE_SCAN) {
		PRINT_D(GENERIC_DBG, "Scanning ...\n");

	}

}

struct net_device *GetIfHandler(struct wilc *wilc, u8 *pMacHeader)
{
	u8 *Bssid, *Bssid1;
	int i = 0;

	Bssid  = pMacHeader + 10;
	Bssid1 = pMacHeader + 4;

	for (i = 0; i < wilc->vif_num; i++)
		if (!memcmp(Bssid1, wilc->vif[i].bssid, ETH_ALEN) ||
		    !memcmp(Bssid, wilc->vif[i].bssid, ETH_ALEN))
			return wilc->vif[i].ndev;

	PRINT_INFO(INIT_DBG, "Invalide handle\n");
	for (i = 0; i < 25; i++)
		PRINT_D(INIT_DBG, "%02x ", pMacHeader[i]);
	Bssid  = pMacHeader + 18;
	Bssid1 = pMacHeader + 12;
	for (i = 0; i < wilc->vif_num; i++)
		if (!memcmp(Bssid1, wilc->vif[i].bssid, ETH_ALEN) ||
		    !memcmp(Bssid, wilc->vif[i].bssid, ETH_ALEN))
			return wilc->vif[i].ndev;

	PRINT_INFO(INIT_DBG, "\n");
	return NULL;
}

int linux_wlan_set_bssid(struct net_device *wilc_netdev, u8 *pBSSID)
{
	int i = 0;
	int ret = -1;
	perInterface_wlan_t *nic;
	struct wilc *wilc;

	nic = netdev_priv(wilc_netdev);
	wilc = nic->wilc;

	for (i = 0; i < wilc->vif_num; i++)
		if (wilc->vif[i].ndev == wilc_netdev) {
			memcpy(wilc->vif[i].bssid, pBSSID, 6);
			ret = 0;
			break;
		}

	return ret;
}

/*Function to get number of connected interfaces*/
int linux_wlan_get_num_conn_ifcs(void)
{
	u8 i = 0;
	u8 null_bssid[6] = {0};
	u8 ret_val = 0;

	for (i = 0; i < g_linux_wlan->vif_num; i++)
		if (memcmp(g_linux_wlan->vif[i].bssid, null_bssid, 6))
			ret_val++;

	return ret_val;
}

#define USE_TX_BACKOFF_DELAY_IF_NO_BUFFERS

static int linux_wlan_txq_task(void *vp)
{
	int ret, txq_count;
	perInterface_wlan_t *nic;
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

	nic = netdev_priv(dev);
	wl = nic->wilc;

	/* inform wilc1000_wlan_init that TXQ task is started. */
	up(&wl->txq_thread_started);
	while (1) {

		PRINT_D(TX_DBG, "txq_task Taking a nap :)\n");
		down(&wl->txq_event);
		/* wait_for_completion(&pd->txq_event); */
		PRINT_D(TX_DBG, "txq_task Who waked me up :$\n");

		if (wl->close) {
			/*Unlock the mutex in the mac_close function to indicate the exiting of the TX thread */
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
			if (txq_count < FLOW_CONTROL_LOWER_THRESHOLD /* && netif_queue_stopped(pd->wilc_netdev)*/) {
				PRINT_D(TX_DBG, "Waking up queue\n");
				/* netif_wake_queue(pd->wilc_netdev); */
				if (netif_queue_stopped(wl->vif[0].ndev))
					netif_wake_queue(wl->vif[0].ndev);
				if (netif_queue_stopped(wl->vif[1].ndev))
					netif_wake_queue(wl->vif[1].ndev);
			}

			if (ret == WILC_TX_ERR_NO_BUF) { /* failed to allocate buffers in chip. */
				do {
					/* Back off from sending packets for some time. */
					/* schedule_timeout will allow RX task to run and free buffers.*/
					/* set_current_state(TASK_UNINTERRUPTIBLE); */
					/* timeout = schedule_timeout(timeout); */
					msleep(TX_BACKOFF_WEIGHT_UNIT_MS << backoff_weight);
				} while (/*timeout*/ 0);
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
			/*TODO: drop packets after a certain time/number of retry count. */
		} while (ret == WILC_TX_ERR_NO_BUF && !wl->close); /* retry sending packets if no more buffers in chip. */
#endif
	}
	return 0;
}

void linux_wlan_rx_complete(void)
{
	PRINT_D(RX_DBG, "RX completed\n");
}

int linux_wlan_get_firmware(perInterface_wlan_t *p_nic)
{

	perInterface_wlan_t *nic = p_nic;
	int ret = 0;
	const struct firmware *wilc_firmware;
	char *firmware;

	if (nic->iftype == AP_MODE)
		firmware = AP_FIRMWARE;
	else if (nic->iftype == STATION_MODE)
		firmware = STA_FIRMWARE;

	else {
		PRINT_D(INIT_DBG, "Get P2P_CONCURRENCY_FIRMWARE\n");
		firmware = P2P_CONCURRENCY_FIRMWARE;
	}

	if (nic == NULL) {
		PRINT_ER("NIC is NULL\n");
		goto _fail_;
	}

	if (&nic->wilc_netdev->dev == NULL) {
		PRINT_ER("&nic->wilc_netdev->dev  is NULL\n");
		goto _fail_;
	}

	/*	the firmare should be located in /lib/firmware in
	 *      root file system with the name specified above */

#ifdef WILC_SDIO
	if (request_firmware(&wilc_firmware, firmware, &g_linux_wlan->wilc_sdio_func->dev) != 0) {
		PRINT_ER("%s - firmare not available\n", firmware);
		ret = -1;
		goto _fail_;
	}
#else
	if (request_firmware(&wilc_firmware, firmware, &g_linux_wlan->wilc_spidev->dev) != 0) {
		PRINT_ER("%s - firmare not available\n", firmware);
		ret = -1;
		goto _fail_;
	}
#endif
	g_linux_wlan->firmware = wilc_firmware;

_fail_:

	return ret;

}

static int linux_wlan_start_firmware(perInterface_wlan_t *nic)
{

	int ret = 0;
	/* start firmware */
	PRINT_D(INIT_DBG, "Starting Firmware ...\n");
	ret = wilc_wlan_start();
	if (ret < 0) {
		PRINT_ER("Failed to start Firmware\n");
		goto _fail_;
	}

	/* wait for mac ready */
	PRINT_D(INIT_DBG, "Waiting for Firmware to get ready ...\n");
	ret = linux_wlan_lock_timeout(&g_linux_wlan->sync_event, 5000);
	if (ret) {
		PRINT_D(INIT_DBG, "Firmware start timed out");
		goto _fail_;
	}
	/*
	 *      TODO: Driver shouoldn't wait forever for firmware to get started -
	 *      in case of timeout this should be handled properly
	 */
	PRINT_D(INIT_DBG, "Firmware successfully started\n");

_fail_:
	return ret;
}
static int linux_wlan_firmware_download(struct wilc *p_nic)
{

	int ret = 0;

	if (!g_linux_wlan->firmware) {
		PRINT_ER("Firmware buffer is NULL\n");
		ret = -ENOBUFS;
		goto _FAIL_;
	}
	/**
	 *      do the firmware download
	 **/
	PRINT_D(INIT_DBG, "Downloading Firmware ...\n");
	ret = wilc_wlan_firmware_download(g_linux_wlan->firmware->data,
					  g_linux_wlan->firmware->size);
	if (ret < 0)
		goto _FAIL_;

	/* Freeing FW buffer */
	PRINT_D(INIT_DBG, "Freeing FW buffer ...\n");
	PRINT_D(INIT_DBG, "Releasing firmware\n");
	release_firmware(g_linux_wlan->firmware);

	PRINT_D(INIT_DBG, "Download Succeeded\n");

_FAIL_:
	return ret;
}

/* startup configuration - could be changed later using iconfig*/
static int linux_wlan_init_test_config(struct net_device *dev, struct wilc *p_nic)
{

	unsigned char c_val[64];
	unsigned char mac_add[] = {0x00, 0x80, 0xC2, 0x5E, 0xa2, 0xff};

	struct wilc_priv *priv;
	struct host_if_drv *pstrWFIDrv;

	PRINT_D(TX_DBG, "Start configuring Firmware\n");
	get_random_bytes(&mac_add[5], 1);
	get_random_bytes(&mac_add[4], 1);
	priv = wiphy_priv(dev->ieee80211_ptr->wiphy);
	pstrWFIDrv = (struct host_if_drv *)priv->hWILCWFIDrv;
	PRINT_D(INIT_DBG, "Host = %p\n", pstrWFIDrv);

	PRINT_D(INIT_DBG, "MAC address is : %02x-%02x-%02x-%02x-%02x-%02x\n", mac_add[0], mac_add[1], mac_add[2], mac_add[3], mac_add[4], mac_add[5]);
	wilc_get_chipid(0);

	*(int *)c_val = 1;

	if (!wilc_wlan_cfg_set(1, WID_SET_DRV_HANDLER, c_val, 4, 0, 0))
		goto _fail_;

	/*to tell fw that we are going to use PC test - WILC specific*/
	c_val[0] = 0;
	if (!wilc_wlan_cfg_set(0, WID_PC_TEST_MODE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = INFRASTRUCTURE;
	if (!wilc_wlan_cfg_set(0, WID_BSS_TYPE, c_val, 1, 0, 0))
		goto _fail_;

	/* c_val[0] = RATE_AUTO; */
	c_val[0] = RATE_AUTO;
	if (!wilc_wlan_cfg_set(0, WID_CURRENT_TX_RATE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = G_MIXED_11B_2_MODE;
	if (!wilc_wlan_cfg_set(0, WID_11G_OPERATING_MODE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = 1;
	if (!wilc_wlan_cfg_set(0, WID_CURRENT_CHANNEL, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = G_SHORT_PREAMBLE;
	if (!wilc_wlan_cfg_set(0, WID_PREAMBLE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = AUTO_PROT;
	if (!wilc_wlan_cfg_set(0, WID_11N_PROT_MECH, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = ACTIVE_SCAN;
	if (!wilc_wlan_cfg_set(0, WID_SCAN_TYPE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = SITE_SURVEY_OFF;
	if (!wilc_wlan_cfg_set(0, WID_SITE_SURVEY, c_val, 1, 0, 0))
		goto _fail_;

	*((int *)c_val) = 0xffff; /* Never use RTS-CTS */
	if (!wilc_wlan_cfg_set(0, WID_RTS_THRESHOLD, c_val, 2, 0, 0))
		goto _fail_;

	*((int *)c_val) = 2346;
	if (!wilc_wlan_cfg_set(0, WID_FRAG_THRESHOLD, c_val, 2, 0, 0))
		goto _fail_;

	/*  SSID                                                                 */
	/*  --------------------------------------------------------------       */
	/*  Configuration :   String with length less than 32 bytes              */
	/*  Values to set :   Any string with length less than 32 bytes          */
	/*                    ( In BSS Station Set SSID to "" (null string)      */
	/*                      to enable Broadcast SSID suppport )              */
	/*  --------------------------------------------------------------       */
	c_val[0] = 0;
	if (!wilc_wlan_cfg_set(0, WID_BCAST_SSID, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = 1;
	if (!wilc_wlan_cfg_set(0, WID_QOS_ENABLE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = NO_POWERSAVE;
	if (!wilc_wlan_cfg_set(0, WID_POWER_MANAGEMENT, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = NO_ENCRYPT; /* NO_ENCRYPT, 0x79 */
	if (!wilc_wlan_cfg_set(0, WID_11I_MODE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = OPEN_SYSTEM;
	if (!wilc_wlan_cfg_set(0, WID_AUTH_TYPE, c_val, 1, 0, 0))
		goto _fail_;

	/*  WEP/802 11I Configuration                                            */
	/*  ------------------------------------------------------------------   */
	/*  Configuration : WEP Key                                              */
	/*  Values (0x)   : 5 byte for WEP40 and 13 bytes for WEP104             */
	/*                  In case more than 5 bytes are passed on for WEP 40   */
	/*                  only first 5 bytes will be used as the key           */
	/*  ------------------------------------------------------------------   */

	strcpy(c_val, "123456790abcdef1234567890");
	if (!wilc_wlan_cfg_set(0, WID_WEP_KEY_VALUE, c_val, (strlen(c_val) + 1), 0, 0))
		goto _fail_;

	/*  WEP/802 11I Configuration                                            */
	/*  ------------------------------------------------------------------   */
	/*  Configuration : AES/TKIP WPA/RSNA Pre-Shared Key                     */
	/*  Values to set : Any string with length greater than equal to 8 bytes */
	/*                  and less than 64 bytes                               */
	/*  ------------------------------------------------------------------   */
	strcpy(c_val, "12345678");
	if (!wilc_wlan_cfg_set(0, WID_11I_PSK, c_val, (strlen(c_val)), 0, 0))
		goto _fail_;

	/*  IEEE802.1X Key Configuration                                         */
	/*  ------------------------------------------------------------------   */
	/*  Configuration : Radius Server Access Secret Key                      */
	/*  Values to set : Any string with length greater than equal to 8 bytes */
	/*                  and less than 65 bytes                               */
	/*  ------------------------------------------------------------------   */
	strcpy(c_val, "password");
	if (!wilc_wlan_cfg_set(0, WID_1X_KEY, c_val, (strlen(c_val) + 1), 0, 0))
		goto _fail_;

	/*   IEEE802.1X Server Address Configuration                             */
	/*  ------------------------------------------------------------------   */
	/*  Configuration : Radius Server IP Address                             */
	/*  Values to set : Any valid IP Address                                 */
	/*  ------------------------------------------------------------------   */
	c_val[0] = 192;
	c_val[1] = 168;
	c_val[2] = 1;
	c_val[3] = 112;
	if (!wilc_wlan_cfg_set(0, WID_1X_SERV_ADDR, c_val, 4, 0, 0))
		goto _fail_;

	c_val[0] = 3;
	if (!wilc_wlan_cfg_set(0, WID_LISTEN_INTERVAL, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = 3;
	if (!wilc_wlan_cfg_set(0, WID_DTIM_PERIOD, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = NORMAL_ACK;
	if (!wilc_wlan_cfg_set(0, WID_ACK_POLICY, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = 0;
	if (!wilc_wlan_cfg_set(0, WID_USER_CONTROL_ON_TX_POWER, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = 48;
	if (!wilc_wlan_cfg_set(0, WID_TX_POWER_LEVEL_11A, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = 28;
	if (!wilc_wlan_cfg_set(0, WID_TX_POWER_LEVEL_11B, c_val, 1, 0, 0))
		goto _fail_;

	/*  Beacon Interval                                                      */
	/*  -------------------------------------------------------------------- */
	/*  Configuration : Sets the beacon interval value                       */
	/*  Values to set : Any 16-bit value                                     */
	/*  -------------------------------------------------------------------- */

	*((int *)c_val) = 100;
	if (!wilc_wlan_cfg_set(0, WID_BEACON_INTERVAL, c_val, 2, 0, 0))
		goto _fail_;

	c_val[0] = REKEY_DISABLE;
	if (!wilc_wlan_cfg_set(0, WID_REKEY_POLICY, c_val, 1, 0, 0))
		goto _fail_;

	/*  Rekey Time (s) (Used only when the Rekey policy is 2 or 4)           */
	/*  -------------------------------------------------------------------- */
	/*  Configuration : Sets the Rekey Time (s)                              */
	/*  Values to set : 32-bit value                                         */
	/*  -------------------------------------------------------------------- */
	*((int *)c_val) = 84600;
	if (!wilc_wlan_cfg_set(0, WID_REKEY_PERIOD, c_val, 4, 0, 0))
		goto _fail_;

	/*  Rekey Packet Count (in 1000s; used when Rekey Policy is 3)           */
	/*  -------------------------------------------------------------------- */
	/*  Configuration : Sets Rekey Group Packet count                        */
	/*  Values to set : 32-bit Value                                         */
	/*  -------------------------------------------------------------------- */
	*((int *)c_val) = 500;
	if (!wilc_wlan_cfg_set(0, WID_REKEY_PACKET_COUNT, c_val, 4, 0, 0))
		goto _fail_;

	c_val[0] = 1;
	if (!wilc_wlan_cfg_set(0, WID_SHORT_SLOT_ALLOWED, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = G_SELF_CTS_PROT;
	if (!wilc_wlan_cfg_set(0, WID_11N_ERP_PROT_TYPE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = 1;  /* Enable N */
	if (!wilc_wlan_cfg_set(0, WID_11N_ENABLE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = HT_MIXED_MODE;
	if (!wilc_wlan_cfg_set(0, WID_11N_OPERATING_MODE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = 1;   /* TXOP Prot disable in N mode: No RTS-CTS on TX A-MPDUs to save air-time. */
	if (!wilc_wlan_cfg_set(0, WID_11N_TXOP_PROT_DISABLE, c_val, 1, 0, 0))
		goto _fail_;

	memcpy(c_val, mac_add, 6);

	if (!wilc_wlan_cfg_set(0, WID_MAC_ADDR, c_val, 6, 0, 0))
		goto _fail_;

	/**
	 *      AP only
	 **/
	c_val[0] = DETECT_PROTECT_REPORT;
	if (!wilc_wlan_cfg_set(0, WID_11N_OBSS_NONHT_DETECTION, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = RTS_CTS_NONHT_PROT;
	if (!wilc_wlan_cfg_set(0, WID_11N_HT_PROT_TYPE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = 0;
	if (!wilc_wlan_cfg_set(0, WID_11N_RIFS_PROT_ENABLE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = MIMO_MODE;
	if (!wilc_wlan_cfg_set(0, WID_11N_SMPS_MODE, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = 7;
	if (!wilc_wlan_cfg_set(0, WID_11N_CURRENT_TX_MCS, c_val, 1, 0, 0))
		goto _fail_;

	c_val[0] = 1; /* Enable N with immediate block ack. */
	if (!wilc_wlan_cfg_set(0, WID_11N_IMMEDIATE_BA_ENABLED, c_val, 1, 1, 1))
		goto _fail_;

	return 0;

_fail_:
	return -1;
}

/**************************/
void wilc1000_wlan_deinit(struct net_device *dev)
{
	perInterface_wlan_t *nic;
	struct wilc *wl;

	nic = netdev_priv(dev);
	wl = nic->wilc;

	if (!wl) {
		netdev_err(dev, "wl is NULL\n");
		return;
	}

	if (wl->initialized)	{
		netdev_info(dev, "Deinitializing wilc1000...\n");

#if defined(PLAT_ALLWINNER_A20) || defined(PLAT_ALLWINNER_A23) || defined(PLAT_ALLWINNER_A31)
		/* johnny : remove */
		PRINT_D(INIT_DBG, "skip wilc_bus_set_default_speed\n");
#else
		wilc_bus_set_default_speed();
#endif

		PRINT_D(INIT_DBG, "Disabling IRQ\n");
#ifdef WILC_SDIO
		mutex_lock(&wl->hif_cs);
		disable_sdio_interrupt();
		mutex_unlock(&wl->hif_cs);
#endif
		if (&wl->txq_event != NULL)
			up(&wl->txq_event);

		PRINT_D(INIT_DBG, "Deinitializing Threads\n");
		wlan_deinitialize_threads(dev);

		PRINT_D(INIT_DBG, "Deinitializing IRQ\n");
		deinit_irq(dev);

		wilc_wlan_stop();

		PRINT_D(INIT_DBG, "Deinitializing WILC Wlan\n");
		wilc_wlan_cleanup(dev);
#if (defined WILC_SDIO) && (!defined WILC_SDIO_IRQ_GPIO)
  #if defined(PLAT_ALLWINNER_A20) || defined(PLAT_ALLWINNER_A23) || defined(PLAT_ALLWINNER_A31)
		PRINT_D(INIT_DBG, "Disabling IRQ 2\n");

		mutex_lock(&wl->hif_cs);
		disable_sdio_interrupt();
		mutex_unlock(&wl->hif_cs);
  #endif
#endif

		/*De-Initialize locks*/
		PRINT_D(INIT_DBG, "Deinitializing Locks\n");
		wlan_deinit_locks(dev);

		/* announce that wilc1000 is not initialized */
		wl->initialized = false;

		PRINT_D(INIT_DBG, "wilc1000 deinitialization Done\n");

	} else {
		PRINT_D(INIT_DBG, "wilc1000 is not initialized\n");
	}
}

int wlan_init_locks(struct net_device *dev)
{
	perInterface_wlan_t *nic;
	struct wilc *wl;

	nic = netdev_priv(dev);
	wl = nic->wilc;

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
	perInterface_wlan_t *nic;
	struct wilc *wilc;

	nic = netdev_priv(dev);
	wilc = nic->wilc;

	PRINT_D(INIT_DBG, "De-Initializing Locks\n");

	if (&wilc->hif_cs != NULL)
		mutex_destroy(&wilc->hif_cs);

	if (&wilc->rxq_cs != NULL)
		mutex_destroy(&wilc->rxq_cs);

	return 0;
}
void linux_to_wlan(wilc_wlan_inp_t *nwi, struct wilc *nic)
{

	PRINT_D(INIT_DBG, "Linux to Wlan services ...\n");

	nwi->os_context.os_private = (void *)nic;

#ifdef WILC_SDIO
	nwi->io_func.io_type = HIF_SDIO;
	nwi->io_func.io_init = linux_sdio_init;
	nwi->io_func.io_deinit = linux_sdio_deinit;
	nwi->io_func.u.sdio.sdio_cmd52 = linux_sdio_cmd52;
	nwi->io_func.u.sdio.sdio_cmd53 = linux_sdio_cmd53;
	nwi->io_func.u.sdio.sdio_set_max_speed = linux_sdio_set_max_speed;
	nwi->io_func.u.sdio.sdio_set_default_speed = linux_sdio_set_default_speed;
#else
	nwi->io_func.io_type = HIF_SPI;
	nwi->io_func.io_init = linux_spi_init;
	nwi->io_func.io_deinit = linux_spi_deinit;
	nwi->io_func.u.spi.spi_tx = linux_spi_write;
	nwi->io_func.u.spi.spi_rx = linux_spi_read;
	nwi->io_func.u.spi.spi_trx = linux_spi_write_read;
	nwi->io_func.u.spi.spi_max_speed = linux_spi_set_max_speed;
#endif
}

int wlan_initialize_threads(struct net_device *dev)
{
	perInterface_wlan_t *nic;
	struct wilc *wilc;
	int ret = 0;

	nic = netdev_priv(dev);
	wilc = nic->wilc;

	PRINT_D(INIT_DBG, "Initializing Threads ...\n");

	/* create tx task */
	PRINT_D(INIT_DBG, "Creating kthread for transmission\n");
	wilc->txq_thread = kthread_run(linux_wlan_txq_task, (void *)dev,
				     "K_TXQ_TASK");
	if (!wilc->txq_thread) {
		PRINT_ER("couldn't create TXQ thread\n");
		ret = -ENOBUFS;
		goto _fail_2;
	}
	/* wait for TXQ task to start. */
	down(&wilc->txq_thread_started);

	return 0;

_fail_2:
	/*De-Initialize 2nd thread*/
	wilc->close = 0;
	return ret;
}

static void wlan_deinitialize_threads(struct net_device *dev)
{
	perInterface_wlan_t *nic;
	struct wilc *wl;

	nic = netdev_priv(dev);
	wl = nic->wilc;

	wl->close = 1;
	PRINT_D(INIT_DBG, "Deinitializing Threads\n");

	if (&wl->txq_event != NULL)
		up(&wl->txq_event);

	if (wl->txq_thread != NULL) {
		kthread_stop(wl->txq_thread);
		wl->txq_thread = NULL;
	}
}

int wilc1000_wlan_init(struct net_device *dev, perInterface_wlan_t *p_nic)
{
	wilc_wlan_inp_t nwi;
	perInterface_wlan_t *nic = p_nic;
	int ret = 0;
	struct wilc *wl = nic->wilc;

	if (!wl->initialized) {
		wl->mac_status = WILC_MAC_STATUS_INIT;
		wl->close = 0;

		wlan_init_locks(dev);

		linux_to_wlan(&nwi, wl);

		ret = wilc_wlan_init(&nwi);
		if (ret < 0) {
			PRINT_ER("Initializing WILC_Wlan FAILED\n");
			ret = -EIO;
			goto _fail_locks_;
		}

#if (!defined WILC_SDIO) || (defined WILC_SDIO_IRQ_GPIO)
		if (init_irq(dev)) {
			PRINT_ER("couldn't initialize IRQ\n");
			ret = -EIO;
			goto _fail_locks_;
		}
#endif

		ret = wlan_initialize_threads(dev);
		if (ret < 0) {
			PRINT_ER("Initializing Threads FAILED\n");
			ret = -EIO;
			goto _fail_wilc_wlan_;
		}

#if (defined WILC_SDIO) && (!defined WILC_SDIO_IRQ_GPIO)
		if (enable_sdio_interrupt()) {
			PRINT_ER("couldn't initialize IRQ\n");
			ret = -EIO;
			goto _fail_irq_init_;
		}
#endif

		if (linux_wlan_get_firmware(nic)) {
			PRINT_ER("Can't get firmware\n");
			ret = -EIO;
			goto _fail_irq_enable_;
		}

		/*Download firmware*/
		ret = linux_wlan_firmware_download(wl);
		if (ret < 0) {
			PRINT_ER("Failed to download firmware\n");
			ret = -EIO;
			goto _fail_irq_enable_;
		}

		/* Start firmware*/
		ret = linux_wlan_start_firmware(nic);
		if (ret < 0) {
			PRINT_ER("Failed to start firmware\n");
			ret = -EIO;
			goto _fail_irq_enable_;
		}

		wilc_bus_set_max_speed();

		if (wilc_wlan_cfg_get(1, WID_FIRMWARE_VERSION, 1, 0)) {
			int size;
			char Firmware_ver[20];

			size = wilc_wlan_cfg_get_val(
					WID_FIRMWARE_VERSION,
					Firmware_ver, sizeof(Firmware_ver));
			Firmware_ver[size] = '\0';
			PRINT_D(INIT_DBG, "***** Firmware Ver = %s  *******\n", Firmware_ver);
		}
		/* Initialize firmware with default configuration */
		ret = linux_wlan_init_test_config(dev, wl);

		if (ret < 0) {
			PRINT_ER("Failed to configure firmware\n");
			ret = -EIO;
			goto _fail_fw_start_;
		}

		wl->initialized = true;
		return 0; /*success*/

_fail_fw_start_:
		wilc_wlan_stop();

_fail_irq_enable_:
#if (defined WILC_SDIO) && (!defined WILC_SDIO_IRQ_GPIO)
		disable_sdio_interrupt();
_fail_irq_init_:
#endif
#if (!defined WILC_SDIO) || (defined WILC_SDIO_IRQ_GPIO)
		deinit_irq(dev);

#endif
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

/*
 *      - this function will be called automatically by OS when module inserted.
 */

int mac_init_fn(struct net_device *ndev)
{

	/*Why we do this !!!*/
	netif_start_queue(ndev); /* ma */
	netif_stop_queue(ndev); /* ma */

	return 0;
}

/* This fn is called, when this device is setup using ifconfig */
int mac_open(struct net_device *ndev)
{
	perInterface_wlan_t *nic;

	/*No need for setting mac address here anymore,*/
	/*Just set it in init_test_config()*/
	unsigned char mac_add[ETH_ALEN] = {0};
	int ret = 0;
	int i = 0;
	struct wilc_priv *priv;
	struct wilc *wl;

	nic = netdev_priv(ndev);
	wl = nic->wilc;

#ifdef WILC_SPI
	if (!wl|| !wl->wilc_spidev) {
		netdev_err(ndev, "wilc1000: SPI device not ready\n");
		return -ENODEV;
	}
#endif
	nic = netdev_priv(ndev);
	priv = wiphy_priv(nic->wilc_netdev->ieee80211_ptr->wiphy);
	PRINT_D(INIT_DBG, "MAC OPEN[%p]\n", ndev);

	ret = wilc_init_host_int(ndev);
	if (ret < 0) {
		PRINT_ER("Failed to initialize host interface\n");

		return ret;
	}

	/*initialize platform*/
	PRINT_D(INIT_DBG, "*** re-init ***\n");
	ret = wilc1000_wlan_init(ndev, nic);
	if (ret < 0) {
		PRINT_ER("Failed to initialize wilc1000\n");
		wilc_deinit_host_int(ndev);
		return ret;
	}

	Set_machw_change_vir_if(ndev, false);

	host_int_get_MacAddress(priv->hWILCWFIDrv, mac_add);
	PRINT_D(INIT_DBG, "Mac address: %pM\n", mac_add);

	/* loop through the NUM of supported devices and set the MAC address */
	for (i = 0; i < wl->vif_num; i++) {
		if (ndev == wl->vif[i].ndev) {
			memcpy(wl->vif[i].src_addr, mac_add, ETH_ALEN);
			wl->vif[i].hif_drv = priv->hWILCWFIDrv;
			break;
		}
	}

	/* TODO: get MAC address whenever the source is EPROM - hardcoded and copy it to ndev*/
	memcpy(ndev->dev_addr, wl->vif[i].src_addr, ETH_ALEN);

	if (!is_valid_ether_addr(ndev->dev_addr)) {
		PRINT_ER("Error: Wrong MAC address\n");
		ret = -EINVAL;
		goto _err_;
	}

	wilc_mgmt_frame_register(nic->wilc_netdev->ieee80211_ptr->wiphy, nic->wilc_netdev->ieee80211_ptr,
				 nic->g_struct_frame_reg[0].frame_type, nic->g_struct_frame_reg[0].reg);
	wilc_mgmt_frame_register(nic->wilc_netdev->ieee80211_ptr->wiphy, nic->wilc_netdev->ieee80211_ptr,
				 nic->g_struct_frame_reg[1].frame_type, nic->g_struct_frame_reg[1].reg);
	netif_wake_queue(ndev);
	wl->open_ifcs++;
	nic->mac_opened = 1;
	return 0;

_err_:
	wilc_deinit_host_int(ndev);
	wilc1000_wlan_deinit(ndev);
	return ret;
}

struct net_device_stats *mac_stats(struct net_device *dev)
{
	perInterface_wlan_t *nic = netdev_priv(dev);

	return &nic->netstats;
}

/* Setup the multicast filter */
static void wilc_set_multicast_list(struct net_device *dev)
{

	struct netdev_hw_addr *ha;
	struct wilc_priv *priv;
	struct host_if_drv *pstrWFIDrv;
	int i = 0;

	priv = wiphy_priv(dev->ieee80211_ptr->wiphy);
	pstrWFIDrv = (struct host_if_drv *)priv->hWILCWFIDrv;

	if (!dev)
		return;

	PRINT_D(INIT_DBG, "Setting Multicast List with count = %d.\n", dev->mc.count);

	if (dev->flags & IFF_PROMISC) {
		/* Normally, we should configure the chip to retrive all packets
		 * but we don't wanna support this right now */
		/* TODO: add promiscuous mode support */
		PRINT_D(INIT_DBG, "Set promiscuous mode ON, retrive all packets\n");
		return;
	}

	/* If there's more addresses than we handle, get all multicast
	 * packets and sort them out in software. */
	if ((dev->flags & IFF_ALLMULTI) || (dev->mc.count) > WILC_MULTICAST_TABLE_SIZE) {
		PRINT_D(INIT_DBG, "Disable multicast filter, retrive all multicast packets\n");
		/* get all multicast packets */
		host_int_setup_multicast_filter(pstrWFIDrv, false, 0);
		return;
	}

	/* No multicast?  Just get our own stuff */
	if ((dev->mc.count) == 0) {
		PRINT_D(INIT_DBG, "Enable multicast filter, retrive directed packets only.\n");
		host_int_setup_multicast_filter(pstrWFIDrv, true, 0);
		return;
	}

	/* Store all of the multicast addresses in the hardware filter */
	netdev_for_each_mc_addr(ha, dev)
	{
		memcpy(gau8MulticastMacAddrList[i], ha->addr, ETH_ALEN);
		PRINT_D(INIT_DBG, "Entry[%d]: %x:%x:%x:%x:%x:%x\n", i,
			gau8MulticastMacAddrList[i][0], gau8MulticastMacAddrList[i][1], gau8MulticastMacAddrList[i][2], gau8MulticastMacAddrList[i][3], gau8MulticastMacAddrList[i][4], gau8MulticastMacAddrList[i][5]);
		i++;
	}

	host_int_setup_multicast_filter(pstrWFIDrv, true, (dev->mc.count));

	return;

}

static void linux_wlan_tx_complete(void *priv, int status)
{

	struct tx_complete_data *pv_data = (struct tx_complete_data *)priv;

	if (status == 1)
		PRINT_D(TX_DBG, "Packet sent successfully - Size = %d - Address = %p - SKB = %p\n", pv_data->size, pv_data->buff, pv_data->skb);
	else
		PRINT_D(TX_DBG, "Couldn't send packet - Size = %d - Address = %p - SKB = %p\n", pv_data->size, pv_data->buff, pv_data->skb);
	/* Free the SK Buffer, its work is done */
	dev_kfree_skb(pv_data->skb);
	kfree(pv_data);
}

int mac_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	perInterface_wlan_t *nic;
	struct tx_complete_data *tx_data = NULL;
	int QueueCount;
	char *pu8UdpBuffer;
	struct iphdr *ih;
	struct ethhdr *eth_h;
	struct wilc *wilc;

	nic = netdev_priv(ndev);
	wilc = nic->wilc;

	PRINT_D(TX_DBG, "Sending packet just received from TCP/IP\n");

	/* Stop the network interface queue */
	if (skb->dev != ndev) {
		PRINT_ER("Packet not destined to this device\n");
		return 0;
	}

	tx_data = kmalloc(sizeof(struct tx_complete_data), GFP_ATOMIC);
	if (tx_data == NULL) {
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

	/*get source and dest ip addresses*/
	ih = (struct iphdr *)(skb->data + sizeof(struct ethhdr));

	pu8UdpBuffer = (char *)ih + sizeof(struct iphdr);
	if ((pu8UdpBuffer[1] == 68 && pu8UdpBuffer[3] == 67) || (pu8UdpBuffer[1] == 67 && pu8UdpBuffer[3] == 68))
		PRINT_D(GENERIC_DBG, "DHCP Message transmitted, type:%x %x %x\n", pu8UdpBuffer[248], pu8UdpBuffer[249], pu8UdpBuffer[250]);

	PRINT_D(TX_DBG, "Sending packet - Size = %d - Address = %p - SKB = %p\n", tx_data->size, tx_data->buff, tx_data->skb);

	/* Send packet to MAC HW - for now the tx_complete function will be just status
	 * indicator. still not sure if I need to suspend host transmission till the tx_complete
	 * function called or not?
	 * allocated buffer will be freed in tx_complete function.
	 */
	PRINT_D(TX_DBG, "Adding tx packet to TX Queue\n");
	nic->netstats.tx_packets++;
	nic->netstats.tx_bytes += tx_data->size;
	tx_data->pBssid = wilc->vif[nic->u8IfIdx].bssid;
	QueueCount = wilc_wlan_txq_add_net_pkt(ndev, (void *)tx_data,
					       tx_data->buff, tx_data->size,
					       linux_wlan_tx_complete);

	if (QueueCount > FLOW_CONTROL_UPPER_THRESHOLD) {
		netif_stop_queue(wilc->vif[0].ndev);
		netif_stop_queue(wilc->vif[1].ndev);
	}

	return 0;
}

int mac_close(struct net_device *ndev)
{
	struct wilc_priv *priv;
	perInterface_wlan_t *nic;
	struct host_if_drv *pstrWFIDrv;
	struct wilc *wl;

	nic = netdev_priv(ndev);

	if ((nic == NULL) || (nic->wilc_netdev == NULL) || (nic->wilc_netdev->ieee80211_ptr == NULL) || (nic->wilc_netdev->ieee80211_ptr->wiphy == NULL)) {
		PRINT_ER("nic = NULL\n");
		return 0;
	}

	priv = wiphy_priv(nic->wilc_netdev->ieee80211_ptr->wiphy);
	wl = nic->wilc;

	if (priv == NULL) {
		PRINT_ER("priv = NULL\n");
		return 0;
	}

	pstrWFIDrv = (struct host_if_drv *)priv->hWILCWFIDrv;

	PRINT_D(GENERIC_DBG, "Mac close\n");

	if (!wl) {
		PRINT_ER("wl = NULL\n");
		return 0;
	}

	if (pstrWFIDrv == NULL)	{
		PRINT_ER("pstrWFIDrv = NULL\n");
		return 0;
	}

	if ((wl->open_ifcs) > 0) {
		wl->open_ifcs--;
	} else {
		PRINT_ER("ERROR: MAC close called while number of opened interfaces is zero\n");
		return 0;
	}

	if (nic->wilc_netdev != NULL) {
		/* Stop the network interface queue */
		netif_stop_queue(nic->wilc_netdev);

		wilc_deinit_host_int(nic->wilc_netdev);
	}

	if (wl->open_ifcs == 0) {
		PRINT_D(GENERIC_DBG, "Deinitializing wilc1000\n");
		wl->close = 1;
		wilc1000_wlan_deinit(ndev);
		WILC_WFI_deinit_mon_interface();
	}

	up(&close_exit_sync);
	nic->mac_opened = 0;

	return 0;
}

int mac_ioctl(struct net_device *ndev, struct ifreq *req, int cmd)
{

	u8 *buff = NULL;
	s8 rssi;
	u32 size = 0, length = 0;
	perInterface_wlan_t *nic;
	struct wilc_priv *priv;
	s32 s32Error = 0;
	struct wilc *wilc;

	/* struct iwreq *wrq = (struct iwreq *) req;	// tony moved to case SIOCSIWPRIV */
	nic = netdev_priv(ndev);
	wilc = nic->wilc;

	if (!wilc->initialized)
		return 0;

	switch (cmd) {

	/* ]] 2013-06-24 */
	case SIOCSIWPRIV:
	{
		struct iwreq *wrq = (struct iwreq *) req;               /* added by tony */

		size = wrq->u.data.length;

		if (size && wrq->u.data.pointer) {

			buff = memdup_user(wrq->u.data.pointer, wrq->u.data.length);
			if (IS_ERR(buff))
				return PTR_ERR(buff);

			if (strncasecmp(buff, "RSSI", length) == 0) {
				priv = wiphy_priv(nic->wilc_netdev->ieee80211_ptr->wiphy);
				s32Error = host_int_get_rssi(priv->hWILCWFIDrv, &(rssi));
				if (s32Error)
					PRINT_ER("Failed to send get rssi param's message queue ");
				PRINT_INFO(GENERIC_DBG, "RSSI :%d\n", rssi);

				/*Rounding up the rssi negative value*/
				rssi += 5;

				snprintf(buff, size, "rssi %d", rssi);

				if (copy_to_user(wrq->u.data.pointer, buff, size)) {
					PRINT_ER("%s: failed to copy data to user buffer\n", __func__);
					s32Error = -EFAULT;
					goto done;
				}
			}
		}
	}
	break;

	default:
	{
		PRINT_INFO(GENERIC_DBG, "Command - %d - has been received\n", cmd);
		s32Error = -EOPNOTSUPP;
		goto done;
	}
	}

done:

	kfree(buff);

	return s32Error;
}

void frmw_to_linux(struct wilc *wilc, u8 *buff, u32 size, u32 pkt_offset)
{

	unsigned int frame_len = 0;
	int stats;
	unsigned char *buff_to_send = NULL;
	struct sk_buff *skb;
	struct net_device *wilc_netdev;
	perInterface_wlan_t *nic;

	wilc_netdev = GetIfHandler(wilc, buff);
	if (wilc_netdev == NULL)
		return;

	buff += pkt_offset;
	nic = netdev_priv(wilc_netdev);

	if (size > 0) {

		frame_len = size;
		buff_to_send = buff;

		/* Need to send the packet up to the host, allocate a skb buffer */
		skb = dev_alloc_skb(frame_len);
		if (skb == NULL) {
			PRINT_ER("Low memory - packet droped\n");
			return;
		}

		if (wilc == NULL || wilc_netdev == NULL)
			PRINT_ER("wilc_netdev in wilc is NULL");
		skb->dev = wilc_netdev;

		if (skb->dev == NULL)
			PRINT_ER("skb->dev is NULL\n");

		/*
		 * for(i=0;i<40;i++)
		 * {
		 *      if(i<frame_len)
		 *              WILC_PRINTF("buff_to_send[%d]=%2x\n",i,buff_to_send[i]);
		 *
		 * }*/

		/* skb_put(skb, frame_len); */
		memcpy(skb_put(skb, frame_len), buff_to_send, frame_len);

		/* WILC_PRINTF("After MEM_CPY\n"); */

		/* nic = netdev_priv(wilc_netdev); */

		skb->protocol = eth_type_trans(skb, wilc_netdev);
		/* Send the packet to the stack by giving it to the bridge */
		nic->netstats.rx_packets++;
		nic->netstats.rx_bytes += frame_len;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		stats = netif_rx(skb);
		PRINT_D(RX_DBG, "netif_rx ret value is: %d\n", stats);
	}
}

void WILC_WFI_mgmt_rx(struct wilc *wilc, u8 *buff, u32 size)
{
	int i = 0;
	perInterface_wlan_t *nic;

	/*Pass the frame on the monitor interface, if any.*/
	/*Otherwise, pass it on p2p0 netdev, if registered on it*/
	for (i = 0; i < wilc->vif_num; i++) {
		nic = netdev_priv(wilc->vif[i].ndev);
		if (nic->monitor_flag) {
			WILC_WFI_monitor_rx(buff, size);
			return;
		}
	}

	nic = netdev_priv(wilc->vif[1].ndev); /* p2p0 */
	if ((buff[0] == nic->g_struct_frame_reg[0].frame_type && nic->g_struct_frame_reg[0].reg) ||
	    (buff[0] == nic->g_struct_frame_reg[1].frame_type && nic->g_struct_frame_reg[1].reg))
		WILC_WFI_p2p_rx(wilc->vif[1].ndev, buff, size);
}

void wl_wlan_cleanup(void)
{
	int i = 0;
	perInterface_wlan_t *nic[NUM_CONCURRENT_IFC];

	if (g_linux_wlan &&
	   (g_linux_wlan->vif[0].ndev || g_linux_wlan->vif[1].ndev)) {
		unregister_inetaddr_notifier(&g_dev_notifier);

		for (i = 0; i < NUM_CONCURRENT_IFC; i++)
			nic[i] = netdev_priv(g_linux_wlan->vif[i].ndev);
	}

	if (g_linux_wlan && g_linux_wlan->firmware)
		release_firmware(g_linux_wlan->firmware);

	if (g_linux_wlan &&
	   (g_linux_wlan->vif[0].ndev || g_linux_wlan->vif[1].ndev)) {
		linux_wlan_lock_timeout(&close_exit_sync, 12 * 1000);

		for (i = 0; i < NUM_CONCURRENT_IFC; i++)
			if (g_linux_wlan->vif[i].ndev)
				if (nic[i]->mac_opened)
					mac_close(g_linux_wlan->vif[i].ndev);

		for (i = 0; i < NUM_CONCURRENT_IFC; i++) {
			unregister_netdev(g_linux_wlan->vif[i].ndev);
			wilc_free_wiphy(g_linux_wlan->vif[i].ndev);
			free_netdev(g_linux_wlan->vif[i].ndev);
		}
	}

	kfree(g_linux_wlan);

#if defined(WILC_DEBUGFS)
	wilc_debugfs_remove();
#endif
	linux_wlan_device_detection(0);
	linux_wlan_device_power(0);
}

int wilc_netdev_init(struct wilc **wilc)
{
	int i;
	perInterface_wlan_t *nic;
	struct net_device *ndev;

	sema_init(&close_exit_sync, 0);

	/*create the common structure*/
	g_linux_wlan = kzalloc(sizeof(*g_linux_wlan), GFP_KERNEL);
	if (!g_linux_wlan)
		return -ENOMEM;

	*wilc = g_linux_wlan;

	register_inetaddr_notifier(&g_dev_notifier);

	for (i = 0; i < NUM_CONCURRENT_IFC; i++) {
		/*allocate first ethernet device with perinterface_wlan_t as its private data*/
		ndev = alloc_etherdev(sizeof(perInterface_wlan_t));
		if (!ndev) {
			PRINT_ER("Failed to allocate ethernet dev\n");
			return -1;
		}

		nic = netdev_priv(ndev);
		memset(nic, 0, sizeof(perInterface_wlan_t));

		/*Name the Devices*/
		if (i == 0) {
		#if defined(NM73131)    /* tony, 2012-09-20 */
			strcpy(ndev->name, "wilc_eth%d");
		#elif defined(PLAT_CLM9722)                     /* rachel */
			strcpy(ndev->name, "eth%d");
		#else /* PANDA_BOARD, PLAT_ALLWINNER_A10, PLAT_ALLWINNER_A20, PLAT_ALLWINNER_A31, PLAT_AML8726_M3 or PLAT_WMS8304 */
			strcpy(ndev->name, "wlan%d");
		#endif
		} else
			strcpy(ndev->name, "p2p%d");

		nic->u8IfIdx = g_linux_wlan->vif_num;
		nic->wilc_netdev = ndev;
		nic->wilc = *wilc;
		g_linux_wlan->vif[g_linux_wlan->vif_num].ndev = ndev;
		g_linux_wlan->vif_num++;
		ndev->netdev_ops = &wilc_netdev_ops;

		{
			struct wireless_dev *wdev;
			/*Register WiFi*/
			wdev = wilc_create_wiphy(ndev);

			#ifdef WILC_SDIO
			/* set netdev, tony */
			SET_NETDEV_DEV(ndev, &local_sdio_func->dev);
			#endif

			if (wdev == NULL) {
				PRINT_ER("Can't register WILC Wiphy\n");
				return -1;
			}

			/*linking the wireless_dev structure with the netdevice*/
			nic->wilc_netdev->ieee80211_ptr = wdev;
			nic->wilc_netdev->ml_priv = nic;
			wdev->netdev = nic->wilc_netdev;
			nic->netstats.rx_packets = 0;
			nic->netstats.tx_packets = 0;
			nic->netstats.rx_bytes = 0;
			nic->netstats.tx_bytes = 0;

		}

		if (register_netdev(ndev)) {
			PRINT_ER("Device couldn't be registered - %s\n", ndev->name);
			return -1; /* ERROR */
		}

		nic->iftype = STATION_MODE;
		nic->mac_opened = 0;

	}

	#ifndef WILC_SDIO
	if (!linux_spi_init(&g_linux_wlan->wilc_spidev)) {
		PRINT_ER("Can't initialize SPI\n");
		return -1; /* ERROR */
	}
	g_linux_wlan->wilc_spidev = wilc_spi_dev;
	#else
	g_linux_wlan->wilc_sdio_func = local_sdio_func;
	#endif

	return 0;
}

/*The 1st function called after module inserted*/
static int __init init_wilc_driver(void)
{
#ifdef WILC_SPI
	struct wilc *wilc;
#endif

#if defined(WILC_DEBUGFS)
	if (wilc_debugfs_init() < 0) {
		PRINT_D(GENERIC_DBG, "fail to create debugfs for wilc driver\n");
		return -1;
	}
#endif

	printk("IN INIT FUNCTION\n");
	printk("*** WILC1000 driver VERSION=[10.2] FW_VER=[10.2] ***\n");

	linux_wlan_device_power(1);
	msleep(100);
	linux_wlan_device_detection(1);

#ifdef WILC_SDIO
	{
		int ret;

		ret = sdio_register_driver(&wilc_bus);
		if (ret < 0)
			PRINT_D(INIT_DBG, "init_wilc_driver: Failed register sdio driver\n");

		return ret;
	}
#else
	PRINT_D(INIT_DBG, "Initializing netdev\n");
	if (wilc_netdev_init(&wilc))
		PRINT_ER("Couldn't initialize netdev\n");
	return 0;
#endif
}
late_initcall(init_wilc_driver);

static void __exit exit_wilc_driver(void)
{
#ifndef WILC_SDIO
	PRINT_D(INIT_DBG, "SPI unregister...\n");
	spi_unregister_driver(&wilc_bus);
#else
	PRINT_D(INIT_DBG, "SDIO unregister...\n");
	sdio_unregister_driver(&wilc_bus);
#endif
}
module_exit(exit_wilc_driver);

MODULE_LICENSE("GPL");
