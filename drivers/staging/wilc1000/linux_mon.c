/*!
 *  @file	linux_mon.c
 *  @brief	File Operations OS wrapper functionality
 *  @author	mdaftedar
 *  @sa		wilc_wfi_netdevice.h
 *  @date	01 MAR 2012
 *  @version	1.0
 */
#include "wilc_wfi_cfgoperations.h"
#include "linux_wlan_common.h"
#include "wilc_wlan_if.h"
#include "wilc_wlan.h"

#ifdef WILC_FULLY_HOSTING_AP
#include "wilc_host_ap.h"
#endif
#ifdef WILC_AP_EXTERNAL_MLME

struct wilc_wfi_radiotap_hdr {
	struct ieee80211_radiotap_header hdr;
	u8 rate;
	/* u32 channel; */
} __attribute__((packed));

struct wilc_wfi_radiotap_cb_hdr {
	struct ieee80211_radiotap_header hdr;
	u8 rate;
	u8 dump;
	u16 tx_flags;
	/* u32 channel; */
} __attribute__((packed));

extern linux_wlan_t *g_linux_wlan;

static struct net_device *wilc_wfi_mon; /* global monitor netdev */

#if USE_WIRELESS
extern int  mac_xmit(struct sk_buff *skb, struct net_device *dev);
#endif


