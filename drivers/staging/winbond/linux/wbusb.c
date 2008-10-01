/*
 * Copyright 2008 Pavel Machek <pavel@suse.cz>
 *
 * Distribute under GPLv2.
 */
#include "sysdef.h"
#include <net/mac80211.h>


MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");


//============================================================
// vendor ID and product ID can into here for others
//============================================================
static struct usb_device_id Id_Table[] =
{
  {USB_DEVICE( 0x0416, 0x0035 )},
  {USB_DEVICE( 0x18E8, 0x6201 )},
  {USB_DEVICE( 0x18E8, 0x6206 )},
  {USB_DEVICE( 0x18E8, 0x6217 )},
  {USB_DEVICE( 0x18E8, 0x6230 )},
  {USB_DEVICE( 0x18E8, 0x6233 )},
  {USB_DEVICE( 0x1131, 0x2035 )},
  {  }
};

MODULE_DEVICE_TABLE(usb, Id_Table);

static struct usb_driver wb35_driver = {
	.name =		"w35und",
	.probe =	wb35_probe,
	.disconnect = wb35_disconnect,
	.id_table = Id_Table,
};

static const struct ieee80211_rate wbsoft_rates[] = {
	{ .bitrate = 10, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
};

static const struct ieee80211_channel wbsoft_channels[] = {
	{ .center_freq = 2412},
};

int wbsoft_enabled;
struct ieee80211_hw *my_dev;
PADAPTER my_adapter;

static int wbsoft_add_interface(struct ieee80211_hw *dev,
				 struct ieee80211_if_init_conf *conf)
{
	printk("wbsoft_add interface called\n");
	return 0;
}

static void wbsoft_remove_interface(struct ieee80211_hw *dev,
				     struct ieee80211_if_init_conf *conf)
{
	printk("wbsoft_remove interface called\n");
}

static int wbsoft_nop(void)
{
	printk("wbsoft_nop called\n");
	return 0;
}

static void wbsoft_configure_filter(struct ieee80211_hw *dev,
				     unsigned int changed_flags,
				     unsigned int *total_flags,
				     int mc_count, struct dev_mc_list *mclist)
{
	unsigned int bit_nr, new_flags;
	u32 mc_filter[2];
	int i;

	new_flags = 0;

	if (*total_flags & FIF_PROMISC_IN_BSS) {
		new_flags |= FIF_PROMISC_IN_BSS;
		mc_filter[1] = mc_filter[0] = ~0;
	} else if ((*total_flags & FIF_ALLMULTI) || (mc_count > 32)) {
		new_flags |= FIF_ALLMULTI;
		mc_filter[1] = mc_filter[0] = ~0;
	} else {
		mc_filter[1] = mc_filter[0] = 0;
		for (i = 0; i < mc_count; i++) {
			if (!mclist)
				break;
			printk("Should call ether_crc here\n");
			//bit_nr = ether_crc(ETH_ALEN, mclist->dmi_addr) >> 26;
			bit_nr = 0;

			bit_nr &= 0x3F;
			mc_filter[bit_nr >> 5] |= 1 << (bit_nr & 31);
			mclist = mclist->next;
		}
	}

	dev->flags &= ~IEEE80211_HW_RX_INCLUDES_FCS;

	*total_flags = new_flags;
}

static int wbsoft_tx(struct ieee80211_hw *dev, struct sk_buff *skb,
		      struct ieee80211_tx_control *control)
{
	char *buffer = kmalloc(skb->len, GFP_ATOMIC);
	printk("Sending frame %d bytes\n", skb->len);
	memcpy(buffer, skb->data, skb->len);
	if (1 == MLMESendFrame(my_adapter, buffer, skb->len, FRAME_TYPE_802_11_MANAGEMENT))
		printk("frame sent ok (%d bytes)?\n", skb->len);
	return NETDEV_TX_OK;
}


static int wbsoft_start(struct ieee80211_hw *dev)
{
	wbsoft_enabled = 1;
	printk("wbsoft_start called\n");
	return 0;
}

static int wbsoft_config(struct ieee80211_hw *dev, struct ieee80211_conf *conf)
{
	ChanInfo ch;
	printk("wbsoft_config called\n");

	ch.band = 1;
	ch.ChanNo = 1;	/* Should use channel_num, or something, as that is already pre-translated */


	hal_set_current_channel(&my_adapter->sHwData, ch);
	hal_set_beacon_period(&my_adapter->sHwData, conf->beacon_int);
//	hal_set_cap_info(&my_adapter->sHwData, ?? );
// hal_set_ssid(phw_data_t pHwData,  PUCHAR pssid,  u8 ssid_len); ??
	hal_set_accept_broadcast(&my_adapter->sHwData, 1);
	hal_set_accept_promiscuous(&my_adapter->sHwData,  1);
	hal_set_accept_multicast(&my_adapter->sHwData,  1);
	hal_set_accept_beacon(&my_adapter->sHwData,  1);
	hal_set_radio_mode(&my_adapter->sHwData,  0);
	//hal_set_antenna_number(  phw_data_t pHwData, u8 number )
	//hal_set_rf_power(phw_data_t pHwData, u8 PowerIndex)


//	hal_start_bss(&my_adapter->sHwData, WLAN_BSSTYPE_INFRASTRUCTURE);	??

//void hal_set_rates(phw_data_t pHwData, PUCHAR pbss_rates,
//		   u8 length, unsigned char basic_rate_set)

	return 0;
}

static int wbsoft_config_interface(struct ieee80211_hw *dev,
				    struct ieee80211_vif *vif,
				    struct ieee80211_if_conf *conf)
{
	printk("wbsoft_config_interface called\n");
	return 0;
}

static u64 wbsoft_get_tsf(struct ieee80211_hw *dev)
{
	printk("wbsoft_get_tsf called\n");
	return 0;
}

static const struct ieee80211_ops wbsoft_ops = {
	.tx			= wbsoft_tx,
	.start			= wbsoft_start,		/* Start can be pretty much empty as we do WbWLanInitialize() during probe? */
	.stop			= wbsoft_nop,
	.add_interface		= wbsoft_add_interface,
	.remove_interface	= wbsoft_remove_interface,
	.config			= wbsoft_config,
	.config_interface	= wbsoft_config_interface,
	.configure_filter	= wbsoft_configure_filter,
	.get_stats		= wbsoft_nop,
	.get_tx_stats		= wbsoft_nop,
	.get_tsf		= wbsoft_get_tsf,
// conf_tx: hal_set_cwmin()/hal_set_cwmax;
};

struct wbsoft_priv {
};


int __init wb35_init(void)
{
	printk("[w35und]driver init\n");
	return usb_register(&wb35_driver);
}

void __exit wb35_exit(void)
{
	printk("[w35und]driver exit\n");
	usb_deregister( &wb35_driver );
}

module_init(wb35_init);
module_exit(wb35_exit);

// Usb kernel subsystem will call this function when a new device is plugged into.
int wb35_probe(struct usb_interface *intf, const struct usb_device_id *id_table)
{
	PADAPTER	Adapter;
	PWBLINUX	pWbLinux;
	PWBUSB		pWbUsb;
        struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
	int	i, ret = -1;
	u32	ltmp;
	struct usb_device *udev = interface_to_usbdev(intf);

	usb_get_dev(udev);

	printk("[w35und]wb35_probe ->\n");

	do {
		for (i=0; i<(sizeof(Id_Table)/sizeof(struct usb_device_id)); i++ ) {
			if ((udev->descriptor.idVendor == Id_Table[i].idVendor) &&
				(udev->descriptor.idProduct == Id_Table[i].idProduct)) {
				printk("[w35und]Found supported hardware\n");
				break;
			}
		}
		if ((i == (sizeof(Id_Table)/sizeof(struct usb_device_id)))) {
			#ifdef _PE_USB_INI_DUMP_
			WBDEBUG(("[w35und] This is not the one we are interested about\n"));
			#endif
			return -ENODEV;
		}

		// 20060630.2 Check the device if it already be opened
		ret = usb_control_msg(udev, usb_rcvctrlpipe( udev, 0 ),
				      0x01, USB_TYPE_VENDOR|USB_RECIP_DEVICE|USB_DIR_IN,
				      0x0, 0x400, &ltmp, 4, HZ*100 );
		if( ret < 0 )
			break;

		ltmp = cpu_to_le32(ltmp);
		if (ltmp)  // Is already initialized?
			break;


		Adapter = kzalloc(sizeof(ADAPTER), GFP_KERNEL);

		my_adapter = Adapter;
		pWbLinux = &Adapter->WbLinux;
		pWbUsb = &Adapter->sHwData.WbUsb;
		pWbUsb->udev = udev;

	        interface = intf->cur_altsetting;
	        endpoint = &interface->endpoint[0].desc;

		if (endpoint[2].wMaxPacketSize == 512) {
			printk("[w35und] Working on USB 2.0\n");
			pWbUsb->IsUsb20 = 1;
		}

		if (!WbWLanInitialize(Adapter)) {
			printk("[w35und]WbWLanInitialize fail\n");
			break;
		}

		{
			struct wbsoft_priv *priv;
			struct ieee80211_hw *dev;
			int res;

			dev = ieee80211_alloc_hw(sizeof(*priv), &wbsoft_ops);

			if (!dev) {
				printk("w35und: ieee80211 alloc failed\n" );
				BUG();
			}

			my_dev = dev;

			SET_IEEE80211_DEV(dev, &udev->dev);
			{
				phw_data_t pHwData = &Adapter->sHwData;
				unsigned char		dev_addr[MAX_ADDR_LEN];
				hal_get_permanent_address(pHwData, dev_addr);
				SET_IEEE80211_PERM_ADDR(dev, dev_addr);
			}


			dev->extra_tx_headroom = 12;	/* FIXME */
			dev->flags = 0;

			dev->channel_change_time = 1000;
//			dev->max_rssi = 100;

			dev->queues = 1;

			static struct ieee80211_supported_band band;

			band.channels = wbsoft_channels;
			band.n_channels = ARRAY_SIZE(wbsoft_channels);
			band.bitrates = wbsoft_rates;
			band.n_bitrates = ARRAY_SIZE(wbsoft_rates);

			dev->wiphy->bands[IEEE80211_BAND_2GHZ] = &band;
#if 0
			wbsoft_modes[0].num_channels = 1;
			wbsoft_modes[0].channels = wbsoft_channels;
			wbsoft_modes[0].mode = MODE_IEEE80211B;
			wbsoft_modes[0].num_rates = ARRAY_SIZE(wbsoft_rates);
			wbsoft_modes[0].rates = wbsoft_rates;

			res = ieee80211_register_hwmode(dev, &wbsoft_modes[0]);
			BUG_ON(res);
#endif

			res = ieee80211_register_hw(dev);
			BUG_ON(res);
		}

		usb_set_intfdata( intf, Adapter );

		printk("[w35und] _probe OK\n");
		return 0;

	} while(FALSE);

	return -ENOMEM;
}

void packet_came(char *pRxBufferAddress, int PacketSize)
{
	struct sk_buff *skb;
	struct ieee80211_rx_status rx_status = {0};

	if (!wbsoft_enabled)
		return;

	skb = dev_alloc_skb(PacketSize);
	if (!skb) {
		printk("Not enough memory for packet, FIXME\n");
		return;
	}

	memcpy(skb_put(skb, PacketSize),
	       pRxBufferAddress,
	       PacketSize);

/*
	rx_status.rate = 10;
	rx_status.channel = 1;
	rx_status.freq = 12345;
	rx_status.phymode = MODE_IEEE80211B;
*/

	ieee80211_rx_irqsafe(my_dev, skb, &rx_status);
}

unsigned char
WbUsb_initial(phw_data_t pHwData)
{
	return 1;
}


void
WbUsb_destroy(phw_data_t pHwData)
{
}

int wb35_open(struct net_device *netdev)
{
	PADAPTER Adapter = (PADAPTER)netdev->priv;
	phw_data_t pHwData = &Adapter->sHwData;

        netif_start_queue(netdev);

	//TODO : put here temporarily
	hal_set_accept_broadcast(pHwData, 1); // open accept broadcast

	return 0;
}

int wb35_close(struct net_device *netdev)
{
	netif_stop_queue(netdev);
	return 0;
}

void wb35_disconnect(struct usb_interface *intf)
{
	PWBLINUX pWbLinux;
	PADAPTER Adapter = usb_get_intfdata(intf);
	usb_set_intfdata(intf, NULL);

        pWbLinux = &Adapter->WbLinux;

	// Card remove
	WbWlanHalt(Adapter);

}