u8 srcAdd[6];
u8 bssid[6];
u8 broadcast[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
/**
 *  @brief      WILC_WFI_monitor_rx
 *  @details
 *  @param[in]
 *  @return     int : Return 0 on Success
 *  @author	mdaftedar
 *  @date	12 JUL 2012
 *  @version	1.0
 */

#define IEEE80211_RADIOTAP_F_TX_RTS	0x0004  /* used rts/cts handshake */
#define IEEE80211_RADIOTAP_F_TX_FAIL	0x0001  /* failed due to excessive*/
#define IS_MANAGMEMENT				0x100
#define IS_MANAGMEMENT_CALLBACK			0x080
#define IS_MGMT_STATUS_SUCCES			0x040
#define GET_PKT_OFFSET(a) (((a) >> 22) & 0x1ff)

void WILC_WFI_monitor_rx(uint8_t *buff, uint32_t size)
{
	uint32_t header, pkt_offset;
	struct sk_buff *skb = NULL;
	struct wilc_wfi_radiotap_hdr *hdr;
	struct wilc_wfi_radiotap_cb_hdr *cb_hdr;

	PRINT_INFO(HOSTAPD_DBG, "In monitor interface receive function\n");

	/*   struct WILC_WFI_priv *priv = netdev_priv(dev); */

	/*   priv = wiphy_priv(priv->dev->ieee80211_ptr->wiphy); */

	/* Bug 4601 */
	if (wilc_wfi_mon == NULL)
		return;

	if (!netif_running(wilc_wfi_mon)) {
		PRINT_INFO(HOSTAPD_DBG, "Monitor interface already RUNNING\n");
		return;
	}

	/* Get WILC header */
	memcpy(&header, (buff - HOST_HDR_OFFSET), HOST_HDR_OFFSET);

	/* The packet offset field conain info about what type of managment frame */
	/* we are dealing with and ack status */
	pkt_offset = GET_PKT_OFFSET(header);

	if (pkt_offset & IS_MANAGMEMENT_CALLBACK) {

		/* hostapd callback mgmt frame */

		skb = dev_alloc_skb(size + sizeof(struct wilc_wfi_radiotap_cb_hdr));
		if (skb == NULL) {
			PRINT_INFO(HOSTAPD_DBG, "Monitor if : No memory to allocate skb");
			return;
		}

		memcpy(skb_put(skb, size), buff, size);

		cb_hdr = (struct wilc_wfi_radiotap_cb_hdr *) skb_push(skb, sizeof(*cb_hdr));
		memset(cb_hdr, 0, sizeof(struct wilc_wfi_radiotap_cb_hdr));

		cb_hdr->hdr.it_version = 0; /* PKTHDR_RADIOTAP_VERSION; */

		cb_hdr->hdr.it_len = cpu_to_le16(sizeof(struct wilc_wfi_radiotap_cb_hdr));

		cb_hdr->hdr.it_present = cpu_to_le32(
				(1 << IEEE80211_RADIOTAP_RATE) |
				(1 << IEEE80211_RADIOTAP_TX_FLAGS));

		cb_hdr->rate = 5; /* txrate->bitrate / 5; */

		if (pkt_offset & IS_MGMT_STATUS_SUCCES)	{
			/* success */
			cb_hdr->tx_flags = IEEE80211_RADIOTAP_F_TX_RTS;
		} else {
			cb_hdr->tx_flags = IEEE80211_RADIOTAP_F_TX_FAIL;
		}

	} else {

		skb = dev_alloc_skb(size + sizeof(struct wilc_wfi_radiotap_hdr));

		if (skb == NULL) {
			PRINT_INFO(HOSTAPD_DBG, "Monitor if : No memory to allocate skb");
			return;
		}

		/* skb = skb_copy_expand(tx_skb, sizeof(*hdr), 0, GFP_ATOMIC); */
		/* if (skb == NULL) */
		/*      return; */

		memcpy(skb_put(skb, size), buff, size);
		hdr = (struct wilc_wfi_radiotap_hdr *) skb_push(skb, sizeof(*hdr));
		memset(hdr, 0, sizeof(struct wilc_wfi_radiotap_hdr));
		hdr->hdr.it_version = 0; /* PKTHDR_RADIOTAP_VERSION; */
		/* hdr->hdr.it_pad = 0; */
		hdr->hdr.it_len = cpu_to_le16(sizeof(struct wilc_wfi_radiotap_hdr));
		PRINT_INFO(HOSTAPD_DBG, "Radiotap len %d\n", hdr->hdr.it_len);
		hdr->hdr.it_present = cpu_to_le32
				(1 << IEEE80211_RADIOTAP_RATE);                   /* | */
		/* (1 << IEEE80211_RADIOTAP_CHANNEL)); */
		PRINT_INFO(HOSTAPD_DBG, "Presentflags %d\n", hdr->hdr.it_present);
		hdr->rate = 5; /* txrate->bitrate / 5; */

	}

/*	if(INFO || if(skb->data[9] == 0x00 || skb->data[9] == 0xb0))
 *      {
 *              for(i=0;i<skb->len;i++)
 *                      PRINT_INFO(HOSTAPD_DBG,"Mon RxData[%d] = %02x\n",i,skb->data[i]);
 *      }*/


	skb->dev = wilc_wfi_mon;
	skb_set_mac_header(skb, 0);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);
	memset(skb->cb, 0, sizeof(skb->cb));

	netif_rx(skb);


}

struct tx_complete_mon_data {
	int size;
	void *buff;
};

static void mgmt_tx_complete(void *priv, int status)
{

	/* struct sk_buff *skb2; */
	/* struct wilc_wfi_radiotap_cb_hdr *cb_hdr; */

	struct tx_complete_mon_data *pv_data = (struct tx_complete_mon_data *)priv;
	u8 *buf =  pv_data->buff;



	if (status == 1) {
		if (INFO || buf[0] == 0x10 || buf[0] == 0xb0)
			PRINT_INFO(HOSTAPD_DBG, "Packet sent successfully - Size = %d - Address = %p.\n", pv_data->size, pv_data->buff);
	} else {
		PRINT_INFO(HOSTAPD_DBG, "Couldn't send packet - Size = %d - Address = %p.\n", pv_data->size, pv_data->buff);
	}


/*			//(skb->data[9] == 0x00 || skb->data[9] == 0xb0 || skb->data[9] == 0x40 ||  skb->data[9] == 0xd0 )
 *      {
 *              skb2 = dev_alloc_skb(pv_data->size+sizeof(struct wilc_wfi_radiotap_cb_hdr));
 *
 *              memcpy(skb_put(skb2,pv_data->size),pv_data->buff, pv_data->size);
 *
 *              cb_hdr = (struct wilc_wfi_radiotap_cb_hdr *) skb_push(skb2, sizeof(*cb_hdr));
 *              memset(cb_hdr, 0, sizeof(struct wilc_wfi_radiotap_cb_hdr));
 *
 *               cb_hdr->hdr.it_version = 0;//PKTHDR_RADIOTAP_VERSION;
 *
 *              cb_hdr->hdr.it_len = cpu_to_le16(sizeof(struct wilc_wfi_radiotap_cb_hdr));
 *
 *       cb_hdr->hdr.it_present = cpu_to_le32(
 *                                        (1 << IEEE80211_RADIOTAP_RATE) |
 *                                       (1 << IEEE80211_RADIOTAP_TX_FLAGS));
 *
 *              cb_hdr->rate = 5;//txrate->bitrate / 5;
 *              cb_hdr->tx_flags = 0x0004;
 *
 *              skb2->dev = wilc_wfi_mon;
 *              skb_set_mac_header(skb2, 0);
 *              skb2->ip_summed = CHECKSUM_UNNECESSARY;
 *              skb2->pkt_type = PACKET_OTHERHOST;
 *              skb2->protocol = htons(ETH_P_802_2);
 *              memset(skb2->cb, 0, sizeof(skb2->cb));
 *
 *              netif_rx(skb2);
 *      }*/

	/* incase of fully hosting mode, the freeing will be done in response to the cfg packet */
	#ifndef WILC_FULLY_HOSTING_AP
	kfree(pv_data->buff);

	kfree(pv_data);
	#endif
}
static int mon_mgmt_tx(struct net_device *dev, const u8 *buf, size_t len)
{
	struct tx_complete_mon_data *mgmt_tx = NULL;

	if (dev == NULL) {
		PRINT_D(HOSTAPD_DBG, "ERROR: dev == NULL\n");
		return WILC_FAIL;
	}

	netif_stop_queue(dev);
	mgmt_tx = kmalloc(sizeof(struct tx_complete_mon_data), GFP_ATOMIC);
	if (mgmt_tx == NULL) {
		PRINT_ER("Failed to allocate memory for mgmt_tx structure\n");
		return WILC_FAIL;
	}

	#ifdef WILC_FULLY_HOSTING_AP
	/* add space for the pointer to tx_complete_mon_data */
	len += sizeof(struct tx_complete_mon_data *);
	#endif

	mgmt_tx->buff = kmalloc(len, GFP_ATOMIC);
	if (mgmt_tx->buff == NULL) {
		PRINT_ER("Failed to allocate memory for mgmt_tx buff\n");
		return WILC_FAIL;

	}

	mgmt_tx->size = len;

	#ifndef WILC_FULLY_HOSTING_AP
	memcpy(mgmt_tx->buff, buf, len);
	#else
	memcpy(mgmt_tx->buff, buf, len - sizeof(struct tx_complete_mon_data *));
	memcpy((mgmt_tx->buff) + (len - sizeof(struct tx_complete_mon_data *)), &mgmt_tx, sizeof(struct tx_complete_mon_data *));

	/* filter data frames to handle it's PS */
	if (filter_monitor_data_frames((mgmt_tx->buff), len) == true) {
		return;
	}

	#endif /* WILC_FULLY_HOSTING_AP */

	g_linux_wlan->oup.wlan_add_mgmt_to_tx_que(mgmt_tx, mgmt_tx->buff, mgmt_tx->size, mgmt_tx_complete);

	netif_wake_queue(dev);
	return 0;
}

/**
 *  @brief      WILC_WFI_mon_xmit
 *  @details
 *  @param[in]
 *  @return     int : Return 0 on Success
 *  @author	mdaftedar
 *  @date	12 JUL 2012
 *  @version	1.0
 */
static netdev_tx_t WILC_WFI_mon_xmit(struct sk_buff *skb,
				     struct net_device *dev)
{
	u32 rtap_len, i, ret = 0;
	struct WILC_WFI_mon_priv  *mon_priv;

	struct sk_buff *skb2;
	struct wilc_wfi_radiotap_cb_hdr *cb_hdr;

	/* Bug 4601 */
	if (wilc_wfi_mon == NULL)
		return WILC_FAIL;

	/* if(skb->data[3] == 0x10 || skb->data[3] == 0xb0) */

	mon_priv = netdev_priv(wilc_wfi_mon);

	if (mon_priv == NULL) {
		PRINT_ER("Monitor interface private structure is NULL\n");
		return WILC_FAIL;
	}


	rtap_len = ieee80211_get_radiotap_len(skb->data);
	if (skb->len < rtap_len) {
		PRINT_ER("Error in radiotap header\n");
		return -1;
	}
	/* skip the radiotap header */
	PRINT_INFO(HOSTAPD_DBG, "Radiotap len: %d\n", rtap_len);

	if (INFO) {
		for (i = 0; i < rtap_len; i++)
			PRINT_INFO(HOSTAPD_DBG, "Radiotap_hdr[%d] %02x\n", i, skb->data[i]);
	}
	/* Skip the ratio tap header */
	skb_pull(skb, rtap_len);

	if (skb->data[0] == 0xc0)
		PRINT_INFO(HOSTAPD_DBG, "%x:%x:%x:%x:%x%x\n", skb->data[4], skb->data[5], skb->data[6], skb->data[7], skb->data[8], skb->data[9]);

	if (skb->data[0] == 0xc0 && (!(memcmp(broadcast, &skb->data[4], 6)))) {
		skb2 = dev_alloc_skb(skb->len + sizeof(struct wilc_wfi_radiotap_cb_hdr));

		memcpy(skb_put(skb2, skb->len), skb->data, skb->len);

		cb_hdr = (struct wilc_wfi_radiotap_cb_hdr *) skb_push(skb2, sizeof(*cb_hdr));
		memset(cb_hdr, 0, sizeof(struct wilc_wfi_radiotap_cb_hdr));

		cb_hdr->hdr.it_version = 0; /* PKTHDR_RADIOTAP_VERSION; */

		cb_hdr->hdr.it_len = cpu_to_le16(sizeof(struct wilc_wfi_radiotap_cb_hdr));

		cb_hdr->hdr.it_present = cpu_to_le32(
				(1 << IEEE80211_RADIOTAP_RATE) |
				(1 << IEEE80211_RADIOTAP_TX_FLAGS));

		cb_hdr->rate = 5; /* txrate->bitrate / 5; */
		cb_hdr->tx_flags = 0x0004;

		skb2->dev = wilc_wfi_mon;
		skb_set_mac_header(skb2, 0);
		skb2->ip_summed = CHECKSUM_UNNECESSARY;
		skb2->pkt_type = PACKET_OTHERHOST;
		skb2->protocol = htons(ETH_P_802_2);
		memset(skb2->cb, 0, sizeof(skb2->cb));

		netif_rx(skb2);

		return 0;
	}
	skb->dev = mon_priv->real_ndev;

	PRINT_INFO(HOSTAPD_DBG, "Skipping the radiotap header\n");



	/* actual deliver of data is device-specific, and not shown here */
	PRINT_INFO(HOSTAPD_DBG, "SKB netdevice name = %s\n", skb->dev->name);
	PRINT_INFO(HOSTAPD_DBG, "MONITOR real dev name = %s\n", mon_priv->real_ndev->name);

	#if USE_WIRELESS
	/* Identify if Ethernet or MAC header (data or mgmt) */
	memcpy(srcAdd, &skb->data[10], 6);
	memcpy(bssid, &skb->data[16], 6);
	/* if source address and bssid fields are equal>>Mac header */
	/*send it to mgmt frames handler */
	if (!(memcmp(srcAdd, bssid, 6))) {
		mon_mgmt_tx(mon_priv->real_ndev, skb->data, skb->len);
		dev_kfree_skb(skb);
	} else
		ret = mac_xmit(skb, mon_priv->real_ndev);
	#endif

	/* return NETDEV_TX_OK; */
	return ret;
}

static const struct net_device_ops wilc_wfi_netdev_ops = {
	.ndo_start_xmit         = WILC_WFI_mon_xmit,

};

#ifdef WILC_FULLY_HOSTING_AP
/*
 *  @brief                      WILC_mgm_HOSTAPD_ACK
 *  @details            report the status of transmitted mgmt frames to HOSTAPD
 *  @param[in]          priv : pointer to tx_complete_mon_data struct
 *				bStatus : status of transmission
 *  @author		Abd Al-Rahman Diab
 *  @date			9 May 2013
 *  @version		1.0
 */
void WILC_mgm_HOSTAPD_ACK(void *priv, bool bStatus)
{
	struct sk_buff *skb;
	struct wilc_wfi_radiotap_cb_hdr *cb_hdr;

	struct tx_complete_mon_data *pv_data = (struct tx_complete_mon_data *)priv;
	u8 *buf =  pv_data->buff;

	/* len of the original frame without the added pointer at the tail */
	u16 u16len = (pv_data->size) - sizeof(struct tx_complete_mon_data *);


	/*if(bStatus == 1){
	 *      if(INFO || buf[0] == 0x10 || buf[0] == 0xb0)
	 *      PRINT_D(HOSTAPD_DBG,"Packet sent successfully - Size = %d - Address = %p.\n",u16len,pv_data->buff);
	 * }else{
	 *              PRINT_D(HOSTAPD_DBG,"Couldn't send packet - Size = %d - Address = %p.\n",u16len,pv_data->buff);
	 *      }
	 */

	/* (skb->data[9] == 0x00 || skb->data[9] == 0xb0 || skb->data[9] == 0x40 ||  skb->data[9] == 0xd0 ) */
	{
		skb = dev_alloc_skb(u16len + sizeof(struct wilc_wfi_radiotap_cb_hdr));

		memcpy(skb_put(skb, u16len), pv_data->buff, u16len);

		cb_hdr = (struct wilc_wfi_radiotap_cb_hdr *) skb_push(skb, sizeof(*cb_hdr));
		memset(cb_hdr, 0, sizeof(struct wilc_wfi_radiotap_cb_hdr));

		cb_hdr->hdr.it_version = 0; /* PKTHDR_RADIOTAP_VERSION; */

		cb_hdr->hdr.it_len = cpu_to_le16(sizeof(struct wilc_wfi_radiotap_cb_hdr));

		cb_hdr->hdr.it_present = cpu_to_le32(
				(1 << IEEE80211_RADIOTAP_RATE) |
				(1 << IEEE80211_RADIOTAP_TX_FLAGS));

		cb_hdr->rate = 5; /* txrate->bitrate / 5; */


		if (bStatus) {
			/* success */
			cb_hdr->tx_flags = IEEE80211_RADIOTAP_F_TX_RTS;
		} else {
			cb_hdr->tx_flags = IEEE80211_RADIOTAP_F_TX_FAIL;
		}

		skb->dev = wilc_wfi_mon;
		skb_set_mac_header(skb, 0);
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		skb->pkt_type = PACKET_OTHERHOST;
		skb->protocol = htons(ETH_P_802_2);
		memset(skb->cb, 0, sizeof(skb->cb));

		netif_rx(skb);
	}

	/* incase of fully hosting mode, the freeing will be done in response to the cfg packet */
	kfree(pv_data->buff);

	kfree(pv_data);

}
#endif /* WILC_FULLY_HOSTING_AP */

/**
 *  @brief      WILC_WFI_mon_setup
 *  @details
 *  @param[in]
 *  @return     int : Return 0 on Success
 *  @author	mdaftedar
 *  @date	12 JUL 2012
 *  @version	1.0
 */
static void WILC_WFI_mon_setup(struct net_device *dev)
{

	dev->netdev_ops = &wilc_wfi_netdev_ops;
	/* dev->destructor = free_netdev; */
	PRINT_INFO(CORECONFIG_DBG, "In Ethernet setup function\n");
	ether_setup(dev);
	dev->priv_flags |= IFF_NO_QUEUE;
	dev->type = ARPHRD_IEEE80211_RADIOTAP;
	eth_zero_addr(dev->dev_addr);

	#ifdef USE_WIRELESS
	{
		/* u8 * mac_add; */
		unsigned char mac_add[] = {0x00, 0x50, 0xc2, 0x5e, 0x10, 0x8f};
		/* priv = wiphy_priv(priv->dev->ieee80211_ptr->wiphy); */
		/* mac_add = (u8*)WILC_MALLOC(ETH_ALEN); */
		/* status = host_int_get_MacAddress(priv->hWILCWFIDrv,mac_add); */
		/* mac_add[ETH_ALEN-1]+=1; */
		memcpy(dev->dev_addr, mac_add, ETH_ALEN);
	}
	#else
	dev->dev_addr[0] = 0x12;
	#endif

}

/**
 *  @brief      WILC_WFI_init_mon_interface
 *  @details
 *  @param[in]
 *  @return     int : Return 0 on Success
 *  @author	mdaftedar
 *  @date	12 JUL 2012
 *  @version	1.0
 */
struct net_device *WILC_WFI_init_mon_interface(const char *name, struct net_device *real_dev)
{


	u32 ret = WILC_SUCCESS;
	struct WILC_WFI_mon_priv *priv;

	/*If monitor interface is already initialized, return it*/
	if (wilc_wfi_mon) {
		return wilc_wfi_mon;
	}

	wilc_wfi_mon = alloc_etherdev(sizeof(struct WILC_WFI_mon_priv));
	if (!wilc_wfi_mon) {
		PRINT_ER("failed to allocate memory\n");
		return NULL;

	}

	wilc_wfi_mon->type = ARPHRD_IEEE80211_RADIOTAP;
	strncpy(wilc_wfi_mon->name, name, IFNAMSIZ);
	wilc_wfi_mon->name[IFNAMSIZ - 1] = 0;
	wilc_wfi_mon->netdev_ops = &wilc_wfi_netdev_ops;

	ret = register_netdevice(wilc_wfi_mon);
	if (ret) {
		PRINT_ER(" register_netdevice failed (%d)\n", ret);
		return NULL;
	}
	priv = netdev_priv(wilc_wfi_mon);
	if (priv == NULL) {
		PRINT_ER("private structure is NULL\n");
		return NULL;
	}

	priv->real_ndev = real_dev;

	return wilc_wfi_mon;
}

/**
 *  @brief      WILC_WFI_deinit_mon_interface
 *  @details
 *  @param[in]
 *  @return     int : Return 0 on Success
 *  @author	mdaftedar
 *  @date	12 JUL 2012
 *  @version	1.0
 */
int WILC_WFI_deinit_mon_interface(void)
{
	bool rollback_lock = false;

	if (wilc_wfi_mon != NULL) {
		PRINT_D(HOSTAPD_DBG, "In Deinit monitor interface\n");
		PRINT_D(HOSTAPD_DBG, "RTNL is being locked\n");
		if (rtnl_is_locked()) {
			rtnl_unlock();
			rollback_lock = true;
		}
		PRINT_D(HOSTAPD_DBG, "Unregister netdev\n");
		unregister_netdev(wilc_wfi_mon);
		/* free_netdev(wilc_wfi_mon); */

		if (rollback_lock) {
			rtnl_lock();
			rollback_lock = false;
		}
		wilc_wfi_mon = NULL;
	}
	return WILC_SUCCESS;

}
#endif /* WILC_AP_EXTERNAL_MLME */
