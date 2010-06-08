/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192U
 *
 * Based on the r8187 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andreamrl@tiscali.it>, et al.
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * Jerry chuang <wlanfae@realtek.com>
 */

#include <linux/vmalloc.h>
#include <linux/notifier.h>

#undef LOOP_TEST
#undef DUMP_RX
#undef DUMP_TX
#undef DEBUG_TX_DESC2
#undef RX_DONT_PASS_UL
#undef DEBUG_EPROM
#undef DEBUG_RX_VERBOSE
#undef DUMMY_RX
#undef DEBUG_ZERO_RX
#undef DEBUG_RX_SKB
#undef DEBUG_TX_FRAG
#undef DEBUG_RX_FRAG
#undef DEBUG_TX_FILLDESC
#undef DEBUG_TX
#undef DEBUG_IRQ
#undef DEBUG_RX
#undef DEBUG_RXALLOC
#undef DEBUG_REGISTERS
#undef DEBUG_RING
#undef DEBUG_IRQ_TASKLET
#undef DEBUG_TX_ALLOC
#undef DEBUG_TX_DESC

#define CONFIG_RTL8192_IO_MAP

#include <asm/uaccess.h>
#include "r8192U.h"
#include "r8180_93cx6.h"   /* Card EEPROM */
#include "r8192U_wx.h"

#include "r8192S_rtl8225.h"
#include "r8192S_hw.h"
#include "r8192S_phy.h"
#include "r8192S_phyreg.h"
#include "r8192S_Efuse.h"

#include "r819xU_cmdpkt.h"
#include "r8192U_dm.h"
//#include "r8192xU_phyreg.h"
#include <linux/usb.h>

#include "r8192U_pm.h"

#include "ieee80211/dot11d.h"



u32 rt_global_debug_component = \
//				COMP_TRACE	|
//    				COMP_DBG	|
//				COMP_INIT    	|
//				COMP_RECV	|
//				COMP_SEND	|
//				COMP_IO		|
				COMP_POWER	|
//				COMP_EPROM   	|
				COMP_SWBW	|
				COMP_POWER_TRACKING |
				COMP_TURBO	|
				COMP_QOS	|
//				COMP_RATE	|
//				COMP_RM		|
				COMP_DIG	|
//				COMP_EFUSE	|
//				COMP_CH		|
//				COMP_TXAGC	|
                              	COMP_HIPWR	|
//                             	COMP_HALDM	|
				COMP_SEC	|
				COMP_LED	|
//				COMP_RF		|
//				COMP_RXDESC	|
				COMP_FIRMWARE	|
				COMP_HT		|
				COMP_AMSDU	|
				COMP_SCAN	|
//				COMP_CMD	|
				COMP_DOWN	|
				COMP_RESET	|
				COMP_ERR; //always open err flags on

#define TOTAL_CAM_ENTRY 32
#define CAM_CONTENT_COUNT 8

static struct usb_device_id rtl8192_usb_id_tbl[] = {
	{USB_DEVICE(0x0bda, 0x8171)}, /* Realtek */
	{USB_DEVICE(0x0bda, 0x8172)},
	{USB_DEVICE(0x0bda, 0x8173)},
	{USB_DEVICE(0x0bda, 0x8174)},
	{USB_DEVICE(0x0bda, 0x8712)},
	{USB_DEVICE(0x0bda, 0x8713)},
	{USB_DEVICE(0x07aa, 0x0047)},
	{USB_DEVICE(0x07d1, 0x3303)},
	{USB_DEVICE(0x07d1, 0x3302)},
	{USB_DEVICE(0x07d1, 0x3300)},
	{USB_DEVICE(0x1740, 0x9603)},
	{USB_DEVICE(0x1740, 0x9605)},
	{USB_DEVICE(0x050d, 0x815F)},
	{USB_DEVICE(0x06f8, 0xe031)},
	{USB_DEVICE(0x7392, 0x7611)},
	{USB_DEVICE(0x7392, 0x7612)},
	{USB_DEVICE(0x7392, 0x7622)},
	{USB_DEVICE(0x0DF6, 0x0045)},
	{USB_DEVICE(0x0E66, 0x0015)},
	{USB_DEVICE(0x0E66, 0x0016)},
	{USB_DEVICE(0x0b05, 0x1786)},
	/* these are not in the official list */
	{USB_DEVICE(0x050d, 0x815F)}, /* Belkin F5D8053 v6 */
	{USB_DEVICE(0x0df6, 0x004b)}, /* WL-349 */
	{}
};

MODULE_LICENSE("GPL");
MODULE_VERSION("V 1.1");
MODULE_DEVICE_TABLE(usb, rtl8192_usb_id_tbl);
MODULE_DESCRIPTION("Linux driver for Realtek RTL8192 USB WiFi cards");

static char* ifname = "wlan%d";
static int hwwep = 1;  //default use hw. set 0 to use software security
static int channels = 0x3fff;



module_param(ifname, charp, S_IRUGO|S_IWUSR );
//module_param(hwseqnum,int, S_IRUGO|S_IWUSR);
module_param(hwwep,int, S_IRUGO|S_IWUSR);
module_param(channels,int, S_IRUGO|S_IWUSR);

MODULE_PARM_DESC(ifname," Net interface name, wlan%d=default");
//MODULE_PARM_DESC(hwseqnum," Try to use hardware 802.11 header sequence numbers. Zero=default");
MODULE_PARM_DESC(hwwep," Try to use hardware security support. ");
MODULE_PARM_DESC(channels," Channel bitmask for specific locales. NYI");

static int __devinit rtl8192_usb_probe(struct usb_interface *intf,
			 const struct usb_device_id *id);
static void __devexit rtl8192_usb_disconnect(struct usb_interface *intf);
static const struct net_device_ops rtl8192_netdev_ops;
static struct notifier_block proc_netdev_notifier;

static struct usb_driver rtl8192_usb_driver = {
	.name		= RTL819xU_MODULE_NAME,	          /* Driver name   */
	.id_table	= rtl8192_usb_id_tbl,	          /* PCI_ID table  */
	.probe		= rtl8192_usb_probe,	          /* probe fn      */
	.disconnect	= rtl8192_usb_disconnect,	  /* remove fn     */
	.suspend	= rtl8192U_suspend,	          /* PM suspend fn */
	.resume		= rtl8192U_resume,                 /* PM resume fn  */
	.reset_resume   = rtl8192U_resume,                 /* PM reset resume fn  */
};


static void 	rtl8192SU_read_eeprom_info(struct net_device *dev);
short 	rtl8192SU_tx(struct net_device *dev, struct sk_buff* skb);
void 	rtl8192SU_rx_nomal(struct sk_buff* skb);
void 	rtl8192SU_rx_cmd(struct sk_buff *skb);
bool 	rtl8192SU_adapter_start(struct net_device *dev);
short	rtl8192SU_tx_cmd(struct net_device *dev, struct sk_buff *skb);
void 	rtl8192SU_link_change(struct net_device *dev);
void 	InitialGain8192S(struct net_device *dev,u8 Operation);
void 	rtl8192SU_query_rxdesc_status(struct sk_buff *skb, struct ieee80211_rx_stats *stats, bool bIsRxAggrSubframe);

struct rtl819x_ops rtl8192su_ops = {
	.nic_type = NIC_8192SU,
	.rtl819x_read_eeprom_info = rtl8192SU_read_eeprom_info,
	.rtl819x_tx = rtl8192SU_tx,
	.rtl819x_tx_cmd = rtl8192SU_tx_cmd,
	.rtl819x_rx_nomal = rtl8192SU_rx_nomal,
	.rtl819x_rx_cmd = rtl8192SU_rx_cmd,
	.rtl819x_adapter_start = rtl8192SU_adapter_start,
	.rtl819x_link_change = rtl8192SU_link_change,
	.rtl819x_initial_gain = InitialGain8192S,
	.rtl819x_query_rxdesc_status = rtl8192SU_query_rxdesc_status,
};


typedef struct _CHANNEL_LIST
{
	u8	Channel[32];
	u8	Len;
}CHANNEL_LIST, *PCHANNEL_LIST;

static CHANNEL_LIST ChannelPlan[] = {
	{{1,2,3,4,5,6,7,8,9,10,11,36,40,44,48,52,56,60,64,149,153,157,161,165},24},  		//FCC
	{{1,2,3,4,5,6,7,8,9,10,11},11},                    				//IC
	{{1,2,3,4,5,6,7,8,9,10,11,12,13,36,40,44,48,52,56,60,64},21},  	//ETSI
	{{1,2,3,4,5,6,7,8,9,10,11,12,13},13},    //Spain. Change to ETSI.
	{{1,2,3,4,5,6,7,8,9,10,11,12,13},13},  	//France. Change to ETSI.
	{{1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,40,44,48,52,56,60,64},22},	//MKK					//MKK
	{{1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,40,44,48,52,56,60,64},22},//MKK1
	{{1,2,3,4,5,6,7,8,9,10,11,12,13},13},	//Israel.
	{{1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,40,44,48,52,56,60,64},22},			// For 11a , TELEC
	{{1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,40,44,48,52,56,60,64}, 22},    //MIC
	{{1,2,3,4,5,6,7,8,9,10,11,12,13,14},14}					//For Global Domain. 1-11:active scan, 12-14 passive scan. //+YJ, 080626
};

static void rtl819x_set_channel_map(u8 channel_plan, struct r8192_priv* priv)
{
	int i, max_chan=-1, min_chan=-1;
	struct ieee80211_device* ieee = priv->ieee80211;
	switch (channel_plan)
	{
		case COUNTRY_CODE_FCC:
		case COUNTRY_CODE_IC:
		case COUNTRY_CODE_ETSI:
		case COUNTRY_CODE_SPAIN:
		case COUNTRY_CODE_FRANCE:
		case COUNTRY_CODE_MKK:
		case COUNTRY_CODE_MKK1:
		case COUNTRY_CODE_ISRAEL:
		case COUNTRY_CODE_TELEC:
		case COUNTRY_CODE_MIC:
		{
			Dot11d_Init(ieee);
			ieee->bGlobalDomain = false;
			//acturally 8225 & 8256 rf chip only support B,G,24N mode
                        if ((priv->rf_chip == RF_8225) || (priv->rf_chip == RF_8256) || (priv->rf_chip == RF_6052))
			{
				min_chan = 1;
				max_chan = 14;
			}
			else
			{
				RT_TRACE(COMP_ERR, "unknown rf chip, can't set channel map in function:%s()\n", __FUNCTION__);
			}
			if (ChannelPlan[channel_plan].Len != 0){
				// Clear old channel map
				memset(GET_DOT11D_INFO(ieee)->channel_map, 0, sizeof(GET_DOT11D_INFO(ieee)->channel_map));
				// Set new channel map
				for (i=0;i<ChannelPlan[channel_plan].Len;i++)
				{
					if (ChannelPlan[channel_plan].Channel[i] < min_chan || ChannelPlan[channel_plan].Channel[i] > max_chan)
					break;
					GET_DOT11D_INFO(ieee)->channel_map[ChannelPlan[channel_plan].Channel[i]] = 1;
				}
			}
			break;
		}
		case COUNTRY_CODE_GLOBAL_DOMAIN:
		{
			GET_DOT11D_INFO(ieee)->bEnabled = 0;//this flag enabled to follow 11d country IE setting, otherwise, it shall follow global domain settings.
			Dot11d_Reset(ieee);
			ieee->bGlobalDomain = true;
			break;
		}
		default:
			break;
	}
	return;
}

#define eqMacAddr(a,b) ( ((a)[0]==(b)[0] && (a)[1]==(b)[1] && (a)[2]==(b)[2] && (a)[3]==(b)[3] && (a)[4]==(b)[4] && (a)[5]==(b)[5]) ? 1:0 )

#define		rx_hal_is_cck_rate(_pDesc)\
			((_pDesc->RxMCS  == DESC92S_RATE1M ||\
			_pDesc->RxMCS == DESC92S_RATE2M ||\
			_pDesc->RxMCS == DESC92S_RATE5_5M ||\
			_pDesc->RxMCS == DESC92S_RATE11M) &&\
			!_pDesc->RxHT)

#define 	tx_hal_is_cck_rate(_DataRate)\
			( _DataRate == MGN_1M ||\
			 _DataRate == MGN_2M ||\
			 _DataRate == MGN_5_5M ||\
			 _DataRate == MGN_11M )




void CamResetAllEntry(struct net_device *dev)
{
#if 1
	u32 ulcommand = 0;
        //2004/02/11  In static WEP, OID_ADD_KEY or OID_ADD_WEP are set before STA associate to AP.
        // However, ResetKey is called on OID_802_11_INFRASTRUCTURE_MODE and MlmeAssociateRequest
        // In this condition, Cam can not be reset because upper layer will not set this static key again.
        //if(Adapter->EncAlgorithm == WEP_Encryption)
        //      return;
//debug
        //DbgPrint("========================================\n");
        //DbgPrint("                            Call ResetAllEntry                                              \n");
        //DbgPrint("========================================\n\n");
	ulcommand |= BIT31|BIT30;
	write_nic_dword(dev, RWCAM, ulcommand);
#else
        for(ucIndex=0;ucIndex<TOTAL_CAM_ENTRY;ucIndex++)
                CAM_mark_invalid(dev, ucIndex);
        for(ucIndex=0;ucIndex<TOTAL_CAM_ENTRY;ucIndex++)
                CAM_empty_entry(dev, ucIndex);
#endif

}


void write_cam(struct net_device *dev, u8 addr, u32 data)
{
        write_nic_dword(dev, WCAMI, data);
        write_nic_dword(dev, RWCAM, BIT31|BIT16|(addr&0xff) );
}

u32 read_cam(struct net_device *dev, u8 addr)
{
        write_nic_dword(dev, RWCAM, 0x80000000|(addr&0xff) );
        return read_nic_dword(dev, 0xa8);
}

void write_nic_byte_E(struct net_device *dev, int indx, u8 data)
{
	int status;
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
	struct usb_device *udev = priv->udev;

	status = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			       RTL8187_REQ_SET_REGS, RTL8187_REQT_WRITE,
			       indx|0xfe00, 0, &data, 1, HZ / 2);

	if (status < 0)
	{
		printk("write_nic_byte_E TimeOut! status:%d\n", status);
	}
}

u8 read_nic_byte_E(struct net_device *dev, int indx)
{
	int status;
	u8 data;
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
	struct usb_device *udev = priv->udev;

	status = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			       RTL8187_REQ_GET_REGS, RTL8187_REQT_READ,
			       indx|0xfe00, 0, &data, 1, HZ / 2);

        if (status < 0)
        {
                printk("read_nic_byte_E TimeOut! status:%d\n", status);
        }

	return data;
}
//as 92U has extend page from 4 to 16, so modify functions below.
void write_nic_byte(struct net_device *dev, int indx, u8 data)
{
	int status;

	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
	struct usb_device *udev = priv->udev;

	status = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			       RTL8187_REQ_SET_REGS, RTL8187_REQT_WRITE,
			       indx, 0, &data, 1, HZ / 2);

        if (status < 0)
        {
                printk("write_nic_byte TimeOut! status:%d\n", status);
        }


}


void write_nic_word(struct net_device *dev, int indx, u16 data)
{

	int status;

	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
	struct usb_device *udev = priv->udev;

	status = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			       RTL8187_REQ_SET_REGS, RTL8187_REQT_WRITE,
			       indx, 0, &data, 2, HZ / 2);

        if (status < 0)
        {
                printk("write_nic_word TimeOut! status:%d\n", status);
        }

}


void write_nic_dword(struct net_device *dev, int indx, u32 data)
{

	int status;

	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
	struct usb_device *udev = priv->udev;

	status = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			       RTL8187_REQ_SET_REGS, RTL8187_REQT_WRITE,
			       indx, 0, &data, 4, HZ / 2);


        if (status < 0)
        {
                printk("write_nic_dword TimeOut! status:%d\n", status);
        }

}



u8 read_nic_byte(struct net_device *dev, int indx)
{
	u8 data;
	int status;
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
	struct usb_device *udev = priv->udev;

	status = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			       RTL8187_REQ_GET_REGS, RTL8187_REQT_READ,
			       indx, 0, &data, 1, HZ / 2);

        if (status < 0)
        {
                printk("read_nic_byte TimeOut! status:%d\n", status);
        }

	return data;
}



u16 read_nic_word(struct net_device *dev, int indx)
{
	u16 data;
	int status;
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
	struct usb_device *udev = priv->udev;

	status = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			       RTL8187_REQ_GET_REGS, RTL8187_REQT_READ,
			       indx, 0, &data, 2, HZ / 2);

        if (status < 0)
        {
                printk("read_nic_word TimeOut! status:%d\n", status);
        }


	return data;
}

u16 read_nic_word_E(struct net_device *dev, int indx)
{
	u16 data;
	int status;
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
	struct usb_device *udev = priv->udev;

	status = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			       RTL8187_REQ_GET_REGS, RTL8187_REQT_READ,
			       indx|0xfe00, 0, &data, 2, HZ / 2);

        if (status < 0)
        {
                printk("read_nic_word TimeOut! status:%d\n", status);
        }


	return data;
}

u32 read_nic_dword(struct net_device *dev, int indx)
{
	u32 data;
	int status;
//	int result;

	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
	struct usb_device *udev = priv->udev;

	status = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			       RTL8187_REQ_GET_REGS, RTL8187_REQT_READ,
			       indx, 0, &data, 4, HZ / 2);
//	if(0 != result) {
//	  printk(KERN_WARNING "read size of data = %d\, date = %d\n", result, data);
//	}

        if (status < 0)
        {
                printk("read_nic_dword TimeOut! status:%d\n", status);
		if(status == -ENODEV) {
			priv->usb_error = true;
		}
        }



	return data;
}


//u8 read_phy_cck(struct net_device *dev, u8 adr);
//u8 read_phy_ofdm(struct net_device *dev, u8 adr);
/* this might still called in what was the PHY rtl8185/rtl8192 common code
 * plans are to possibilty turn it again in one common code...
 */
inline void force_pci_posting(struct net_device *dev)
{
}


static struct net_device_stats *rtl8192_stats(struct net_device *dev);
void rtl8192_commit(struct net_device *dev);
//void rtl8192_restart(struct net_device *dev);
void rtl8192_restart(struct work_struct *work);
//void rtl8192_rq_tx_ack(struct work_struct *work);

void watch_dog_timer_callback(unsigned long data);

/****************************************************************************
   -----------------------------PROCFS STUFF-------------------------
*****************************************************************************/

static struct proc_dir_entry *rtl8192_proc = NULL;



static int proc_get_stats_ap(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
	struct ieee80211_device *ieee = priv->ieee80211;
	struct ieee80211_network *target;

	int len = 0;

        list_for_each_entry(target, &ieee->network_list, list) {

		len += snprintf(page + len, count - len,
                "%s ", target->ssid);

		if(target->wpa_ie_len>0 || target->rsn_ie_len>0){
	                len += snprintf(page + len, count - len,
        	        "WPA\n");
		}
		else{
                        len += snprintf(page + len, count - len,
                        "non_WPA\n");
                }

        }

	*eof = 1;
	return len;
}

static int proc_get_registers(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;
//	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);

	int len = 0;
	int i,n,page0,page1,page2;

	int max=0xff;
	page0 = 0x000;
	page1 = 0x100;
	page2 = 0x800;

	/* This dump the current register page */
	if(!IS_BB_REG_OFFSET_92S(page0)){
		len += snprintf(page + len, count - len,
				"\n####################page %x##################\n ", (page0>>8));
		for(n=0;n<=max;)
		{
			len += snprintf(page + len, count - len,
					"\nD:  %2x > ",n);
			for(i=0;i<16 && n<=max;i++,n++)
				len += snprintf(page + len, count - len,
						"%2.2x ",read_nic_byte(dev,(page0|n)));
		}
	}else{
		len += snprintf(page + len, count - len,
				"\n####################page %x##################\n ", (page0>>8));
		for(n=0;n<=max;)
		{
			len += snprintf(page + len, count - len, "\nD:  %2x > ",n);
			for(i=0;i<4 && n<=max;n+=4,i++)
				len += snprintf(page + len, count - len,
						"%8.8x ",rtl8192_QueryBBReg(dev,(page0|n), bMaskDWord));
		}
	}
	len += snprintf(page + len, count - len,"\n");
	*eof = 1;
	return len;

}
static int proc_get_registers_1(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;
//	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);

	int len = 0;
	int i,n,page0;

	int max=0xff;
	page0 = 0x100;

	/* This dump the current register page */
		len += snprintf(page + len, count - len,
				"\n####################page %x##################\n ", (page0>>8));
		for(n=0;n<=max;)
		{
			len += snprintf(page + len, count - len,
					"\nD:  %2x > ",n);
			for(i=0;i<16 && n<=max;i++,n++)
				len += snprintf(page + len, count - len,
						"%2.2x ",read_nic_byte(dev,(page0|n)));
		}
	len += snprintf(page + len, count - len,"\n");
	*eof = 1;
	return len;

}
static int proc_get_registers_2(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;
//	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);

	int len = 0;
	int i,n,page0;

	int max=0xff;
	page0 = 0x200;

	/* This dump the current register page */
		len += snprintf(page + len, count - len,
				"\n####################page %x##################\n ", (page0>>8));
		for(n=0;n<=max;)
		{
			len += snprintf(page + len, count - len,
					"\nD:  %2x > ",n);
			for(i=0;i<16 && n<=max;i++,n++)
				len += snprintf(page + len, count - len,
						"%2.2x ",read_nic_byte(dev,(page0|n)));
		}
	len += snprintf(page + len, count - len,"\n");
	*eof = 1;
	return len;

}
static int proc_get_registers_8(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;

	int len = 0;
	int i,n,page0;

	int max=0xff;
	page0 = 0x800;

	/* This dump the current register page */
		len += snprintf(page + len, count - len,
				"\n####################page %x##################\n ", (page0>>8));
		for(n=0;n<=max;)
		{
			len += snprintf(page + len, count - len, "\nD:  %2x > ",n);
			for(i=0;i<4 && n<=max;n+=4,i++)
				len += snprintf(page + len, count - len,
						"%8.8x ",rtl8192_QueryBBReg(dev,(page0|n), bMaskDWord));
		}
	len += snprintf(page + len, count - len,"\n");
	*eof = 1;
	return len;

	}
static int proc_get_registers_9(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;
//	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);

	int len = 0;
	int i,n,page0;

	int max=0xff;
	page0 = 0x900;

	/* This dump the current register page */
		len += snprintf(page + len, count - len,
				"\n####################page %x##################\n ", (page0>>8));
		for(n=0;n<=max;)
		{
			len += snprintf(page + len, count - len, "\nD:  %2x > ",n);
			for(i=0;i<4 && n<=max;n+=4,i++)
			len += snprintf(page + len, count - len,
						"%8.8x ",rtl8192_QueryBBReg(dev,(page0|n), bMaskDWord));
		}
	len += snprintf(page + len, count - len,"\n");
	*eof = 1;
	return len;
}
static int proc_get_registers_a(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;
//	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);

	int len = 0;
	int i,n,page0;

	int max=0xff;
	page0 = 0xa00;

	/* This dump the current register page */
				len += snprintf(page + len, count - len,
				"\n####################page %x##################\n ", (page0>>8));
		for(n=0;n<=max;)
		{
			len += snprintf(page + len, count - len, "\nD:  %2x > ",n);
			for(i=0;i<4 && n<=max;n+=4,i++)
				len += snprintf(page + len, count - len,
						"%8.8x ",rtl8192_QueryBBReg(dev,(page0|n), bMaskDWord));
		}
	len += snprintf(page + len, count - len,"\n");
	*eof = 1;
	return len;
}
static int proc_get_registers_b(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;
//	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);

	int len = 0;
	int i,n,page0;

	int max=0xff;
	page0 = 0xb00;

	/* This dump the current register page */
		len += snprintf(page + len, count - len,
				"\n####################page %x##################\n ", (page0>>8));
		for(n=0;n<=max;)
		{
			len += snprintf(page + len, count - len, "\nD:  %2x > ",n);
			for(i=0;i<4 && n<=max;n+=4,i++)
				len += snprintf(page + len, count - len,
						"%8.8x ",rtl8192_QueryBBReg(dev,(page0|n), bMaskDWord));
		}
	len += snprintf(page + len, count - len,"\n");
	*eof = 1;
	return len;
	}
static int proc_get_registers_c(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;
//	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);

	int len = 0;
	int i,n,page0;

	int max=0xff;
	page0 = 0xc00;

	/* This dump the current register page */
		len += snprintf(page + len, count - len,
				"\n####################page %x##################\n ", (page0>>8));
		for(n=0;n<=max;)
		{
			len += snprintf(page + len, count - len, "\nD:  %2x > ",n);
			for(i=0;i<4 && n<=max;n+=4,i++)
				len += snprintf(page + len, count - len,
						"%8.8x ",rtl8192_QueryBBReg(dev,(page0|n), bMaskDWord));
		}
	len += snprintf(page + len, count - len,"\n");
	*eof = 1;
	return len;
}
static int proc_get_registers_d(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;
//	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);

	int len = 0;
	int i,n,page0;

	int max=0xff;
	page0 = 0xd00;

	/* This dump the current register page */
		len += snprintf(page + len, count - len,
				"\n####################page %x##################\n ", (page0>>8));
		for(n=0;n<=max;)
		{
			len += snprintf(page + len, count - len, "\nD:  %2x > ",n);
			for(i=0;i<4 && n<=max;n+=4,i++)
				len += snprintf(page + len, count - len,
						"%8.8x ",rtl8192_QueryBBReg(dev,(page0|n), bMaskDWord));
		}
	len += snprintf(page + len, count - len,"\n");
	*eof = 1;
	return len;
}
static int proc_get_registers_e(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;
//	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);

	int len = 0;
	int i,n,page0;

	int max=0xff;
	page0 = 0xe00;

	/* This dump the current register page */
		len += snprintf(page + len, count - len,
				"\n####################page %x##################\n ", (page0>>8));
		for(n=0;n<=max;)
		{
			len += snprintf(page + len, count - len, "\nD:  %2x > ",n);
			for(i=0;i<4 && n<=max;n+=4,i++)
				len += snprintf(page + len, count - len,
						"%8.8x ",rtl8192_QueryBBReg(dev,(page0|n), bMaskDWord));
		}
	len += snprintf(page + len, count - len,"\n");
	*eof = 1;
	return len;
}

static int proc_get_stats_tx(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);

	int len = 0;

	len += snprintf(page + len, count - len,
		"TX VI priority ok int: %lu\n"
		"TX VI priority error int: %lu\n"
		"TX VO priority ok int: %lu\n"
		"TX VO priority error int: %lu\n"
		"TX BE priority ok int: %lu\n"
		"TX BE priority error int: %lu\n"
		"TX BK priority ok int: %lu\n"
		"TX BK priority error int: %lu\n"
		"TX MANAGE priority ok int: %lu\n"
		"TX MANAGE priority error int: %lu\n"
		"TX BEACON priority ok int: %lu\n"
		"TX BEACON priority error int: %lu\n"
//		"TX high priority ok int: %lu\n"
//		"TX high priority failed error int: %lu\n"
		"TX queue resume: %lu\n"
		"TX queue stopped?: %d\n"
		"TX fifo overflow: %lu\n"
//		"TX beacon: %lu\n"
		"TX VI queue: %d\n"
		"TX VO queue: %d\n"
		"TX BE queue: %d\n"
		"TX BK queue: %d\n"
//		"TX HW queue: %d\n"
		"TX VI dropped: %lu\n"
		"TX VO dropped: %lu\n"
		"TX BE dropped: %lu\n"
		"TX BK dropped: %lu\n"
		"TX total data packets %lu\n",
//		"TX beacon aborted: %lu\n",
		priv->stats.txviokint,
		priv->stats.txvierr,
		priv->stats.txvookint,
		priv->stats.txvoerr,
		priv->stats.txbeokint,
		priv->stats.txbeerr,
		priv->stats.txbkokint,
		priv->stats.txbkerr,
		priv->stats.txmanageokint,
		priv->stats.txmanageerr,
		priv->stats.txbeaconokint,
		priv->stats.txbeaconerr,
//		priv->stats.txhpokint,
//		priv->stats.txhperr,
		priv->stats.txresumed,
		netif_queue_stopped(dev),
		priv->stats.txoverflow,
//		priv->stats.txbeacon,
		atomic_read(&(priv->tx_pending[VI_PRIORITY])),
		atomic_read(&(priv->tx_pending[VO_PRIORITY])),
		atomic_read(&(priv->tx_pending[BE_PRIORITY])),
		atomic_read(&(priv->tx_pending[BK_PRIORITY])),
//		read_nic_byte(dev, TXFIFOCOUNT),
		priv->stats.txvidrop,
		priv->stats.txvodrop,
		priv->stats.txbedrop,
		priv->stats.txbkdrop,
		priv->stats.txdatapkt
//		priv->stats.txbeaconerr
		);

	*eof = 1;
	return len;
}



static int proc_get_stats_rx(char *page, char **start,
			  off_t offset, int count,
			  int *eof, void *data)
{
	struct net_device *dev = data;
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);

	int len = 0;

	len += snprintf(page + len, count - len,
		"RX packets: %lu\n"
		"RX urb status error: %lu\n"
		"RX invalid urb error: %lu\n",
		priv->stats.rxoktotal,
		priv->stats.rxstaterr,
		priv->stats.rxurberr);

	*eof = 1;
	return len;
}

int rtl8192_proc_module_init(void)
{
	int ret;

	RT_TRACE(COMP_INIT, "Initializing proc filesystem");
	rtl8192_proc=create_proc_entry(RTL819xU_MODULE_NAME, S_IFDIR, init_net.proc_net);
	if (!rtl8192_proc)
		return -ENOMEM;
	ret = register_netdevice_notifier(&proc_netdev_notifier);
	if (ret)
		remove_proc_entry(RTL819xU_MODULE_NAME, init_net.proc_net);
	return ret;
}


void rtl8192_proc_module_remove(void)
{
	unregister_netdevice_notifier(&proc_netdev_notifier);
	remove_proc_entry(RTL819xU_MODULE_NAME, init_net.proc_net);
}


void rtl8192_proc_remove_one(struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);


	if (priv->dir_dev) {
	//	remove_proc_entry("stats-hw", priv->dir_dev);
		remove_proc_entry("stats-tx", priv->dir_dev);
		remove_proc_entry("stats-rx", priv->dir_dev);
	//	remove_proc_entry("stats-ieee", priv->dir_dev);
		remove_proc_entry("stats-ap", priv->dir_dev);
		remove_proc_entry("registers", priv->dir_dev);
		remove_proc_entry("registers-1", priv->dir_dev);
		remove_proc_entry("registers-2", priv->dir_dev);
		remove_proc_entry("registers-8", priv->dir_dev);
		remove_proc_entry("registers-9", priv->dir_dev);
		remove_proc_entry("registers-a", priv->dir_dev);
		remove_proc_entry("registers-b", priv->dir_dev);
		remove_proc_entry("registers-c", priv->dir_dev);
		remove_proc_entry("registers-d", priv->dir_dev);
		remove_proc_entry("registers-e", priv->dir_dev);
	//	remove_proc_entry("cck-registers",priv->dir_dev);
	//	remove_proc_entry("ofdm-registers",priv->dir_dev);
		remove_proc_entry(priv->dir_dev->name, rtl8192_proc);
		priv->dir_dev = NULL;
	}
}


void rtl8192_proc_init_one(struct net_device *dev)
{
	struct proc_dir_entry *e;
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
	priv->dir_dev = create_proc_entry(dev->name,
					  S_IFDIR | S_IRUGO | S_IXUGO,
					  rtl8192_proc);
	if (!priv->dir_dev) {
		RT_TRACE(COMP_ERR, "Unable to initialize /proc/net/rtl8192/%s\n",
		      dev->name);
		return;
	}
	e = create_proc_read_entry("stats-rx", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_stats_rx, dev);

	if (!e) {
		RT_TRACE(COMP_ERR,"Unable to initialize "
		      "/proc/net/rtl8192/%s/stats-rx\n",
		      dev->name);
	}


	e = create_proc_read_entry("stats-tx", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_stats_tx, dev);

	if (!e) {
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/stats-tx\n",
		      dev->name);
	}

	e = create_proc_read_entry("stats-ap", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_stats_ap, dev);

	if (!e) {
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/stats-ap\n",
		      dev->name);
	}

	e = create_proc_read_entry("registers", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers, dev);
	if (!e) {
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers\n",
		      dev->name);
	}
	e = create_proc_read_entry("registers-1", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_1, dev);
	if (!e) {
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-1\n",
		      dev->name);
	}
	e = create_proc_read_entry("registers-2", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_2, dev);
	if (!e) {
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-2\n",
		      dev->name);
	}
	e = create_proc_read_entry("registers-8", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_8, dev);
	if (!e) {
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-8\n",
		      dev->name);
	}
	e = create_proc_read_entry("registers-9", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_9, dev);
	if (!e) {
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-9\n",
		      dev->name);
	}
	e = create_proc_read_entry("registers-a", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_a, dev);
	if (!e) {
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-a\n",
		      dev->name);
	}
	e = create_proc_read_entry("registers-b", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_b, dev);
	if (!e) {
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-b\n",
		      dev->name);
	}
	e = create_proc_read_entry("registers-c", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_c, dev);
	if (!e) {
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-c\n",
		      dev->name);
	}
	e = create_proc_read_entry("registers-d", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_d, dev);
	if (!e) {
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-d\n",
		      dev->name);
	}
	e = create_proc_read_entry("registers-e", S_IFREG | S_IRUGO,
				   priv->dir_dev, proc_get_registers_e, dev);
	if (!e) {
		RT_TRACE(COMP_ERR, "Unable to initialize "
		      "/proc/net/rtl8192/%s/registers-e\n",
		      dev->name);
	}
}

static int proc_netdev_event(struct notifier_block *this,
			     unsigned long event, void *ptr)
{
	struct net_device *net_dev = ptr;

	if (net_dev->netdev_ops == &rtl8192_netdev_ops &&
	    event == NETDEV_CHANGENAME) {
		rtl8192_proc_remove_one(net_dev);
		rtl8192_proc_init_one(net_dev);
	}

	return NOTIFY_DONE;
}

static struct notifier_block proc_netdev_notifier = {
	.notifier_call = proc_netdev_event,
};

/****************************************************************************
   -----------------------------MISC STUFF-------------------------
*****************************************************************************/

/* this is only for debugging */
void print_buffer(u32 *buffer, int len)
{
	int i;
	u8 *buf =(u8*)buffer;

	printk("ASCII BUFFER DUMP (len: %x):\n",len);

	for(i=0;i<len;i++)
		printk("%c",buf[i]);

	printk("\nBINARY BUFFER DUMP (len: %x):\n",len);

	for(i=0;i<len;i++)
		printk("%x",buf[i]);

	printk("\n");
}

//short check_nic_enough_desc(struct net_device *dev, priority_t priority)
short check_nic_enough_desc(struct net_device *dev,int queue_index)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	int used = atomic_read(&priv->tx_pending[queue_index]);

	return (used < MAX_TX_URB);
}

void tx_timeout(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	//rtl8192_commit(dev);

	schedule_work(&priv->reset_wq);
	//DMESG("TXTIMEOUT");
}


/* this is only for debug */
void dump_eprom(struct net_device *dev)
{
	int i;
	for(i=0; i<63; i++)
		RT_TRACE(COMP_EPROM, "EEPROM addr %x : %x", i, eprom_read(dev,i));
}

/* this is only for debug */
void rtl8192_dump_reg(struct net_device *dev)
{
	int i;
	int n;
	int max=0x1ff;

	RT_TRACE(COMP_PHY, "Dumping NIC register map");

	for(n=0;n<=max;)
	{
		printk( "\nD: %2x> ", n);
		for(i=0;i<16 && n<=max;i++,n++)
			printk("%2x ",read_nic_byte(dev,n));
	}
	printk("\n");
}

/****************************************************************************
      ------------------------------HW STUFF---------------------------
*****************************************************************************/

void rtl8192_set_mode(struct net_device *dev,int mode)
{
	u8 ecmd;
	ecmd=read_nic_byte(dev, EPROM_CMD);
	ecmd=ecmd &~ EPROM_CMD_OPERATING_MODE_MASK;
	ecmd=ecmd | (mode<<EPROM_CMD_OPERATING_MODE_SHIFT);
	ecmd=ecmd &~ (1<<EPROM_CS_SHIFT);
	ecmd=ecmd &~ (1<<EPROM_CK_SHIFT);
	write_nic_byte(dev, EPROM_CMD, ecmd);
}


void rtl8192_update_msr(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8 msr;

	msr  = read_nic_byte(dev, MSR);
	msr &= ~ MSR_LINK_MASK;

	/* do not change in link_state != WLAN_LINK_ASSOCIATED.
	 * msr must be updated if the state is ASSOCIATING.
	 * this is intentional and make sense for ad-hoc and
	 * master (see the create BSS/IBSS func)
	 */
	if (priv->ieee80211->state == IEEE80211_LINKED){

		if (priv->ieee80211->iw_mode == IW_MODE_INFRA)
			msr |= (MSR_LINK_MANAGED<<MSR_LINK_SHIFT);
		else if (priv->ieee80211->iw_mode == IW_MODE_ADHOC)
			msr |= (MSR_LINK_ADHOC<<MSR_LINK_SHIFT);
		else if (priv->ieee80211->iw_mode == IW_MODE_MASTER)
			msr |= (MSR_LINK_MASTER<<MSR_LINK_SHIFT);

	}else
		msr |= (MSR_LINK_NONE<<MSR_LINK_SHIFT);

	write_nic_byte(dev, MSR, msr);
}

void rtl8192_set_chan(struct net_device *dev,short ch)
{
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
//	u32 tx;
	RT_TRACE(COMP_CH, "=====>%s()====ch:%d\n", __FUNCTION__, ch);
	//printk("=====>%s()====ch:%d\n", __FUNCTION__, ch);
	priv->chan=ch;

	/* this hack should avoid frame TX during channel setting*/


//	tx = read_nic_dword(dev,TX_CONF);
//	tx &= ~TX_LOOPBACK_MASK;

#ifndef LOOP_TEST
//	write_nic_dword(dev,TX_CONF, tx |( TX_LOOPBACK_MAC<<TX_LOOPBACK_SHIFT));

	//need to implement rf set channel here WB

	if (priv->rf_set_chan)
	priv->rf_set_chan(dev,priv->chan);
	mdelay(10);
//	write_nic_dword(dev,TX_CONF,tx | (TX_LOOPBACK_NONE<<TX_LOOPBACK_SHIFT));
#endif
}

static void rtl8192_rx_isr(struct urb *urb);

u32 get_rxpacket_shiftbytes_819xusb(struct ieee80211_rx_stats *pstats)
{

		return (sizeof(rx_desc_819x_usb) + pstats->RxDrvInfoSize
				+ pstats->RxBufShift);

}
static int rtl8192_rx_initiate(struct net_device*dev)
{
        struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
        struct urb *entry;
        struct sk_buff *skb;
        struct rtl8192_rx_info *info;

	/* nomal packet rx procedure */
        while (skb_queue_len(&priv->rx_queue) < MAX_RX_URB) {
                skb = __dev_alloc_skb(RX_URB_SIZE, GFP_KERNEL);
                if (!skb)
                        break;
	        entry = usb_alloc_urb(0, GFP_KERNEL);
                if (!entry) {
                        kfree_skb(skb);
                        break;
                }
//		printk("nomal packet IN request!\n");
                usb_fill_bulk_urb(entry, priv->udev,
                                  usb_rcvbulkpipe(priv->udev, 3), skb_tail_pointer(skb),
                                  RX_URB_SIZE, rtl8192_rx_isr, skb);
                info = (struct rtl8192_rx_info *) skb->cb;
                info->urb = entry;
                info->dev = dev;
		info->out_pipe = 3; //denote rx normal packet queue
                skb_queue_tail(&priv->rx_queue, skb);
                usb_submit_urb(entry, GFP_KERNEL);
        }

	/* command packet rx procedure */
        while (skb_queue_len(&priv->rx_queue) < MAX_RX_URB + 3) {
//		printk("command packet IN request!\n");
                skb = __dev_alloc_skb(RX_URB_SIZE ,GFP_KERNEL);
                if (!skb)
                        break;
                entry = usb_alloc_urb(0, GFP_KERNEL);
                if (!entry) {
                        kfree_skb(skb);
                        break;
                }
                usb_fill_bulk_urb(entry, priv->udev,
                                  usb_rcvbulkpipe(priv->udev, 9), skb_tail_pointer(skb),
                                  RX_URB_SIZE, rtl8192_rx_isr, skb);
                info = (struct rtl8192_rx_info *) skb->cb;
                info->urb = entry;
                info->dev = dev;
		   info->out_pipe = 9; //denote rx cmd packet queue
                skb_queue_tail(&priv->rx_queue, skb);
		usb_submit_urb(entry, GFP_KERNEL);
        }

        return 0;
}

void rtl8192_set_rxconf(struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
	u32 rxconf;

	rxconf=read_nic_dword(dev,RCR);
	rxconf = rxconf &~ MAC_FILTER_MASK;
	rxconf = rxconf | RCR_AMF;
	rxconf = rxconf | RCR_ADF;
	rxconf = rxconf | RCR_AB;
	rxconf = rxconf | RCR_AM;
	//rxconf = rxconf | RCR_ACF;

	if (dev->flags & IFF_PROMISC) {DMESG ("NIC in promisc mode");}

	if(priv->ieee80211->iw_mode == IW_MODE_MONITOR || \
	   dev->flags & IFF_PROMISC){
		rxconf = rxconf | RCR_AAP;
	} /*else if(priv->ieee80211->iw_mode == IW_MODE_MASTER){
		rxconf = rxconf | (1<<ACCEPT_ALLMAC_FRAME_SHIFT);
		rxconf = rxconf | (1<<RX_CHECK_BSSID_SHIFT);
	}*/else{
		rxconf = rxconf | RCR_APM;
		rxconf = rxconf | RCR_CBSSID;
	}


	if(priv->ieee80211->iw_mode == IW_MODE_MONITOR){
		rxconf = rxconf | RCR_AICV;
		rxconf = rxconf | RCR_APWRMGT;
	}

	if( priv->crcmon == 1 && priv->ieee80211->iw_mode == IW_MODE_MONITOR)
		rxconf = rxconf | RCR_ACRC32;


	rxconf = rxconf &~ RX_FIFO_THRESHOLD_MASK;
	rxconf = rxconf | (RX_FIFO_THRESHOLD_NONE<<RX_FIFO_THRESHOLD_SHIFT);
	rxconf = rxconf &~ MAX_RX_DMA_MASK;
	rxconf = rxconf | ((u32)7<<RCR_MXDMA_OFFSET);

//	rxconf = rxconf | (1<<RX_AUTORESETPHY_SHIFT);
	rxconf = rxconf | RCR_ONLYERLPKT;

//	rxconf = rxconf &~ RCR_CS_MASK;
//	rxconf = rxconf | (1<<RCR_CS_SHIFT);

	write_nic_dword(dev, RCR, rxconf);

	#ifdef DEBUG_RX
	DMESG("rxconf: %x %x",rxconf ,read_nic_dword(dev,RCR));
	#endif
}
//wait to be removed
void rtl8192_rx_enable(struct net_device *dev)
{
	//u8 cmd;

	//struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);

	rtl8192_rx_initiate(dev);

//	rtl8192_set_rxconf(dev);
}


void rtl8192_tx_enable(struct net_device *dev)
{
}

void rtl8192_rtx_disable(struct net_device *dev)
{
	u8 cmd;
	struct r8192_priv *priv = ieee80211_priv(dev);
	struct sk_buff *skb;
	struct rtl8192_rx_info *info;

	cmd=read_nic_byte(dev,CMDR);
	write_nic_byte(dev, CMDR, cmd &~ \
		(CR_TE|CR_RE));
	force_pci_posting(dev);
	mdelay(10);

	while ((skb = __skb_dequeue(&priv->rx_queue))) {
		info = (struct rtl8192_rx_info *) skb->cb;
		if (!info->urb)
			continue;

		usb_kill_urb(info->urb);
		kfree_skb(skb);
	}

	if (skb_queue_len(&priv->skb_queue)) {
		printk(KERN_WARNING "skb_queue not empty\n");
	}

	skb_queue_purge(&priv->skb_queue);
	return;
}


int alloc_tx_beacon_desc_ring(struct net_device *dev, int count)
{
	return 0;
}

inline u16 ieeerate2rtlrate(int rate)
{
	switch(rate){
	case 10:
	return 0;
	case 20:
	return 1;
	case 55:
	return 2;
	case 110:
	return 3;
	case 60:
	return 4;
	case 90:
	return 5;
	case 120:
	return 6;
	case 180:
	return 7;
	case 240:
	return 8;
	case 360:
	return 9;
	case 480:
	return 10;
	case 540:
	return 11;
	default:
	return 3;

	}
}
static u16 rtl_rate[] = {10,20,55,110,60,90,120,180,240,360,480,540};
inline u16 rtl8192_rate2rate(short rate)
{
	if (rate >11) return 0;
	return rtl_rate[rate];
}

static void rtl8192_rx_isr(struct urb *urb)
{
        struct sk_buff *skb = (struct sk_buff *) urb->context;
        struct rtl8192_rx_info *info = (struct rtl8192_rx_info *)skb->cb;
        struct net_device *dev = info->dev;
	struct r8192_priv *priv = ieee80211_priv(dev);
	int out_pipe = info->out_pipe;
	int err;
	if(!priv->up)
		return;
        if (unlikely(urb->status)) {
                info->urb = NULL;
                priv->stats.rxstaterr++;
                priv->ieee80211->stats.rx_errors++;
                usb_free_urb(urb);
	//	printk("%s():rx status err\n",__FUNCTION__);
                return;
        }

        skb_unlink(skb, &priv->rx_queue);
        skb_put(skb, urb->actual_length);

	skb_queue_tail(&priv->skb_queue, skb);
	tasklet_schedule(&priv->irq_rx_tasklet);

        skb = dev_alloc_skb(RX_URB_SIZE);
        if (unlikely(!skb)) {
                usb_free_urb(urb);
		printk("%s():can,t alloc skb\n",__FUNCTION__);
                /* TODO check rx queue length and refill *somewhere* */
                return;
        }

	usb_fill_bulk_urb(urb, priv->udev,
			usb_rcvbulkpipe(priv->udev, out_pipe),
			skb_tail_pointer(skb),
			RX_URB_SIZE, rtl8192_rx_isr, skb);

        info = (struct rtl8192_rx_info *) skb->cb;
        info->urb = urb;
        info->dev = dev;
	info->out_pipe = out_pipe;

        urb->transfer_buffer = skb_tail_pointer(skb);
        urb->context = skb;
        skb_queue_tail(&priv->rx_queue, skb);
        err = usb_submit_urb(urb, GFP_ATOMIC);
	if(err && err != EPERM)
		printk("can not submit rxurb, err is %x,URB status is %x\n",err,urb->status);
}

u32
rtl819xusb_rx_command_packet(
	struct net_device *dev,
	struct ieee80211_rx_stats *pstats
	)
{
	u32	status;

	//RT_TRACE(COMP_RECV, DBG_TRACE, ("---> RxCommandPacketHandle819xUsb()\n"));

	status = cmpk_message_handle_rx(dev, pstats);
	if (status)
	{
		DMESG("rxcommandpackethandle819xusb: It is a command packet\n");
	}
	else
	{
		//RT_TRACE(COMP_RECV, DBG_TRACE, ("RxCommandPacketHandle819xUsb: It is not a command packet\n"));
	}

	//RT_TRACE(COMP_RECV, DBG_TRACE, ("<--- RxCommandPacketHandle819xUsb()\n"));
	return status;
}

void rtl8192_data_hard_stop(struct net_device *dev)
{
	//FIXME !!
}


void rtl8192_data_hard_resume(struct net_device *dev)
{
	// FIXME !!
}

/* this function TX data frames when the ieee80211 stack requires this.
 * It checks also if we need to stop the ieee tx queue, eventually do it
 */
void rtl8192_hard_data_xmit(struct sk_buff *skb, struct net_device *dev, int rate)
{
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
	int ret;
	unsigned long flags;
	cb_desc *tcb_desc = (cb_desc *)(skb->cb + MAX_DEV_ADDR_SIZE);
	u8 queue_index = tcb_desc->queue_index;

	/* shall not be referred by command packet */
	assert(queue_index != TXCMD_QUEUE);

	spin_lock_irqsave(&priv->tx_lock,flags);

        memcpy((unsigned char *)(skb->cb),&dev,sizeof(dev));
//	tcb_desc->RATRIndex = 7;
//	tcb_desc->bTxDisableRateFallBack = 1;
//	tcb_desc->bTxUseDriverAssingedRate = 1;
	tcb_desc->bTxEnableFwCalcDur = 1;
	skb_push(skb, priv->ieee80211->tx_headroom);
	ret = priv->ops->rtl819x_tx(dev, skb);

	//priv->ieee80211->stats.tx_bytes+=(skb->len - priv->ieee80211->tx_headroom);
	//priv->ieee80211->stats.tx_packets++;

	spin_unlock_irqrestore(&priv->tx_lock,flags);

//	return ret;
	return;
}

/* This is a rough attempt to TX a frame
 * This is called by the ieee 80211 stack to TX management frames.
 * If the ring is full packet are dropped (for data frame the queue
 * is stopped before this can happen).
 */
int rtl8192_hard_start_xmit(struct sk_buff *skb,struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
	int ret;
	unsigned long flags;
        cb_desc *tcb_desc = (cb_desc *)(skb->cb + MAX_DEV_ADDR_SIZE);
        u8 queue_index = tcb_desc->queue_index;


	spin_lock_irqsave(&priv->tx_lock,flags);

        memcpy((unsigned char *)(skb->cb),&dev,sizeof(dev));
	if(queue_index == TXCMD_QUEUE) {
		skb_push(skb, USB_HWDESC_HEADER_LEN);
		priv->ops->rtl819x_tx_cmd(dev, skb);
		ret = 1;
	        spin_unlock_irqrestore(&priv->tx_lock,flags);
		return ret;
	} else {
		skb_push(skb, priv->ieee80211->tx_headroom);
		ret = priv->ops->rtl819x_tx(dev, skb);
	}

	spin_unlock_irqrestore(&priv->tx_lock,flags);

	return ret;
}


void rtl8192_try_wake_queue(struct net_device *dev, int pri);


static void rtl8192_tx_isr(struct urb *tx_urb)
{
	struct sk_buff *skb = (struct sk_buff*)tx_urb->context;
	struct net_device *dev = NULL;
	struct r8192_priv *priv = NULL;
	cb_desc *tcb_desc = (cb_desc *)(skb->cb + MAX_DEV_ADDR_SIZE);
	u8  queue_index = tcb_desc->queue_index;
//	bool bToSend0Byte;
//	u16 BufLen = skb->len;

	memcpy(&dev,(struct net_device*)(skb->cb),sizeof(struct net_device*));
	priv = ieee80211_priv(dev);

	if(tcb_desc->queue_index != TXCMD_QUEUE) {
		if(tx_urb->status == 0) {
		//	dev->trans_start = jiffies;
			// As act as station mode, destion shall be  unicast address.
			//priv->ieee80211->stats.tx_bytes+=(skb->len - priv->ieee80211->tx_headroom);
			//priv->ieee80211->stats.tx_packets++;
			priv->stats.txoktotal++;
			priv->ieee80211->LinkDetectInfo.NumTxOkInPeriod++;
			priv->stats.txbytesunicast += (skb->len - priv->ieee80211->tx_headroom);
		} else {
			priv->ieee80211->stats.tx_errors++;
			//priv->stats.txmanageerr++;
			/* TODO */
		}
	}

	/* free skb and tx_urb */
	if(skb != NULL) {
		dev_kfree_skb_any(skb);
		usb_free_urb(tx_urb);
		atomic_dec(&priv->tx_pending[queue_index]);
	}

	{
		//
		// Handle HW Beacon:
		// We had transfer our beacon frame to host controler at this moment.
		//
		//
		// Caution:
		// Handling the wait queue of command packets.
		// For Tx command packets, we must not do TCB fragment because it is not handled right now.
		// We must cut the packets to match the size of TX_CMD_PKT before we send it.
		//
	if (queue_index == MGNT_QUEUE){
        if (priv->ieee80211->ack_tx_to_ieee){
            if (rtl8192_is_tx_queue_empty(dev)){
                priv->ieee80211->ack_tx_to_ieee = 0;
                ieee80211_ps_tx_ack(priv->ieee80211, 1);
            }
        }
    }
		/* Handle MPDU in wait queue. */
		if(queue_index != BEACON_QUEUE) {
			/* Don't send data frame during scanning.*/
			if((skb_queue_len(&priv->ieee80211->skb_waitQ[queue_index]) != 0)&&\
					(!(priv->ieee80211->queue_stop))) {
				if(NULL != (skb = skb_dequeue(&(priv->ieee80211->skb_waitQ[queue_index]))))
					priv->ieee80211->softmac_hard_start_xmit(skb, dev);

				return; //modified by david to avoid further processing AMSDU
			}
		}
	}
}

void rtl8192_beacon_stop(struct net_device *dev)
{
	u8 msr, msrm, msr2;
	struct r8192_priv *priv = ieee80211_priv(dev);

	msr  = read_nic_byte(dev, MSR);
	msrm = msr & MSR_LINK_MASK;
	msr2 = msr & ~MSR_LINK_MASK;

	if(NIC_8192U == priv->card_8192) {
		usb_kill_urb(priv->rx_urb[MAX_RX_URB]);
	}
	if ((msrm == (MSR_LINK_ADHOC<<MSR_LINK_SHIFT) ||
		(msrm == (MSR_LINK_MASTER<<MSR_LINK_SHIFT)))){
		write_nic_byte(dev, MSR, msr2 | MSR_LINK_NONE);
		write_nic_byte(dev, MSR, msr);
	}
}

void rtl8192_config_rate(struct net_device* dev, u16* rate_config)
{
	 struct r8192_priv *priv = ieee80211_priv(dev);
	 struct ieee80211_network *net;
	 u8 i=0, basic_rate = 0;
	 net = & priv->ieee80211->current_network;

	 for (i=0; i<net->rates_len; i++)
	 {
		 basic_rate = net->rates[i]&0x7f;
		 switch(basic_rate)
		 {
			 case MGN_1M:	*rate_config |= RRSR_1M;	break;
			 case MGN_2M:	*rate_config |= RRSR_2M;	break;
			 case MGN_5_5M:	*rate_config |= RRSR_5_5M;	break;
			 case MGN_11M:	*rate_config |= RRSR_11M;	break;
			 case MGN_6M:	*rate_config |= RRSR_6M;	break;
			 case MGN_9M:	*rate_config |= RRSR_9M;	break;
			 case MGN_12M:	*rate_config |= RRSR_12M;	break;
			 case MGN_18M:	*rate_config |= RRSR_18M;	break;
			 case MGN_24M:	*rate_config |= RRSR_24M;	break;
			 case MGN_36M:	*rate_config |= RRSR_36M;	break;
			 case MGN_48M:	*rate_config |= RRSR_48M;	break;
			 case MGN_54M:	*rate_config |= RRSR_54M;	break;
		 }
	 }
	 for (i=0; i<net->rates_ex_len; i++)
	 {
		 basic_rate = net->rates_ex[i]&0x7f;
		 switch(basic_rate)
		 {
			 case MGN_1M:	*rate_config |= RRSR_1M;	break;
			 case MGN_2M:	*rate_config |= RRSR_2M;	break;
			 case MGN_5_5M:	*rate_config |= RRSR_5_5M;	break;
			 case MGN_11M:	*rate_config |= RRSR_11M;	break;
			 case MGN_6M:	*rate_config |= RRSR_6M;	break;
			 case MGN_9M:	*rate_config |= RRSR_9M;	break;
			 case MGN_12M:	*rate_config |= RRSR_12M;	break;
			 case MGN_18M:	*rate_config |= RRSR_18M;	break;
			 case MGN_24M:	*rate_config |= RRSR_24M;	break;
			 case MGN_36M:	*rate_config |= RRSR_36M;	break;
			 case MGN_48M:	*rate_config |= RRSR_48M;	break;
			 case MGN_54M:	*rate_config |= RRSR_54M;	break;
		 }
	 }
}


#define SHORT_SLOT_TIME 9
#define NON_SHORT_SLOT_TIME 20

void rtl8192_update_cap(struct net_device* dev, u16 cap)
{
	//u32 tmp = 0;
	struct r8192_priv *priv = ieee80211_priv(dev);
	struct ieee80211_network *net = &priv->ieee80211->current_network;
	priv->short_preamble = cap & WLAN_CAPABILITY_SHORT_PREAMBLE;

	//LZM MOD 090303 HW_VAR_ACK_PREAMBLE
	if(0)
	{
		u8 tmp = 0;
		tmp = ((priv->nCur40MhzPrimeSC) << 5);
		if (priv->short_preamble)
			tmp |= 0x80;
		write_nic_byte(dev, RRSR+2, tmp);
	}

	if (net->mode & (IEEE_G|IEEE_N_24G))
	{
		u8 slot_time = 0;
		if ((cap & WLAN_CAPABILITY_SHORT_SLOT)&&(!priv->ieee80211->pHTInfo->bCurrentRT2RTLongSlotTime))
		{//short slot time
			slot_time = SHORT_SLOT_TIME;
		}
		else //long slot time
			slot_time = NON_SHORT_SLOT_TIME;
		priv->slot_time = slot_time;
		write_nic_byte(dev, SLOT_TIME, slot_time);
	}

}
void rtl8192_net_update(struct net_device *dev)
{

	struct r8192_priv *priv = ieee80211_priv(dev);
	struct ieee80211_network *net;
	u16 BcnTimeCfg = 0, BcnCW = 6, BcnIFS = 0xf;
	u16 rate_config = 0;
	net = & priv->ieee80211->current_network;

	rtl8192_config_rate(dev, &rate_config);
	priv->basic_rate = rate_config &= 0x15f;

	write_nic_dword(dev,BSSIDR,((u32*)net->bssid)[0]);
	write_nic_word(dev,BSSIDR+4,((u16*)net->bssid)[2]);
	//for(i=0;i<ETH_ALEN;i++)
	//	write_nic_byte(dev,BSSID+i,net->bssid[i]);

	rtl8192_update_msr(dev);
//	rtl8192_update_cap(dev, net->capability);
	if (priv->ieee80211->iw_mode == IW_MODE_ADHOC)
	{
	write_nic_word(dev, ATIMWND, 2);
	write_nic_word(dev, BCN_DMATIME, 1023);
	write_nic_word(dev, BCN_INTERVAL, net->beacon_interval);
//	write_nic_word(dev, BcnIntTime, 100);
	write_nic_word(dev, BCN_DRV_EARLY_INT, 1);
	write_nic_byte(dev, BCN_ERR_THRESH, 100);
		BcnTimeCfg |= (BcnCW<<BCN_TCFG_CW_SHIFT);
	// TODO: BcnIFS may required to be changed on ASIC
	 	BcnTimeCfg |= BcnIFS<<BCN_TCFG_IFS;

	write_nic_word(dev, BCN_TCFG, BcnTimeCfg);
	}



}

//temporary hw beacon is not used any more.
//open it when necessary
#if 1
void rtl819xusb_beacon_tx(struct net_device *dev,u16  tx_rate)
{
}
#endif
inline u8 rtl8192_IsWirelessBMode(u16 rate)
{
	if( ((rate <= 110) && (rate != 60) && (rate != 90)) || (rate == 220) )
		return 1;
	else return 0;
}

u16 N_DBPSOfRate(u16 DataRate);

u16 ComputeTxTime(
	u16		FrameLength,
	u16		DataRate,
	u8		bManagementFrame,
	u8		bShortPreamble
)
{
	u16	FrameTime;
	u16	N_DBPS;
	u16	Ceiling;

	if( rtl8192_IsWirelessBMode(DataRate) )
	{
		if( bManagementFrame || !bShortPreamble || DataRate == 10 )
		{	// long preamble
			FrameTime = (u16)(144+48+(FrameLength*8/(DataRate/10)));
		}
		else
		{	// Short preamble
			FrameTime = (u16)(72+24+(FrameLength*8/(DataRate/10)));
		}
		if( ( FrameLength*8 % (DataRate/10) ) != 0 ) //Get the Ceilling
				FrameTime ++;
	} else {	//802.11g DSSS-OFDM PLCP length field calculation.
		N_DBPS = N_DBPSOfRate(DataRate);
		Ceiling = (16 + 8*FrameLength + 6) / N_DBPS
				+ (((16 + 8*FrameLength + 6) % N_DBPS) ? 1 : 0);
		FrameTime = (u16)(16 + 4 + 4*Ceiling + 6);
	}
	return FrameTime;
}

u16 N_DBPSOfRate(u16 DataRate)
{
	 u16 N_DBPS = 24;

	 switch(DataRate)
	 {
	 case 60:
	  N_DBPS = 24;
	  break;

	 case 90:
	  N_DBPS = 36;
	  break;

	 case 120:
	  N_DBPS = 48;
	  break;

	 case 180:
	  N_DBPS = 72;
	  break;

	 case 240:
	  N_DBPS = 96;
	  break;

	 case 360:
	  N_DBPS = 144;
	  break;

	 case 480:
	  N_DBPS = 192;
	  break;

	 case 540:
	  N_DBPS = 216;
	  break;

	 default:
	  break;
	 }

	 return N_DBPS;
}

void rtl819xU_cmd_isr(struct urb *tx_cmd_urb, struct pt_regs *regs)
{
	usb_free_urb(tx_cmd_urb);
}

unsigned int txqueue2outpipe(struct r8192_priv* priv,unsigned int tx_queue) {

	if(tx_queue >= 9)
	{
		RT_TRACE(COMP_ERR,"%s():Unknown queue ID!!!\n",__FUNCTION__);
		return 0x04;
	}
	return priv->txqueue_to_outpipemap[tx_queue];
}

short rtl8192SU_tx_cmd(struct net_device *dev, struct sk_buff *skb)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	int			status;
	struct urb		*tx_urb;
	unsigned int 		idx_pipe;
	tx_desc_cmd_819x_usb *pdesc = (tx_desc_cmd_819x_usb *)skb->data;
	cb_desc *tcb_desc = (cb_desc *)(skb->cb + MAX_DEV_ADDR_SIZE);
	u8 queue_index = tcb_desc->queue_index;
	u32			PktSize = 0;

	//printk("\n %s::::::::::::::::::::::queue_index = %d\n",__FUNCTION__, queue_index);
	atomic_inc(&priv->tx_pending[queue_index]);

	tx_urb = usb_alloc_urb(0,GFP_ATOMIC);
	if(!tx_urb){
		dev_kfree_skb(skb);
		return -ENOMEM;
	}

	memset(pdesc, 0, USB_HWDESC_HEADER_LEN);

	/* Tx descriptor ought to be set according to the skb->cb */
	pdesc->LINIP = tcb_desc->bLastIniPkt;
	PktSize = (u16)(skb->len - USB_HWDESC_HEADER_LEN);
	pdesc->PktSize = PktSize;
	//printk("PKTSize = %d %x\n",pdesc->PktSize,pdesc->PktSize);
	//----------------------------------------------------------------------------
	// Fill up USB_OUT_CONTEXT.
	//----------------------------------------------------------------------------
	// Get index to out pipe from specified QueueID.
	idx_pipe = txqueue2outpipe(priv,queue_index);
	//printk("=============>%s queue_index:%d, outpipe:%d\n", __func__,queue_index,priv->RtOutPipes[idx_pipe]);

	usb_fill_bulk_urb(tx_urb,
	                            priv->udev,
	                            usb_sndbulkpipe(priv->udev,priv->RtOutPipes[idx_pipe]),
	                            skb->data,
	                            skb->len,
	                            rtl8192_tx_isr,
	                            skb);

	status = usb_submit_urb(tx_urb, GFP_ATOMIC);
	if (!status){
		return 0;
	}else{
		printk("Error TX CMD URB, error %d",
				status);
		return -1;
	}
}

/*
 * Mapping Software/Hardware descriptor queue id to "Queue Select Field"
 * in TxFwInfo data structure
 * 2006.10.30 by Emily
 *
 * \param QUEUEID       Software Queue
*/
u8 MapHwQueueToFirmwareQueue(u8 QueueID)
{
	u8 QueueSelect = 0x0;       //defualt set to

	switch(QueueID) {
		case BE_QUEUE:
			QueueSelect = QSLT_BE;  //or QSelect = pTcb->priority;
			break;

		case BK_QUEUE:
			QueueSelect = QSLT_BK;  //or QSelect = pTcb->priority;
			break;

		case VO_QUEUE:
			QueueSelect = QSLT_VO;  //or QSelect = pTcb->priority;
			break;

		case VI_QUEUE:
			QueueSelect = QSLT_VI;  //or QSelect = pTcb->priority;
			break;
		case MGNT_QUEUE:
			QueueSelect = QSLT_MGNT;
			break;

		case BEACON_QUEUE:
			QueueSelect = QSLT_BEACON;
			break;

			// TODO: 2006.10.30 mark other queue selection until we verify it is OK
			// TODO: Remove Assertions
//#if (RTL819X_FPGA_VER & RTL819X_FPGA_GUANGAN_070502)
		case TXCMD_QUEUE:
			QueueSelect = QSLT_CMD;
			break;
//#endif
		case HIGH_QUEUE:
			QueueSelect = QSLT_HIGH;
			break;

		default:
			RT_TRACE(COMP_ERR, "TransmitTCB(): Impossible Queue Selection: %d \n", QueueID);
			break;
	}
	return QueueSelect;
}

u8 MRateToHwRate8190Pci(u8 rate)
{
	u8	ret = DESC92S_RATE1M;

	switch(rate)
	{
		// CCK and OFDM non-HT rates
	case MGN_1M:		ret = DESC92S_RATE1M;	break;
	case MGN_2M:		ret = DESC92S_RATE2M;	break;
	case MGN_5_5M:		ret = DESC92S_RATE5_5M;	break;
	case MGN_11M:		ret = DESC92S_RATE11M;	break;
	case MGN_6M:		ret = DESC92S_RATE6M;	break;
	case MGN_9M:		ret = DESC92S_RATE9M;	break;
	case MGN_12M:		ret = DESC92S_RATE12M;	break;
	case MGN_18M:		ret = DESC92S_RATE18M;	break;
	case MGN_24M:		ret = DESC92S_RATE24M;	break;
	case MGN_36M:		ret = DESC92S_RATE36M;	break;
	case MGN_48M:		ret = DESC92S_RATE48M;	break;
	case MGN_54M:		ret = DESC92S_RATE54M;	break;

		// HT rates since here
	case MGN_MCS0:		ret = DESC92S_RATEMCS0;	break;
	case MGN_MCS1:		ret = DESC92S_RATEMCS1;	break;
	case MGN_MCS2:		ret = DESC92S_RATEMCS2;	break;
	case MGN_MCS3:		ret = DESC92S_RATEMCS3;	break;
	case MGN_MCS4:		ret = DESC92S_RATEMCS4;	break;
	case MGN_MCS5:		ret = DESC92S_RATEMCS5;	break;
	case MGN_MCS6:		ret = DESC92S_RATEMCS6;	break;
	case MGN_MCS7:		ret = DESC92S_RATEMCS7;	break;
	case MGN_MCS8:		ret = DESC92S_RATEMCS8;	break;
	case MGN_MCS9:		ret = DESC92S_RATEMCS9;	break;
	case MGN_MCS10:	ret = DESC92S_RATEMCS10;	break;
	case MGN_MCS11:	ret = DESC92S_RATEMCS11;	break;
	case MGN_MCS12:	ret = DESC92S_RATEMCS12;	break;
	case MGN_MCS13:	ret = DESC92S_RATEMCS13;	break;
	case MGN_MCS14:	ret = DESC92S_RATEMCS14;	break;
	case MGN_MCS15:	ret = DESC92S_RATEMCS15;	break;

	// Set the highest SG rate
	case MGN_MCS0_SG:
	case MGN_MCS1_SG:
	case MGN_MCS2_SG:
	case MGN_MCS3_SG:
	case MGN_MCS4_SG:
	case MGN_MCS5_SG:
	case MGN_MCS6_SG:
	case MGN_MCS7_SG:
	case MGN_MCS8_SG:
	case MGN_MCS9_SG:
	case MGN_MCS10_SG:
	case MGN_MCS11_SG:
	case MGN_MCS12_SG:
	case MGN_MCS13_SG:
	case MGN_MCS14_SG:
	case MGN_MCS15_SG:
	{
		ret = DESC92S_RATEMCS15_SG;
		break;
	}

	default:		break;
	}
	return ret;
}

u8 QueryIsShort(u8 TxHT, u8 TxRate, cb_desc *tcb_desc)
{
	u8   tmp_Short;

	tmp_Short = (TxHT==1)?((tcb_desc->bUseShortGI)?1:0):((tcb_desc->bUseShortPreamble)?1:0);

	if(TxHT==1 && TxRate != DESC90_RATEMCS15)
		tmp_Short = 0;

	return tmp_Short;
}

static void tx_zero_isr(struct urb *tx_urb)
{
	return;
}


/*
 * The tx procedure is just as following,  skb->cb will contain all the following
 *information: * priority, morefrag, rate, &dev.
 * */
 //	<Note> Buffer format for 8192S Usb bulk out:
//
//  --------------------------------------------------
//  | 8192S Usb Tx Desc | 802_11_MAC_header |    data          |
//  --------------------------------------------------
//  |  32 bytes           	  |       24 bytes             |0-2318 bytes|
//  --------------------------------------------------
//  |<------------ BufferLen ------------------------->|

short rtl8192SU_tx(struct net_device *dev, struct sk_buff* skb)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	cb_desc *tcb_desc = (cb_desc *)(skb->cb + MAX_DEV_ADDR_SIZE);
	tx_desc_819x_usb *tx_desc = (tx_desc_819x_usb *)skb->data;
	//tx_fwinfo_819x_usb *tx_fwinfo = (tx_fwinfo_819x_usb *)(skb->data + USB_HWDESC_HEADER_LEN);//92su del
	struct usb_device *udev = priv->udev;
	int pend;
	int status;
	struct urb *tx_urb = NULL, *tx_urb_zero = NULL;
	//int urb_len;
	unsigned int idx_pipe;
	u16		MPDUOverhead = 0;
 	//RT_DEBUG_DATA(COMP_SEND, tcb_desc, sizeof(cb_desc));

	pend = atomic_read(&priv->tx_pending[tcb_desc->queue_index]);
	/* we are locked here so the two atomic_read and inc are executed
	 * without interleaves  * !!! For debug purpose 	  */
	if( pend > MAX_TX_URB){
		switch (tcb_desc->queue_index) {
			case VO_PRIORITY:
				priv->stats.txvodrop++;
				break;
			case VI_PRIORITY:
				priv->stats.txvidrop++;
				break;
			case BE_PRIORITY:
				priv->stats.txbedrop++;
				break;
			default://BK_PRIORITY
				priv->stats.txbkdrop++;
				break;
		}
		printk("To discard skb packet!\n");
		dev_kfree_skb_any(skb);
		return -1;
	}

	tx_urb = usb_alloc_urb(0,GFP_ATOMIC);
	if(!tx_urb){
		dev_kfree_skb_any(skb);
		return -ENOMEM;
	}

	memset(tx_desc, 0, sizeof(tx_desc_819x_usb));


		tx_desc->NonQos = (IsQoSDataFrame(skb->data)==TRUE)? 0:1;

	/* Fill Tx descriptor */
	//memset(tx_fwinfo,0,sizeof(tx_fwinfo_819x_usb));

	// This part can just fill to the first descriptor of the frame.
	/* DWORD 0 */
	tx_desc->TxHT = (tcb_desc->data_rate&0x80)?1:0;


	tx_desc->TxRate = MRateToHwRate8190Pci(tcb_desc->data_rate);
	//tx_desc->EnableCPUDur = tcb_desc->bTxEnableFwCalcDur;
	tx_desc->TxShort = QueryIsShort(tx_desc->TxHT, tx_desc->TxRate, tcb_desc);


	// Aggregation related
	if(tcb_desc->bAMPDUEnable) {//AMPDU enabled
		tx_desc->AllowAggregation = 1;
		/* DWORD 1 */
		//tx_fwinfo->RxMF = tcb_desc->ampdu_factor;
		//tx_fwinfo->RxAMD = tcb_desc->ampdu_density&0x07;//ampdudensity
	} else {
		tx_desc->AllowAggregation = 0;
		/* DWORD 1 */
		//tx_fwinfo->RxMF = 0;
		//tx_fwinfo->RxAMD = 0;
	}

	//
	// <Roger_Notes> For AMPDU case, we must insert SSN into TX_DESC,
	// FW according as this SSN to do necessary packet retry.
	// 2008.06.06.
	//
	{
		u8	*pSeq;
		u16	Temp;
		//pSeq = (u8 *)(VirtualAddress+USB_HWDESC_HEADER_LEN + FRAME_OFFSET_SEQUENCE);
		pSeq = (u8 *)(skb->data+USB_HWDESC_HEADER_LEN + 22);
		Temp = pSeq[0];
		Temp <<= 12;
		Temp |= (*(u16 *)pSeq)>>4;
		tx_desc->Seq = Temp;
	}

	/* Protection mode related */
	tx_desc->RTSEn = (tcb_desc->bRTSEnable)?1:0;
	tx_desc->CTS2Self = (tcb_desc->bCTSEnable)?1:0;
	tx_desc->RTSSTBC = (tcb_desc->bRTSSTBC)?1:0;
	tx_desc->RTSHT = (tcb_desc->rts_rate&0x80)?1:0;
	tx_desc->RTSRate =  MRateToHwRate8190Pci((u8)tcb_desc->rts_rate);
	tx_desc->RTSSubcarrier = (tx_desc->RTSHT==0)?(tcb_desc->RTSSC):0;
	tx_desc->RTSBW = (tx_desc->RTSHT==1)?((tcb_desc->bRTSBW)?1:0):0;
	tx_desc->RTSShort = (tx_desc->RTSHT==0)?(tcb_desc->bRTSUseShortPreamble?1:0):\
				(tcb_desc->bRTSUseShortGI?1:0);
	//LZM 090219
	tx_desc->DisRTSFB = 0;
	tx_desc->RTSRateFBLmt = 0xf;

	// <Roger_EXP> 2008.09.22. We disable RTS rate fallback temporarily.
	//tx_desc->DisRTSFB = 0x01;

	/* Set Bandwidth and sub-channel settings. */
	if(priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20_40)
	{
		if(tcb_desc->bPacketBW) {
			tx_desc->TxBandwidth = 1;
			tx_desc->TxSubCarrier = 0;    //By SD3's Jerry suggestion, use duplicated mode
		} else {
			tx_desc->TxBandwidth = 0;
			tx_desc->TxSubCarrier = priv->nCur40MhzPrimeSC;
		}
	} else {
		tx_desc->TxBandwidth = 0;
		tx_desc->TxSubCarrier = 0;
	}


	//memset(tx_desc, 0, sizeof(tx_desc_819x_usb));
	/* DWORD 0 */
        tx_desc->LINIP = 0;
        //tx_desc->CmdInit = 1; //92su del
        tx_desc->Offset =  USB_HWDESC_HEADER_LEN;

	{
		tx_desc->PktSize = (skb->len - USB_HWDESC_HEADER_LEN) & 0xffff;
	}

	/*DWORD 1*/
	//tx_desc->SecCAMID= 0;//92su del
	tx_desc->RaBRSRID= tcb_desc->RATRIndex;
//#ifdef RTL8192S_PREPARE_FOR_NORMAL_RELEASE

	{
		MPDUOverhead = 0;
		//tx_desc->NoEnc = 1;//92su del
	}

	tx_desc->SecType = 0x0;

		if (tcb_desc->bHwSec)
			{
				switch (priv->ieee80211->pairwise_key_type)
				{
					case KEY_TYPE_WEP40:
					case KEY_TYPE_WEP104:
						 tx_desc->SecType = 0x1;
						 //tx_desc->NoEnc = 0;//92su del
						 break;
					case KEY_TYPE_TKIP:
						 tx_desc->SecType = 0x2;
						 //tx_desc->NoEnc = 0;//92su del
						 break;
					case KEY_TYPE_CCMP:
						 tx_desc->SecType = 0x3;
						 //tx_desc->NoEnc = 0;//92su del
						 break;
					case KEY_TYPE_NA:
						 tx_desc->SecType = 0x0;
						 //tx_desc->NoEnc = 1;//92su del
						 break;
					default:
						 tx_desc->SecType = 0x0;
						 //tx_desc->NoEnc = 1;//92su del
						 break;
				}
			}

	//tx_desc->TxFWInfoSize =  sizeof(tx_fwinfo_819x_usb);//92su del


	tx_desc->USERATE = tcb_desc->bTxUseDriverAssingedRate;
	tx_desc->DISFB = tcb_desc->bTxDisableRateFallBack;
	tx_desc->DataRateFBLmt = 0x1F;// Alwasy enable all rate fallback range

	tx_desc->QueueSelect = MapHwQueueToFirmwareQueue(tcb_desc->queue_index);


        /* Fill fields that are required to be initialized in all of the descriptors */
        //DWORD 0
        tx_desc->FirstSeg = 1;
        tx_desc->LastSeg = 1;
        tx_desc->OWN = 1;

	{
		//DWORD 2
		//tx_desc->TxBufferSize = (u32)(skb->len - USB_HWDESC_HEADER_LEN);
		tx_desc->TxBufferSize = (u32)(skb->len);//92su mod FIXLZM
	}

	/* Get index to out pipe from specified QueueID */
	idx_pipe = txqueue2outpipe(priv,tcb_desc->queue_index);
	//printk("=============>%s queue_index:%d, outpipe:%d\n", __func__,tcb_desc->queue_index,priv->RtOutPipes[idx_pipe]);

	//RT_DEBUG_DATA(COMP_SEND,tx_fwinfo,sizeof(tx_fwinfo_819x_usb));
	//RT_DEBUG_DATA(COMP_SEND,tx_desc,sizeof(tx_desc_819x_usb));

	/* To submit bulk urb */
	usb_fill_bulk_urb(tx_urb,
				    udev,
				    usb_sndbulkpipe(udev,priv->RtOutPipes[idx_pipe]),
				    skb->data,
				    skb->len, rtl8192_tx_isr, skb);

	status = usb_submit_urb(tx_urb, GFP_ATOMIC);
	if (!status){
//we need to send 0 byte packet whenever 512N bytes/64N(HIGN SPEED/NORMAL SPEED) bytes packet has been transmitted. Otherwise, it will be halt to wait for another packet. WB. 2008.08.27
		bool bSend0Byte = false;
		u8 zero = 0;
		if(udev->speed == USB_SPEED_HIGH)
		{
			if (skb->len > 0 && skb->len % 512 == 0)
				bSend0Byte = true;
		}
		else
		{
			if (skb->len > 0 && skb->len % 64 == 0)
				bSend0Byte = true;
		}
		if (bSend0Byte)
		{
#if 1
			tx_urb_zero = usb_alloc_urb(0,GFP_ATOMIC);
			if(!tx_urb_zero){
				RT_TRACE(COMP_ERR, "can't alloc urb for zero byte\n");
				return -ENOMEM;
			}
			usb_fill_bulk_urb(tx_urb_zero,udev,
					usb_sndbulkpipe(udev,idx_pipe), &zero,
					0, tx_zero_isr, dev);
			status = usb_submit_urb(tx_urb_zero, GFP_ATOMIC);
			if (status){
			RT_TRACE(COMP_ERR, "Error TX URB for zero byte %d, error %d", atomic_read(&priv->tx_pending[tcb_desc->queue_index]), status);
			return -1;
			}
#endif
		}
		dev->trans_start = jiffies;
		atomic_inc(&priv->tx_pending[tcb_desc->queue_index]);
		return 0;
	}else{
		RT_TRACE(COMP_ERR, "Error TX URB %d, error %d", atomic_read(&priv->tx_pending[tcb_desc->queue_index]),
				status);
		return -1;
	}
}

void rtl8192SU_net_update(struct net_device *dev)
{

	struct r8192_priv *priv = ieee80211_priv(dev);
	struct ieee80211_device* ieee = priv->ieee80211;
	struct ieee80211_network *net = &priv->ieee80211->current_network;
	//u16 BcnTimeCfg = 0, BcnCW = 6, BcnIFS = 0xf;
	u16 rate_config = 0;
	u32 regTmp = 0;
	u8 rateIndex = 0;
	u8	retrylimit = 0x30;
	u16 cap = net->capability;

	priv->short_preamble = cap & WLAN_CAPABILITY_SHORT_PREAMBLE;

//HW_VAR_BASIC_RATE
	//update Basic rate: RR, BRSR
	rtl8192_config_rate(dev, &rate_config);	//HalSetBrateCfg

	priv->basic_rate = rate_config  = rate_config & 0x15f;

	// Set RRSR rate table.
	write_nic_byte(dev, RRSR, rate_config&0xff);
	write_nic_byte(dev, RRSR+1, (rate_config>>8)&0xff);

	// Set RTS initial rate
	while(rate_config > 0x1)
	{
		rate_config = (rate_config>> 1);
		rateIndex++;
	}
	write_nic_byte(dev, INIRTSMCS_SEL, rateIndex);
//HW_VAR_BASIC_RATE

	//set ack preample
	regTmp = (priv->nCur40MhzPrimeSC) << 5;
	if (priv->short_preamble)
		regTmp |= 0x80;
	write_nic_byte(dev, RRSR+2, regTmp);

	write_nic_dword(dev,BSSIDR,((u32*)net->bssid)[0]);
	write_nic_word(dev,BSSIDR+4,((u16*)net->bssid)[2]);

	write_nic_word(dev, BCN_INTERVAL, net->beacon_interval);
	//2008.10.24 added by tynli for beacon changed.
	PHY_SetBeaconHwReg( dev, net->beacon_interval);

	rtl8192_update_cap(dev, cap);

	if (ieee->iw_mode == IW_MODE_ADHOC){
		retrylimit = 7;
		//we should enable ibss interrupt here, but disable it temporarily
		if (0){
			priv->irq_mask |= (IMR_BcnInt | IMR_BcnInt | IMR_TBDOK | IMR_TBDER);
			//rtl8192_irq_disable(dev);
			//rtl8192_irq_enable(dev);
		}
	}
	else{
		if (0){
			priv->irq_mask &= ~(IMR_BcnInt | IMR_BcnInt | IMR_TBDOK | IMR_TBDER);
			//rtl8192_irq_disable(dev);
			//rtl8192_irq_enable(dev);
		}
	}

	priv->ShortRetryLimit = priv->LongRetryLimit = retrylimit;

	write_nic_word(dev, 	RETRY_LIMIT,
				retrylimit << RETRY_LIMIT_SHORT_SHIFT | \
				retrylimit << RETRY_LIMIT_LONG_SHIFT);
}

void rtl8192SU_update_ratr_table(struct net_device* dev)
{
		struct r8192_priv* priv = ieee80211_priv(dev);
	struct ieee80211_device* ieee = priv->ieee80211;
	u8* pMcsRate = ieee->dot11HTOperationalRateSet;
	//struct ieee80211_network *net = &ieee->current_network;
	u32 ratr_value = 0;

	u8 rate_index = 0;
	int WirelessMode = ieee->mode;
	u8 MimoPs = ieee->pHTInfo->PeerMimoPs;

	u8 bNMode = 0;

	rtl8192_config_rate(dev, (u16*)(&ratr_value));
	ratr_value |= (*(u16*)(pMcsRate)) << 12;

	//switch (ieee->mode)
	switch (WirelessMode)
	{
		case IEEE_A:
			ratr_value &= 0x00000FF0;
			break;
		case IEEE_B:
			ratr_value &= 0x0000000D;
			break;
		case IEEE_G:
			ratr_value &= 0x00000FF5;
			break;
		case IEEE_N_24G:
		case IEEE_N_5G:
		{
			bNMode = 1;

			if (MimoPs == 0) //MIMO_PS_STATIC
					{
				ratr_value &= 0x0007F005;
			}
			else
			{	// MCS rate only => for 11N mode.
				u32	ratr_mask;

				// 1T2R or 1T1R, Spatial Stream 2 should be disabled
				if (	priv->rf_type == RF_1T2R ||
					priv->rf_type == RF_1T1R ||
					(ieee->pHTInfo->IOTAction & HT_IOT_ACT_DISABLE_TX_2SS) )
						ratr_mask = 0x000ff005;
					else
						ratr_mask = 0x0f0ff005;

				if((ieee->pHTInfo->bCurTxBW40MHz) &&
				    !(ieee->pHTInfo->IOTAction & HT_IOT_ACT_DISABLE_TX_40_MHZ))
					ratr_mask |= 0x00000010; // Set 6MBps

				// Select rates for rate adaptive mechanism.
					ratr_value &= ratr_mask;
					}
			}
			break;
		default:
			if(0)
			{
				if(priv->rf_type == RF_1T2R)	// 1T2R, Spatial Stream 2 should be disabled
				{
				ratr_value &= 0x000ff0f5;
				}
				else
				{
				ratr_value &= 0x0f0ff0f5;
				}
			}
			//printk("====>%s(), mode is not correct:%x\n", __FUNCTION__, ieee->mode);
			break;
	}

	ratr_value &= 0x0FFFFFFF;

	// Get MAX MCS available.
	if (   (bNMode && ((ieee->pHTInfo->IOTAction & HT_IOT_ACT_DISABLE_SHORT_GI)==0)) &&
		((ieee->pHTInfo->bCurBW40MHz && ieee->pHTInfo->bCurShortGI40MHz) ||
	        (!ieee->pHTInfo->bCurBW40MHz && ieee->pHTInfo->bCurShortGI20MHz)))
	{
		u8 shortGI_rate = 0;
		u32 tmp_ratr_value = 0;
		ratr_value |= 0x10000000;//???
		tmp_ratr_value = (ratr_value>>12);
		for(shortGI_rate=15; shortGI_rate>0; shortGI_rate--)
		{
			if((1<<shortGI_rate) & tmp_ratr_value)
				break;
		}
		shortGI_rate = (shortGI_rate<<12)|(shortGI_rate<<8)|(shortGI_rate<<4)|(shortGI_rate);
		write_nic_byte(dev, SG_RATE, shortGI_rate);
		//printk("==>SG_RATE:%x\n", read_nic_byte(dev, SG_RATE));
	}
	write_nic_dword(dev, ARFR0+rate_index*4, ratr_value);
	printk("=============>ARFR0+rate_index*4:%#x\n", ratr_value);

	//2 UFWP
	if (ratr_value & 0xfffff000){
		//printk("===>set to N mode\n");
		HalSetFwCmd8192S(dev, FW_CMD_RA_REFRESH_N);
	}
	else	{
		//printk("===>set to B/G mode\n");
		HalSetFwCmd8192S(dev, FW_CMD_RA_REFRESH_BG);
	}
}

void rtl8192SU_link_change(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	struct ieee80211_device* ieee = priv->ieee80211;
	//unsigned long flags;
	u32 reg = 0;

	printk("=====>%s 1\n", __func__);
	reg = read_nic_dword(dev, RCR);

	if (ieee->state == IEEE80211_LINKED)
	{

		rtl8192SU_net_update(dev);
		rtl8192SU_update_ratr_table(dev);
		ieee->SetFwCmdHandler(dev, FW_CMD_HIGH_PWR_ENABLE);
		priv->ReceiveConfig = reg |= RCR_CBSSID;

	}else{
		priv->ReceiveConfig = reg &= ~RCR_CBSSID;

	}

	write_nic_dword(dev, RCR, reg);
	rtl8192_update_msr(dev);

	printk("<=====%s 2\n", __func__);
}

static struct ieee80211_qos_parameters def_qos_parameters = {
        {3,3,3,3},/* cw_min */
        {7,7,7,7},/* cw_max */
        {2,2,2,2},/* aifs */
        {0,0,0,0},/* flags */
        {0,0,0,0} /* tx_op_limit */
};


void rtl8192_update_beacon(struct work_struct * work)
{
        struct r8192_priv *priv = container_of(work, struct r8192_priv, update_beacon_wq.work);
        struct net_device *dev = priv->ieee80211->dev;
 	struct ieee80211_device* ieee = priv->ieee80211;
	struct ieee80211_network* net = &ieee->current_network;

	if (ieee->pHTInfo->bCurrentHTSupport)
		HTUpdateSelfAndPeerSetting(ieee, net);
	ieee->pHTInfo->bCurrentRT2RTLongSlotTime = net->bssht.bdRT2RTLongSlotTime;
	// Joseph test for turbo mode with AP
	ieee->pHTInfo->RT2RT_HT_Mode = net->bssht.RT2RT_HT_Mode;
	rtl8192_update_cap(dev, net->capability);
}
/*
* background support to run QoS activate functionality
*/
int WDCAPARA_ADD[] = {EDCAPARA_BE,EDCAPARA_BK,EDCAPARA_VI,EDCAPARA_VO};

void rtl8192_qos_activate(struct work_struct * work)
{
        struct r8192_priv *priv = container_of(work, struct r8192_priv, qos_activate);
        struct net_device *dev = priv->ieee80211->dev;
        struct ieee80211_qos_parameters *qos_parameters = &priv->ieee80211->current_network.qos_data.parameters;
        u8 mode = priv->ieee80211->current_network.mode;
        //u32 size = sizeof(struct ieee80211_qos_parameters);
	u8  u1bAIFS;
	u32 u4bAcParam;
        int i;

        if (priv == NULL)
                return;

       mutex_lock(&priv->mutex);

        if(priv->ieee80211->state != IEEE80211_LINKED)
		goto success;
	RT_TRACE(COMP_QOS,"qos active process with associate response received\n");
	/* It better set slot time at first */
	/* For we just support b/g mode at present, let the slot time at 9/20 selection */
	/* update the ac parameter to related registers */
	for(i = 0; i <  QOS_QUEUE_NUM; i++) {
		//Mode G/A: slotTimeTimer = 9; Mode B: 20
		u1bAIFS = qos_parameters->aifs[i] * ((mode&(IEEE_G|IEEE_N_24G)) ?9:20) + aSifsTime;
		u4bAcParam = ((((u32)(qos_parameters->tx_op_limit[i]))<< AC_PARAM_TXOP_LIMIT_OFFSET)|
				(((u32)(qos_parameters->cw_max[i]))<< AC_PARAM_ECW_MAX_OFFSET)|
				(((u32)(qos_parameters->cw_min[i]))<< AC_PARAM_ECW_MIN_OFFSET)|
				((u32)u1bAIFS << AC_PARAM_AIFS_OFFSET));

		write_nic_dword(dev, WDCAPARA_ADD[i], u4bAcParam);
		//write_nic_dword(dev, WDCAPARA_ADD[i], 0x005e4322);
	}

success:
       mutex_unlock(&priv->mutex);
}

static int rtl8192_qos_handle_probe_response(struct r8192_priv *priv,
		int active_network,
		struct ieee80211_network *network)
{
	int ret = 0;
	u32 size = sizeof(struct ieee80211_qos_parameters);

	if(priv->ieee80211->state !=IEEE80211_LINKED)
                return ret;

        if ((priv->ieee80211->iw_mode != IW_MODE_INFRA))
                return ret;

	if (network->flags & NETWORK_HAS_QOS_MASK) {
		if (active_network &&
				(network->flags & NETWORK_HAS_QOS_PARAMETERS))
			network->qos_data.active = network->qos_data.supported;

		if ((network->qos_data.active == 1) && (active_network == 1) &&
				(network->flags & NETWORK_HAS_QOS_PARAMETERS) &&
				(network->qos_data.old_param_count !=
				 network->qos_data.param_count)) {
			network->qos_data.old_param_count =
				network->qos_data.param_count;
			queue_work(priv->priv_wq, &priv->qos_activate);
			RT_TRACE (COMP_QOS, "QoS parameters change call "
					"qos_activate\n");
		}
	} else {
		memcpy(&priv->ieee80211->current_network.qos_data.parameters,\
		       &def_qos_parameters, size);

		if ((network->qos_data.active == 1) && (active_network == 1)) {
			queue_work(priv->priv_wq, &priv->qos_activate);
			RT_TRACE(COMP_QOS, "QoS was disabled call qos_activate \n");
		}
		network->qos_data.active = 0;
		network->qos_data.supported = 0;
	}

	return 0;
}

/* handle manage frame frame beacon and probe response */
static int rtl8192_handle_beacon(struct net_device * dev,
				 struct ieee80211_probe_response *beacon,
				 struct ieee80211_network *network)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	rtl8192_qos_handle_probe_response(priv,1,network);
	queue_delayed_work(priv->priv_wq, &priv->update_beacon_wq, 0);

	return 0;

}

/*
* handling the beaconing responses. if we get different QoS setting
* off the network from the associated setting, adjust the QoS
* setting
*/
static int rtl8192_qos_association_resp(struct r8192_priv *priv,
                                    struct ieee80211_network *network)
{
        int ret = 0;
        unsigned long flags;
        u32 size = sizeof(struct ieee80211_qos_parameters);
        int set_qos_param = 0;

        if ((priv == NULL) || (network == NULL))
                return ret;

	if(priv->ieee80211->state !=IEEE80211_LINKED)
                return ret;

        if ((priv->ieee80211->iw_mode != IW_MODE_INFRA))
                return ret;

        spin_lock_irqsave(&priv->ieee80211->lock, flags);
	if(network->flags & NETWORK_HAS_QOS_PARAMETERS) {
		memcpy(&priv->ieee80211->current_network.qos_data.parameters,\
			 &network->qos_data.parameters,\
			sizeof(struct ieee80211_qos_parameters));
		priv->ieee80211->current_network.qos_data.active = 1;
		 {
                        set_qos_param = 1;
			/* update qos parameter for current network */
			priv->ieee80211->current_network.qos_data.old_param_count = \
				 priv->ieee80211->current_network.qos_data.param_count;
			priv->ieee80211->current_network.qos_data.param_count = \
			     	 network->qos_data.param_count;
		}
        } else {
		memcpy(&priv->ieee80211->current_network.qos_data.parameters,\
		       &def_qos_parameters, size);
		priv->ieee80211->current_network.qos_data.active = 0;
		priv->ieee80211->current_network.qos_data.supported = 0;
                set_qos_param = 1;
        }

        spin_unlock_irqrestore(&priv->ieee80211->lock, flags);

	RT_TRACE(COMP_QOS, "%s: network->flags = %d,%d\n",__FUNCTION__,network->flags ,priv->ieee80211->current_network.qos_data.active);
	if (set_qos_param == 1)
		queue_work(priv->priv_wq, &priv->qos_activate);

        return ret;
}


static int rtl8192_handle_assoc_response(struct net_device *dev,
                                     struct ieee80211_assoc_response_frame *resp,
                                     struct ieee80211_network *network)
{
        struct r8192_priv *priv = ieee80211_priv(dev);
        rtl8192_qos_association_resp(priv, network);
        return 0;
}


void rtl8192_update_ratr_table(struct net_device* dev)
	//	POCTET_STRING	posLegacyRate,
	//	u8*			pMcsRate)
	//	PRT_WLAN_STA	pEntry)
{
	struct r8192_priv* priv = ieee80211_priv(dev);
	struct ieee80211_device* ieee = priv->ieee80211;
	u8* pMcsRate = ieee->dot11HTOperationalRateSet;
	//struct ieee80211_network *net = &ieee->current_network;
	u32 ratr_value = 0;
	u8 rate_index = 0;
	rtl8192_config_rate(dev, (u16*)(&ratr_value));
	ratr_value |= (*(u16*)(pMcsRate)) << 12;
//	switch (net->mode)
	switch (ieee->mode)
	{
		case IEEE_A:
			ratr_value &= 0x00000FF0;
			break;
		case IEEE_B:
			ratr_value &= 0x0000000F;
			break;
		case IEEE_G:
			ratr_value &= 0x00000FF7;
			break;
		case IEEE_N_24G:
		case IEEE_N_5G:
			if (ieee->pHTInfo->PeerMimoPs == 0) //MIMO_PS_STATIC
				ratr_value &= 0x0007F007;
			else{
				if (priv->rf_type == RF_1T2R)
					ratr_value &= 0x000FF007;
				else
					ratr_value &= 0x0F81F007;
			}
			break;
		default:
			break;
	}
	ratr_value &= 0x0FFFFFFF;
	if(ieee->pHTInfo->bCurTxBW40MHz && ieee->pHTInfo->bCurShortGI40MHz){
		ratr_value |= 0x80000000;
	}else if(!ieee->pHTInfo->bCurTxBW40MHz && ieee->pHTInfo->bCurShortGI20MHz){
		ratr_value |= 0x80000000;
	}
	write_nic_dword(dev, RATR0+rate_index*4, ratr_value);
	write_nic_byte(dev, UFWP, 1);
}

static u8 ccmp_ie[4] = {0x00,0x50,0xf2,0x04};
static u8 ccmp_rsn_ie[4] = {0x00, 0x0f, 0xac, 0x04};
bool GetNmodeSupportBySecCfg8192(struct net_device*dev)
{
#if 1
	struct r8192_priv* priv = ieee80211_priv(dev);
	struct ieee80211_device* ieee = priv->ieee80211;
	struct ieee80211_network * network = &ieee->current_network;
        int wpa_ie_len= ieee->wpa_ie_len;
        struct ieee80211_crypt_data* crypt;
        int encrypt;
	return TRUE;

        crypt = ieee->crypt[ieee->tx_keyidx];
	//we use connecting AP's capability instead of only security config on our driver to distinguish whether it should use N mode or G mode
        encrypt = (network->capability & WLAN_CAPABILITY_PRIVACY) || (ieee->host_encrypt && crypt && crypt->ops && (0 == strcmp(crypt->ops->name,"WEP")));

	/* simply judge  */
	if(encrypt && (wpa_ie_len == 0)) {
		/* wep encryption, no N mode setting */
		return false;
//	} else if((wpa_ie_len != 0)&&(memcmp(&(ieee->wpa_ie[14]),ccmp_ie,4))) {
	} else if((wpa_ie_len != 0)) {
		/* parse pairwise key type */
		//if((pairwisekey = WEP40)||(pairwisekey = WEP104)||(pairwisekey = TKIP))
		if (((ieee->wpa_ie[0] == 0xdd) && (!memcmp(&(ieee->wpa_ie[14]),ccmp_ie,4))) || ((ieee->wpa_ie[0] == 0x30) && (!memcmp(&ieee->wpa_ie[10],ccmp_rsn_ie, 4))))
			return true;
		else
			return false;
	} else {
		return true;
	}

	return true;
#endif
}

bool GetHalfNmodeSupportByAPs819xUsb(struct net_device* dev)
{
	bool			Reval;
	struct r8192_priv* priv = ieee80211_priv(dev);
	struct ieee80211_device* ieee = priv->ieee80211;

// 	Added by Roger, 2008.08.29.
	return false;

	if(ieee->bHalfWirelessN24GMode == true)
		Reval = true;
	else
		Reval =  false;

	return Reval;
}

void rtl8192_refresh_supportrate(struct r8192_priv* priv)
{
	struct ieee80211_device* ieee = priv->ieee80211;
	//we donot consider set support rate for ABG mode, only HT MCS rate is set here.
	if (ieee->mode == WIRELESS_MODE_N_24G || ieee->mode == WIRELESS_MODE_N_5G)
	{
		memcpy(ieee->Regdot11HTOperationalRateSet, ieee->RegHTSuppRateSet, 16);
		//RT_DEBUG_DATA(COMP_INIT, ieee->RegHTSuppRateSet, 16);
		//RT_DEBUG_DATA(COMP_INIT, ieee->Regdot11HTOperationalRateSet, 16);
	}
	else
		memset(ieee->Regdot11HTOperationalRateSet, 0, 16);
	return;
}

u8 rtl8192_getSupportedWireleeMode(struct net_device*dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8 ret = 0;
	switch(priv->rf_chip)
	{
		case RF_8225:
		case RF_8256:
		case RF_PSEUDO_11N:
		case RF_6052:
			ret = (WIRELESS_MODE_N_24G|WIRELESS_MODE_G|WIRELESS_MODE_B);
			break;
		case RF_8258:
			ret = (WIRELESS_MODE_A|WIRELESS_MODE_N_5G);
			break;
		default:
			ret = WIRELESS_MODE_B;
			break;
	}
	return ret;
}
void rtl8192_SetWirelessMode(struct net_device* dev, u8 wireless_mode)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8 bSupportMode = rtl8192_getSupportedWireleeMode(dev);

#if 1
	if ((wireless_mode == WIRELESS_MODE_AUTO) || ((wireless_mode&bSupportMode)==0))
	{
		if(bSupportMode & WIRELESS_MODE_N_24G)
		{
			wireless_mode = WIRELESS_MODE_N_24G;
		}
		else if(bSupportMode & WIRELESS_MODE_N_5G)
		{
			wireless_mode = WIRELESS_MODE_N_5G;
		}
		else if((bSupportMode & WIRELESS_MODE_A))
		{
			wireless_mode = WIRELESS_MODE_A;
		}
		else if((bSupportMode & WIRELESS_MODE_G))
		{
			wireless_mode = WIRELESS_MODE_G;
		}
		else if((bSupportMode & WIRELESS_MODE_B))
		{
			wireless_mode = WIRELESS_MODE_B;
		}
		else{
			RT_TRACE(COMP_ERR, "%s(), No valid wireless mode supported, SupportedWirelessMode(%x)!!!\n", __FUNCTION__,bSupportMode);
			wireless_mode = WIRELESS_MODE_B;
		}
	}
#ifdef TO_DO_LIST //// TODO: this function doesn't work well at this time, we shoud wait for FPGA
	ActUpdateChannelAccessSetting( pAdapter, pHalData->CurrentWirelessMode, &pAdapter->MgntInfo.Info8185.ChannelAccessSetting );
#endif
	//LZM 090306 usb crash here, mark it temp
	//write_nic_word(dev, SIFS_OFDM, 0x0e0e);
	priv->ieee80211->mode = wireless_mode;

	if ((wireless_mode == WIRELESS_MODE_N_24G) ||  (wireless_mode == WIRELESS_MODE_N_5G))
		priv->ieee80211->pHTInfo->bEnableHT = 1;
	else
		priv->ieee80211->pHTInfo->bEnableHT = 0;
	RT_TRACE(COMP_INIT, "Current Wireless Mode is %x\n", wireless_mode);
	rtl8192_refresh_supportrate(priv);
#endif

}


short rtl8192_is_tx_queue_empty(struct net_device *dev)
{
	int i=0;
	struct r8192_priv *priv = ieee80211_priv(dev);
	//struct ieee80211_device* ieee = priv->ieee80211;
	for (i=0; i<=MGNT_QUEUE; i++)
	{
		if ((i== TXCMD_QUEUE) || (i == HCCA_QUEUE) )
			continue;
		if (atomic_read(&priv->tx_pending[i]))
		{
			printk("===>tx queue is not empty:%d, %d\n", i, atomic_read(&priv->tx_pending[i]));
			return 0;
		}
	}
	return 1;
}

void rtl8192_hw_sleep_down(struct net_device *dev)
{
	RT_TRACE(COMP_POWER, "%s()============>come to sleep down\n", __FUNCTION__);
#ifdef TODO
//	MgntActSet_RF_State(dev, eRfSleep, RF_CHANGE_BY_PS);
#endif
}

void rtl8192_hw_sleep_wq (struct work_struct *work)
{
//      struct r8180_priv *priv = container_of(work, struct r8180_priv, watch_dog_wq);
//      struct ieee80211_device * ieee = (struct ieee80211_device*)
//                                             container_of(work, struct ieee80211_device, watch_dog_wq);
        struct delayed_work *dwork = container_of(work,struct delayed_work,work);
        struct ieee80211_device *ieee = container_of(dwork,struct ieee80211_device,hw_sleep_wq);
        struct net_device *dev = ieee->dev;

	//printk("=========>%s()\n", __FUNCTION__);
        rtl8192_hw_sleep_down(dev);
}
//	printk("dev is %d\n",dev);
//	printk("&*&(^*(&(&=========>%s()\n", __FUNCTION__);
void rtl8192_hw_wakeup(struct net_device* dev)
{
//	u32 flags = 0;

//	spin_lock_irqsave(&priv->ps_lock,flags);
	RT_TRACE(COMP_POWER, "%s()============>come to wake up\n", __FUNCTION__);
#ifdef TODO
//	MgntActSet_RF_State(dev, eRfSleep, RF_CHANGE_BY_PS);
#endif
	//FIXME: will we send package stored while nic is sleep?
//	spin_unlock_irqrestore(&priv->ps_lock,flags);
}

void rtl8192_hw_wakeup_wq (struct work_struct *work)
{
//	struct r8180_priv *priv = container_of(work, struct r8180_priv, watch_dog_wq);
//	struct ieee80211_device * ieee = (struct ieee80211_device*)
//	                                       container_of(work, struct ieee80211_device, watch_dog_wq);
	struct delayed_work *dwork = container_of(work,struct delayed_work,work);
	struct ieee80211_device *ieee = container_of(dwork,struct ieee80211_device,hw_wakeup_wq);
	struct net_device *dev = ieee->dev;

	rtl8192_hw_wakeup(dev);
}

#define MIN_SLEEP_TIME 50
#define MAX_SLEEP_TIME 10000
void rtl8192_hw_to_sleep(struct net_device *dev, u32 th, u32 tl)
{

	struct r8192_priv *priv = ieee80211_priv(dev);

	u32 rb = jiffies;
	unsigned long flags;

	spin_lock_irqsave(&priv->ps_lock,flags);

	/* Writing HW register with 0 equals to disable
	 * the timer, that is not really what we want
	 */
	tl -= MSECS(4+16+7);

	//if(tl == 0) tl = 1;

	/* FIXME HACK FIXME HACK */
//	force_pci_posting(dev);
	//mdelay(1);

//	rb = read_nic_dword(dev, TSFTR);

	/* If the interval in witch we are requested to sleep is too
	 * short then give up and remain awake
	 */
	if(((tl>=rb)&& (tl-rb) <= MSECS(MIN_SLEEP_TIME))
		||((rb>tl)&& (rb-tl) < MSECS(MIN_SLEEP_TIME))) {
		spin_unlock_irqrestore(&priv->ps_lock,flags);
		printk("too short to sleep\n");
		return;
	}

//	write_nic_dword(dev, TimerInt, tl);
//	rb = read_nic_dword(dev, TSFTR);
	{
		u32 tmp = (tl>rb)?(tl-rb):(rb-tl);
	//	if (tl<rb)

		queue_delayed_work(priv->ieee80211->wq, &priv->ieee80211->hw_wakeup_wq, tmp); //as tl may be less than rb
	}
	/* if we suspect the TimerInt is gone beyond tl
	 * while setting it, then give up
	 */
#if 1
	if(((tl > rb) && ((tl-rb) > MSECS(MAX_SLEEP_TIME)))||
		((tl < rb) && ((rb-tl) > MSECS(MAX_SLEEP_TIME)))) {
		printk("========>too long to sleep:%x, %x, %lx\n", tl, rb,  MSECS(MAX_SLEEP_TIME));
		spin_unlock_irqrestore(&priv->ps_lock,flags);
		return;
	}
#endif
//	if(priv->rf_sleep)
//		priv->rf_sleep(dev);

	//printk("<=========%s()\n", __FUNCTION__);
	queue_delayed_work(priv->ieee80211->wq, (void *)&priv->ieee80211->hw_sleep_wq,0);

	spin_unlock_irqrestore(&priv->ps_lock,flags);
}
//init priv variables here. only non_zero value should be initialized here.
static void rtl8192_init_priv_variable(struct net_device* dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8 i;
	priv->card_8192 = NIC_8192U;
	priv->chan = 1; //set to channel 1
	priv->ieee80211->mode = WIRELESS_MODE_AUTO; //SET AUTO
	priv->ieee80211->iw_mode = IW_MODE_INFRA;
	priv->ieee80211->ieee_up=0;
	priv->retry_rts = DEFAULT_RETRY_RTS;
	priv->retry_data = DEFAULT_RETRY_DATA;
	priv->ieee80211->rts = DEFAULT_RTS_THRESHOLD;
	priv->ieee80211->rate = 110; //11 mbps
	priv->ieee80211->short_slot = 1;
	priv->promisc = (dev->flags & IFF_PROMISC) ? 1:0;
	priv->CckPwEnl = 6;
	//for silent reset
	priv->IrpPendingCount = 1;
	priv->ResetProgress = RESET_TYPE_NORESET;
	priv->bForcedSilentReset = 0;
	priv->bDisableNormalResetCheck = false;
	priv->force_reset = false;

	priv->ieee80211->FwRWRF = 0; 	//we don't use FW read/write RF until stable firmware is available.
	priv->ieee80211->current_network.beacon_interval = DEFAULT_BEACONINTERVAL;
	priv->ieee80211->iw_mode = IW_MODE_INFRA;
	priv->ieee80211->softmac_features  = IEEE_SOFTMAC_SCAN |
		IEEE_SOFTMAC_ASSOCIATE | IEEE_SOFTMAC_PROBERQ |
		IEEE_SOFTMAC_PROBERS | IEEE_SOFTMAC_TX_QUEUE |
		IEEE_SOFTMAC_BEACONS;//added by amy 080604 //|  //IEEE_SOFTMAC_SINGLE_QUEUE;

	priv->ieee80211->active_scan = 1;
	priv->ieee80211->modulation = IEEE80211_CCK_MODULATION | IEEE80211_OFDM_MODULATION;
	priv->ieee80211->host_encrypt = 1;
	priv->ieee80211->host_decrypt = 1;
	priv->ieee80211->start_send_beacons = NULL;//rtl819xusb_beacon_tx;//-by amy 080604
	priv->ieee80211->stop_send_beacons = NULL;//rtl8192_beacon_stop;//-by amy 080604
	priv->ieee80211->softmac_hard_start_xmit = rtl8192_hard_start_xmit;
	priv->ieee80211->set_chan = rtl8192_set_chan;
	priv->ieee80211->link_change = priv->ops->rtl819x_link_change;
	priv->ieee80211->softmac_data_hard_start_xmit = rtl8192_hard_data_xmit;
	priv->ieee80211->data_hard_stop = rtl8192_data_hard_stop;
	priv->ieee80211->data_hard_resume = rtl8192_data_hard_resume;
	priv->ieee80211->init_wmmparam_flag = 0;
	priv->ieee80211->fts = DEFAULT_FRAG_THRESHOLD;
	priv->ieee80211->check_nic_enough_desc = check_nic_enough_desc;
	priv->ieee80211->tx_headroom = TX_PACKET_SHIFT_BYTES;
	priv->ieee80211->qos_support = 1;

	//added by WB
//	priv->ieee80211->SwChnlByTimerHandler = rtl8192_phy_SwChnl;
	priv->ieee80211->SetBWModeHandler = rtl8192_SetBWMode;
	priv->ieee80211->handle_assoc_response = rtl8192_handle_assoc_response;
	priv->ieee80211->handle_beacon = rtl8192_handle_beacon;
	//for LPS
	priv->ieee80211->sta_wake_up = rtl8192_hw_wakeup;
//	priv->ieee80211->ps_request_tx_ack = rtl8192_rq_tx_ack;
	priv->ieee80211->enter_sleep_state = rtl8192_hw_to_sleep;
	priv->ieee80211->ps_is_queue_empty = rtl8192_is_tx_queue_empty;
	//added by david
	priv->ieee80211->GetNmodeSupportBySecCfg = GetNmodeSupportBySecCfg8192;
	priv->ieee80211->GetHalfNmodeSupportByAPsHandler = GetHalfNmodeSupportByAPs819xUsb;
	priv->ieee80211->SetWirelessMode = rtl8192_SetWirelessMode;
	//added by amy
	priv->ieee80211->InitialGainHandler = priv->ops->rtl819x_initial_gain;
	priv->card_type = USB;

//1 RTL8192SU/
	priv->ieee80211->current_network.beacon_interval = DEFAULT_BEACONINTERVAL;
	priv->ieee80211->SetFwCmdHandler = HalSetFwCmd8192S;
	priv->bRFSiOrPi = 0;//o=si,1=pi;
	//lzm add
	priv->bInHctTest = false;

	priv->MidHighPwrTHR_L1 = 0x3B;
	priv->MidHighPwrTHR_L2 = 0x40;

	if(priv->bInHctTest)
  	{
		priv->ShortRetryLimit = HAL_RETRY_LIMIT_AP_ADHOC;
		priv->LongRetryLimit = HAL_RETRY_LIMIT_AP_ADHOC;
  	}
	else
	{
		priv->ShortRetryLimit = HAL_RETRY_LIMIT_INFRA;
		priv->LongRetryLimit = HAL_RETRY_LIMIT_INFRA;
	}

	priv->SetFwCmdInProgress = false; //is set FW CMD in Progress? 92S only
	priv->CurrentFwCmdIO = 0;

	priv->MinSpaceCfg = 0;

	priv->EarlyRxThreshold = 7;
	priv->enable_gpio0 = 0;
	priv->TransmitConfig	=
				((u32)TCR_MXDMA_2048<<TCR_MXDMA_OFFSET) |	// Max DMA Burst Size per Tx DMA Burst, 7: reservied.
				(priv->ShortRetryLimit<<TCR_SRL_OFFSET) |	// Short retry limit
				(priv->LongRetryLimit<<TCR_LRL_OFFSET) |	// Long retry limit
				(false ? TCR_SAT : 0);	// FALSE: HW provies PLCP length and LENGEXT, TURE: SW proiveds them
	if(priv->bInHctTest)
		priv->ReceiveConfig	=	//priv->CSMethod |
								RCR_AMF | RCR_ADF |	//RCR_AAP | 	//accept management/data
									RCR_ACF |RCR_APPFCS|						//accept control frame for SW AP needs PS-poll, 2005.07.07, by rcnjko.
								RCR_AB | RCR_AM | RCR_APM |		//accept BC/MC/UC
								RCR_AICV | RCR_ACRC32 | 		//accept ICV/CRC error packet
								RCR_APP_PHYST_STAFF | RCR_APP_PHYST_RXFF |	// Accept PHY status
								((u32)7<<RCR_MXDMA_OFFSET) | // Max DMA Burst Size per Rx DMA Burst, 7: unlimited.
								(priv->EarlyRxThreshold<<RCR_FIFO_OFFSET) | // Rx FIFO Threshold, 7: No Rx threshold.
								(priv->EarlyRxThreshold == 7 ? RCR_OnlyErlPkt:0);
	else
		priv->ReceiveConfig	=	//priv->CSMethod |
									RCR_AMF | RCR_ADF | RCR_AB |
									RCR_AM | RCR_APM |RCR_AAP |RCR_ADD3|RCR_APP_ICV|
								RCR_APP_PHYST_STAFF | RCR_APP_PHYST_RXFF |	// Accept PHY status
									RCR_APP_MIC | RCR_APPFCS;

	// <Roger_EXP> 2008.06.16.
	priv->IntrMask 		= 	(u16)(IMR_ROK | IMR_VODOK | IMR_VIDOK | IMR_BEDOK | IMR_BKDOK |		\
								IMR_HCCADOK | IMR_MGNTDOK | IMR_COMDOK | IMR_HIGHDOK | 					\
								IMR_BDOK | IMR_RXCMDOK | /*IMR_TIMEOUT0 |*/ IMR_RDU | IMR_RXFOVW	|			\
								IMR_TXFOVW | IMR_BcnInt | IMR_TBDOK | IMR_TBDER);

//1 End


	priv->AcmControl = 0;
	priv->pFirmware = (rt_firmware*)vmalloc(sizeof(rt_firmware));
	if (priv->pFirmware)
	memset(priv->pFirmware, 0, sizeof(rt_firmware));

	/* rx related queue */
        skb_queue_head_init(&priv->rx_queue);
	skb_queue_head_init(&priv->skb_queue);

	/* Tx related queue */
	for(i = 0; i < MAX_QUEUE_SIZE; i++) {
		skb_queue_head_init(&priv->ieee80211->skb_waitQ [i]);
	}
	for(i = 0; i < MAX_QUEUE_SIZE; i++) {
		skb_queue_head_init(&priv->ieee80211->skb_aggQ [i]);
	}
	for(i = 0; i < MAX_QUEUE_SIZE; i++) {
		skb_queue_head_init(&priv->ieee80211->skb_drv_aggQ [i]);
	}
	priv->rf_set_chan = rtl8192_phy_SwChnl;
}

//init lock here
static void rtl8192_init_priv_lock(struct r8192_priv* priv)
{
	spin_lock_init(&priv->tx_lock);
	spin_lock_init(&priv->irq_lock);//added by thomas
	//spin_lock_init(&priv->rf_lock);//use rf_sem, or will crash in some OS.
	sema_init(&priv->wx_sem,1);
	sema_init(&priv->rf_sem,1);
	spin_lock_init(&priv->ps_lock);
	mutex_init(&priv->mutex);
}

extern  void    rtl819x_watchdog_wqcallback(struct work_struct *work);

void rtl8192_irq_rx_tasklet(struct r8192_priv *priv);
//init tasklet and wait_queue here. only 2.6 above kernel is considered
#define DRV_NAME "wlan0"
static void rtl8192_init_priv_task(struct net_device* dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

#ifdef PF_SYNCTHREAD
	priv->priv_wq = create_workqueue(DRV_NAME,0);
#else
	priv->priv_wq = create_workqueue(DRV_NAME);
#endif

	INIT_WORK(&priv->reset_wq, rtl8192_restart);

	//INIT_DELAYED_WORK(&priv->watch_dog_wq, hal_dm_watchdog);
	INIT_DELAYED_WORK(&priv->watch_dog_wq, rtl819x_watchdog_wqcallback);
	INIT_DELAYED_WORK(&priv->txpower_tracking_wq,  dm_txpower_trackingcallback);
//	INIT_DELAYED_WORK(&priv->gpio_change_rf_wq,  dm_gpio_change_rf_callback);
	INIT_DELAYED_WORK(&priv->rfpath_check_wq,  dm_rf_pathcheck_workitemcallback);
	INIT_DELAYED_WORK(&priv->update_beacon_wq, rtl8192_update_beacon);
	INIT_DELAYED_WORK(&priv->initialgain_operate_wq, InitialGainOperateWorkItemCallBack);
	//INIT_WORK(&priv->SwChnlWorkItem,  rtl8192_SwChnl_WorkItem);
	//INIT_WORK(&priv->SetBWModeWorkItem,  rtl8192_SetBWModeWorkItem);
	INIT_WORK(&priv->qos_activate, rtl8192_qos_activate);
	INIT_DELAYED_WORK(&priv->ieee80211->hw_wakeup_wq,(void*) rtl8192_hw_wakeup_wq);
	INIT_DELAYED_WORK(&priv->ieee80211->hw_sleep_wq,(void*) rtl8192_hw_sleep_wq);

	tasklet_init(&priv->irq_rx_tasklet,
	     (void(*)(unsigned long))rtl8192_irq_rx_tasklet,
	     (unsigned long)priv);
}

static void rtl8192_get_eeprom_size(struct net_device* dev)
{
	u16 curCR = 0;
	struct r8192_priv *priv = ieee80211_priv(dev);
	RT_TRACE(COMP_EPROM, "===========>%s()\n", __FUNCTION__);
	curCR = read_nic_word_E(dev,EPROM_CMD);
	RT_TRACE(COMP_EPROM, "read from Reg EPROM_CMD(%x):%x\n", EPROM_CMD, curCR);
	//whether need I consider BIT5?
	priv->epromtype = (curCR & Cmd9346CR_9356SEL) ? EPROM_93c56 : EPROM_93c46;
	RT_TRACE(COMP_EPROM, "<===========%s(), epromtype:%d\n", __FUNCTION__, priv->epromtype);
}

//used to swap endian. as ntohl & htonl are not neccessary to swap endian, so use this instead.
static inline u16 endian_swap(u16* data)
{
	u16 tmp = *data;
	*data = (tmp >> 8) | (tmp << 8);
	return *data;
}

u8 rtl8192SU_UsbOptionToEndPointNumber(u8 UsbOption)
{
	u8	nEndPoint = 0;
	switch(UsbOption)
	{
		case 0:
			nEndPoint = 6;
			break;
		case 1:
			nEndPoint = 11;
			break;
		case 2:
			nEndPoint = 4;
			break;
		default:
			RT_TRACE(COMP_INIT, "UsbOptionToEndPointNumber(): Invalid UsbOption(%#x)\n", UsbOption);
			break;
	}
	return nEndPoint;
}

u8 rtl8192SU_BoardTypeToRFtype(struct net_device* dev,  u8 Boardtype)
{
	u8	RFtype = RF_1T2R;

	switch(Boardtype)
	{
		case 0:
			RFtype = RF_1T1R;
			break;
		case 1:
			RFtype = RF_1T2R;
			break;
		case 2:
			RFtype = RF_2T2R;
			break;
		case 3:
			RFtype = RF_2T2R_GREEN;
			break;
		default:
			break;
	}

	return RFtype;
}

//
//	Description:
//		Config HW adapter information into initial value.
//
//	Assumption:
//		1. After Auto load fail(i.e, check CR9346 fail)
//
//	Created by Roger, 2008.10.21.
//
void
rtl8192SU_ConfigAdapterInfo8192SForAutoLoadFail(struct net_device* dev)
{
	struct r8192_priv 	*priv = ieee80211_priv(dev);
	//u16			i,usValue;
	//u8 sMacAddr[6] = {0x00, 0xE0, 0x4C, 0x81, 0x92, 0x00};
	u8		rf_path, index;	// For EEPROM/EFUSE After V0.6_1117
	int	i;

	RT_TRACE(COMP_INIT, "====> ConfigAdapterInfo8192SForAutoLoadFail\n");

	write_nic_byte(dev, SYS_ISO_CTRL+1, 0xE8); // Isolation signals from Loader
	//PlatformStallExecution(10000);
	mdelay(10);
	write_nic_byte(dev, PMC_FSM, 0x02); // Enable Loader Data Keep

	//RT_ASSERT(priv->AutoloadFailFlag==TRUE, ("ReadAdapterInfo8192SEEPROM(): AutoloadFailFlag !=TRUE\n"));

	// Initialize IC Version && Channel Plan
	priv->eeprom_vid = 0;
	priv->eeprom_pid = 0;
	priv->card_8192_version = 0;
	priv->eeprom_ChannelPlan = 0;
	priv->eeprom_CustomerID = 0;
	priv->eeprom_SubCustomerID = 0;
	priv->bIgnoreDiffRateTxPowerOffset = false;

	RT_TRACE(COMP_INIT, "EEPROM VID = 0x%4x\n", priv->eeprom_vid);
	RT_TRACE(COMP_INIT, "EEPROM PID = 0x%4x\n", priv->eeprom_pid);
	RT_TRACE(COMP_INIT, "EEPROM Customer ID: 0x%2x\n", priv->eeprom_CustomerID);
	RT_TRACE(COMP_INIT, "EEPROM SubCustomer ID: 0x%2x\n", priv->eeprom_SubCustomerID);
	RT_TRACE(COMP_INIT, "EEPROM ChannelPlan = 0x%4x\n", priv->eeprom_ChannelPlan);
	RT_TRACE(COMP_INIT, "IgnoreDiffRateTxPowerOffset = %d\n", priv->bIgnoreDiffRateTxPowerOffset);



	priv->EEPROMUsbOption = EEPROM_USB_Default_OPTIONAL_FUNC;
	RT_TRACE(COMP_INIT, "USB Option = %#x\n", priv->EEPROMUsbOption);

	for(i=0; i<5; i++)
		priv->EEPROMUsbPhyParam[i] = EEPROM_USB_Default_PHY_PARAM;

	//RT_PRINT_DATA(COMP_INIT|COMP_EFUSE, DBG_LOUD, ("EFUSE USB PHY Param: \n"), priv->EEPROMUsbPhyParam, 5);

	{
	//<Roger_Notes> In this case, we random assigh MAC address here. 2008.10.15.
		static u8 sMacAddr[6] = {0x00, 0xE0, 0x4C, 0x81, 0x92, 0x00};
		u8	i;

        	//sMacAddr[5] = (u8)GetRandomNumber(1, 254);

		for(i = 0; i < 6; i++)
			dev->dev_addr[i] = sMacAddr[i];
	}
	//NicIFSetMacAddress(Adapter, Adapter->PermanentAddress);
	write_nic_dword(dev, IDR0, ((u32*)dev->dev_addr)[0]);
	write_nic_word(dev, IDR4, ((u16*)(dev->dev_addr + 4))[0]);

	RT_TRACE(COMP_INIT, "ReadAdapterInfo8192SEFuse(), Permanent Address = %02x-%02x-%02x-%02x-%02x-%02x\n",
			dev->dev_addr[0], dev->dev_addr[1],
			dev->dev_addr[2], dev->dev_addr[3],
			dev->dev_addr[4], dev->dev_addr[5]);

	priv->EEPROMBoardType = EEPROM_Default_BoardType;
	priv->rf_type = RF_1T2R; //RF_2T2R
	priv->EEPROMTxPowerDiff = EEPROM_Default_PwDiff;
	priv->EEPROMThermalMeter = EEPROM_Default_ThermalMeter;
	priv->EEPROMCrystalCap = EEPROM_Default_CrystalCap;
	priv->EEPROMTxPwrBase = EEPROM_Default_TxPowerBase;
	priv->EEPROMTSSI_A = EEPROM_Default_TSSI;
	priv->EEPROMTSSI_B = EEPROM_Default_TSSI;
	priv->EEPROMTxPwrTkMode = EEPROM_Default_TxPwrTkMode;



	for (rf_path = 0; rf_path < 2; rf_path++)
	{
		for (i = 0; i < 3; i++)
		{
			// Read CCK RF A & B Tx power
			priv->RfCckChnlAreaTxPwr[rf_path][i] =
			priv->RfOfdmChnlAreaTxPwr1T[rf_path][i] =
			priv->RfOfdmChnlAreaTxPwr2T[rf_path][i] =
			(u8)(EEPROM_Default_TxPower & 0xff);
		}
	}

	for (i = 0; i < 3; i++)
	{
		//RT_TRACE((COMP_EFUSE), "CCK RF-%d CHan_Area-%d = 0x%x\n",  rf_path, i,
		//priv->RfCckChnlAreaTxPwr[rf_path][i]);
		//RT_TRACE((COMP_EFUSE), "OFDM-1T RF-%d CHan_Area-%d = 0x%x\n",  rf_path, i,
		//priv->RfOfdmChnlAreaTxPwr1T[rf_path][i]);
		//RT_TRACE((COMP_EFUSE), "OFDM-2T RF-%d CHan_Area-%d = 0x%x\n",  rf_path, i,
		//priv->RfOfdmChnlAreaTxPwr2T[rf_path][i]);
	}

	// Assign dedicated channel tx power
	for(i=0; i<14; i++)	// channel 1~3 use the same Tx Power Level.
		{
		if (i < 3)			// Cjanel 1-3
			index = 0;
		else if (i < 9)		// Channel 4-9
			index = 1;
		else				// Channel 10-14
			index = 2;

		// Record A & B CCK /OFDM - 1T/2T Channel area tx power
		priv->RfTxPwrLevelCck[rf_path][i]  =
		priv->RfCckChnlAreaTxPwr[rf_path][index];
		priv->RfTxPwrLevelOfdm1T[rf_path][i]  =
		priv->RfOfdmChnlAreaTxPwr1T[rf_path][index];
		priv->RfTxPwrLevelOfdm2T[rf_path][i]  =
		priv->RfOfdmChnlAreaTxPwr2T[rf_path][index];
		}

		for(i=0; i<14; i++)
		{
		//RT_TRACE((COMP_EFUSE), "Rf-%d TxPwr CH-%d CCK OFDM_1T OFDM_2T= 0x%x/0x%x/0x%x\n",
		//rf_path, i, priv->RfTxPwrLevelCck[0][i],
		//priv->RfTxPwrLevelOfdm1T[0][i] ,
		//priv->RfTxPwrLevelOfdm2T[0][i] );
		}

	//
	// Update remained HAL variables.
	//
	priv->TSSI_13dBm = priv->EEPROMThermalMeter *100;
	priv->LegacyHTTxPowerDiff = priv->EEPROMTxPowerDiff;//new
	priv->TxPowerDiff = priv->EEPROMTxPowerDiff;
	//priv->AntennaTxPwDiff[0] = (priv->EEPROMTxPowerDiff & 0xf);// Antenna B gain offset to antenna A, bit0~3
	//priv->AntennaTxPwDiff[1] = ((priv->EEPROMTxPowerDiff & 0xf0)>>4);// Antenna C gain offset to antenna A, bit4~7
	priv->CrystalCap = priv->EEPROMCrystalCap;	// CrystalCap, bit12~15
	priv->ThermalMeter[0] = priv->EEPROMThermalMeter;// ThermalMeter, bit0~3 for RFIC1, bit4~7 for RFIC2
	priv->LedStrategy = SW_LED_MODE0;

	init_rate_adaptive(dev);

	RT_TRACE(COMP_INIT, "<==== ConfigAdapterInfo8192SForAutoLoadFail\n");

}

//
//	Description:
//		Read HW adapter information by E-Fuse or EEPROM according CR9346 reported.
//
//	Assumption:
//		1. CR9346 regiser has verified.
//		2. PASSIVE_LEVEL (USB interface)
//
//	Created by Roger, 2008.10.21.
//
void
rtl8192SU_ReadAdapterInfo8192SUsb(struct net_device* dev)
{
	struct r8192_priv 	*priv = ieee80211_priv(dev);
	u16			i,usValue;
	u8			tmpU1b, tempval;
	u16			EEPROMId;
	u8			hwinfo[HWSET_MAX_SIZE_92S];
	u8			rf_path, index;	// For EEPROM/EFUSE After V0.6_1117


	RT_TRACE(COMP_INIT, "====> ReadAdapterInfo8192SUsb\n");

	//
	// <Roger_Note> The following operation are prevent Efuse leakage by turn on 2.5V.
	// 2008.11.25.
	//
	tmpU1b = read_nic_byte(dev, EFUSE_TEST+3);
	write_nic_byte(dev, EFUSE_TEST+3, tmpU1b|0x80);
	//PlatformStallExecution(1000);
	mdelay(10);
	write_nic_byte(dev, EFUSE_TEST+3, (tmpU1b&(~BIT7)));

	// Retrieve Chip version.
	priv->card_8192_version = (VERSION_8192S)((read_nic_dword(dev, PMC_FSM)>>16)&0xF);
	RT_TRACE(COMP_INIT, "Chip Version ID: 0x%2x\n", priv->card_8192_version);

	switch(priv->card_8192_version)
	{
		case 0:
			RT_TRACE(COMP_INIT, "Chip Version ID: VERSION_8192S_ACUT.\n");
			break;
		case 1:
			RT_TRACE(COMP_INIT, "Chip Version ID: VERSION_8192S_BCUT.\n");
			break;
		case 2:
			RT_TRACE(COMP_INIT, "Chip Version ID: VERSION_8192S_CCUT.\n");
			break;
		default:
			RT_TRACE(COMP_INIT, "Unknown Chip Version!!\n");
			priv->card_8192_version = VERSION_8192S_BCUT;
			break;
	}

	//if (IS_BOOT_FROM_EEPROM(Adapter))
	if(priv->EepromOrEfuse)
	{	// Read frin EEPROM
		write_nic_byte(dev, SYS_ISO_CTRL+1, 0xE8); // Isolation signals from Loader
		//PlatformStallExecution(10000);
		mdelay(10);
		write_nic_byte(dev, PMC_FSM, 0x02); // Enable Loader Data Keep
		// Read all Content from EEPROM or EFUSE.
		for(i = 0; i < HWSET_MAX_SIZE_92S; i += 2)
		{
			usValue = eprom_read(dev, (u16) (i>>1));
			*((u16*)(&hwinfo[i])) = usValue;
		}
	}
	else if (!(priv->EepromOrEfuse))
	{	// Read from EFUSE

		//
		// <Roger_Notes> We set Isolation signals from Loader and reset EEPROM after system resuming
		// from suspend mode.
		// 2008.10.21.
		//
		//PlatformEFIOWrite1Byte(Adapter, SYS_ISO_CTRL+1, 0xE8); // Isolation signals from Loader
		//PlatformStallExecution(10000);
		//PlatformEFIOWrite1Byte(Adapter, SYS_FUNC_EN+1, 0x40);
		//PlatformEFIOWrite1Byte(Adapter, SYS_FUNC_EN+1, 0x50);

		//tmpU1b = PlatformEFIORead1Byte(Adapter, EFUSE_TEST+3);
		//PlatformEFIOWrite1Byte(Adapter, EFUSE_TEST+3, (tmpU1b | 0x80));
		//PlatformEFIOWrite1Byte(Adapter, EFUSE_TEST+3, 0x72);
		//PlatformEFIOWrite1Byte(Adapter, EFUSE_CLK, 0x03);

		// Read EFUSE real map to shadow.
		EFUSE_ShadowMapUpdate(dev);
		memcpy(hwinfo, &priv->EfuseMap[EFUSE_INIT_MAP][0], HWSET_MAX_SIZE_92S);
	}
	else
	{
		RT_TRACE(COMP_INIT, "ReadAdapterInfo8192SUsb(): Invalid boot type!!\n");
	}

	//YJ,test,090106
	//dump_buf(hwinfo,HWSET_MAX_SIZE_92S);
	//
	// <Roger_Notes> The following are EFUSE/EEPROM independent operations!!
	//
	//RT_PRINT_DATA(COMP_EFUSE, DBG_LOUD, ("MAP: \n"), hwinfo, HWSET_MAX_SIZE_92S);

	//
	// <Roger_Notes> Event though CR9346 regiser can verify whether Autoload is success or not, but we still
	// double check ID codes for 92S here(e.g., due to HW GPIO polling fail issue).
	// 2008.10.21.
	//
	EEPROMId = *((u16 *)&hwinfo[0]);

	if( EEPROMId != RTL8190_EEPROM_ID )
	{
		RT_TRACE(COMP_INIT, "ID(%#x) is invalid!!\n", EEPROMId);
		priv->bTXPowerDataReadFromEEPORM = FALSE;
		priv->AutoloadFailFlag=TRUE;
	}
	else
	{
		priv->AutoloadFailFlag=FALSE;
		priv->bTXPowerDataReadFromEEPORM = TRUE;
	}
       // Read IC Version && Channel Plan
	if(!priv->AutoloadFailFlag)
	{
        	// VID, PID
	    	priv->eeprom_vid = *(u16 *)&hwinfo[EEPROM_VID];
		priv->eeprom_pid = *(u16 *)&hwinfo[EEPROM_PID];
		priv->bIgnoreDiffRateTxPowerOffset = false;	//cosa for test


		// EEPROM Version ID, Channel plan
		priv->EEPROMVersion = *(u8 *)&hwinfo[EEPROM_Version];
		priv->eeprom_ChannelPlan = *(u8 *)&hwinfo[EEPROM_ChannelPlan];

		// Customer ID, 0x00 and 0xff are reserved for Realtek.
		priv->eeprom_CustomerID = *(u8 *)&hwinfo[EEPROM_CustomID];
		priv->eeprom_SubCustomerID = *(u8 *)&hwinfo[EEPROM_SubCustomID];
	}
	else
	{
		//priv->eeprom_vid = 0;
		//priv->eeprom_pid = 0;
		//priv->EEPROMVersion = 0;
		//priv->eeprom_ChannelPlan = 0;
		//priv->eeprom_CustomerID = 0;
		//priv->eeprom_SubCustomerID = 0;

		rtl8192SU_ConfigAdapterInfo8192SForAutoLoadFail(dev);
		return;
	}


	RT_TRACE(COMP_INIT, "EEPROM Id = 0x%4x\n", EEPROMId);
	RT_TRACE(COMP_INIT, "EEPROM VID = 0x%4x\n", priv->eeprom_vid);
	RT_TRACE(COMP_INIT, "EEPROM PID = 0x%4x\n", priv->eeprom_pid);
	RT_TRACE(COMP_INIT, "EEPROM Version ID: 0x%2x\n", priv->EEPROMVersion);
	RT_TRACE(COMP_INIT, "EEPROM Customer ID: 0x%2x\n", priv->eeprom_CustomerID);
	RT_TRACE(COMP_INIT, "EEPROM SubCustomer ID: 0x%2x\n", priv->eeprom_SubCustomerID);
	RT_TRACE(COMP_INIT, "EEPROM ChannelPlan = 0x%4x\n", priv->eeprom_ChannelPlan);
	RT_TRACE(COMP_INIT, "bIgnoreDiffRateTxPowerOffset = %d\n", priv->bIgnoreDiffRateTxPowerOffset);


	// Read USB optional function.
	if(!priv->AutoloadFailFlag)
	{
		priv->EEPROMUsbOption = *(u8 *)&hwinfo[EEPROM_USB_OPTIONAL];
	}
	else
	{
		priv->EEPROMUsbOption = EEPROM_USB_Default_OPTIONAL_FUNC;
	}


	priv->EEPROMUsbEndPointNumber = rtl8192SU_UsbOptionToEndPointNumber((priv->EEPROMUsbOption&EEPROM_EP_NUMBER)>>3);

	RT_TRACE(COMP_INIT, "USB Option = %#x\n", priv->EEPROMUsbOption);
	RT_TRACE(COMP_INIT, "EndPoint Number = %#x\n", priv->EEPROMUsbEndPointNumber);

#ifdef TO_DO_LIST
	//
	//  Decide CustomerID according to VID/DID or EEPROM
	//
	switch(pHalData->EEPROMCustomerID)
	{
		case EEPROM_CID_ALPHA:
			pMgntInfo->CustomerID = RT_CID_819x_ALPHA;
			break;

		case EEPROM_CID_CAMEO:
			pMgntInfo->CustomerID = RT_CID_819x_CAMEO;
			break;

		case EEPROM_CID_SITECOM:
			pMgntInfo->CustomerID = RT_CID_819x_Sitecom;
			RT_TRACE(COMP_INIT, DBG_LOUD, ("CustomerID = 0x%4x\n", pMgntInfo->CustomerID));

			break;

		case EEPROM_CID_WHQL:
			Adapter->bInHctTest = TRUE;

			pMgntInfo->bSupportTurboMode = FALSE;
			pMgntInfo->bAutoTurboBy8186 = FALSE;

			pMgntInfo->PowerSaveControl.bInactivePs = FALSE;
			pMgntInfo->PowerSaveControl.bIPSModeBackup = FALSE;
			pMgntInfo->PowerSaveControl.bLeisurePs = FALSE;
			pMgntInfo->keepAliveLevel = 0;
			break;

		default:
			pMgntInfo->CustomerID = RT_CID_DEFAULT;
			break;

	}

	//
	// Led mode
	//
	switch(pMgntInfo->CustomerID)
	{
		case RT_CID_DEFAULT:
		case RT_CID_819x_ALPHA:
			pHalData->LedStrategy = SW_LED_MODE1;
			pHalData->bRegUseLed = TRUE;
			pHalData->SwLed1.bLedOn = TRUE;
			break;
		case RT_CID_819x_CAMEO:
			pHalData->LedStrategy = SW_LED_MODE1;
			pHalData->bRegUseLed = TRUE;
			break;

		case RT_CID_819x_Sitecom:
			pHalData->LedStrategy = SW_LED_MODE2;
			pHalData->bRegUseLed = TRUE;
			break;

		default:
			pHalData->LedStrategy = SW_LED_MODE0;
			break;
	}
#endif

	// Read USB PHY parameters.
	for(i=0; i<5; i++)
		priv->EEPROMUsbPhyParam[i] = *(u8 *)&hwinfo[EEPROM_USB_PHY_PARA1+i];

	//RT_PRINT_DATA(COMP_EFUSE, DBG_LOUD, ("USB PHY Param: \n"), pHalData->EEPROMUsbPhyParam, 5);


       //Read Permanent MAC address
	for(i=0; i<6; i++)
		dev->dev_addr[i] =  *(u8 *)&hwinfo[EEPROM_NODE_ADDRESS_BYTE_0+i];

	//NicIFSetMacAddress(Adapter, Adapter->PermanentAddress);
	write_nic_dword(dev, IDR0, ((u32*)dev->dev_addr)[0]);
	write_nic_word(dev, IDR4, ((u16*)(dev->dev_addr + 4))[0]);

	RT_TRACE(COMP_INIT, "ReadAdapterInfo8192SEFuse(), Permanent Address = %02x-%02x-%02x-%02x-%02x-%02x\n",
			dev->dev_addr[0], dev->dev_addr[1],
			dev->dev_addr[2], dev->dev_addr[3],
			dev->dev_addr[4], dev->dev_addr[5]);

	//
	// Get CustomerID(Boad Type)
	// i.e., 0x0: RTL8188SU, 0x1: RTL8191SU, 0x2: RTL8192SU, 0x3: RTL8191GU.
	// Others: Reserved. Default is 0x2: RTL8192SU.
	//
	//if(!priv->AutoloadFailFlag)
	//{
		priv->EEPROMBoardType = *(u8 *)&hwinfo[EEPROM_BoardType];
		priv->rf_type = rtl8192SU_BoardTypeToRFtype(dev, priv->EEPROMBoardType);
	//}
	//else
	//{
	//	priv->EEPROMBoardType = EEPROM_Default_BoardType;
	//	priv->rf_type = RF_1T2R;
	//}

	priv->rf_chip = RF_6052;

	priv->rf_chip = RF_6052;//lzm test
	RT_TRACE(COMP_INIT, "BoardType = 0x%2x\n", priv->EEPROMBoardType);
	RT_TRACE(COMP_INIT, "RF_Type = 0x%2x\n", priv->rf_type);

	//
	// Read antenna tx power offset of B/C/D to A  from EEPROM
	// and read ThermalMeter from EEPROM
	//
	//if(!priv->AutoloadFailFlag)
	{
		priv->EEPROMTxPowerDiff = *(u8 *)&hwinfo[EEPROM_PwDiff];
		priv->EEPROMThermalMeter = *(u8 *)&hwinfo[EEPROM_ThermalMeter];
	}
	//else
	//{
	//	priv->EEPROMTxPowerDiff = EEPROM_Default_PwDiff;
	//	priv->EEPROMThermalMeter = EEPROM_Default_ThermalMeter;
	//}

	RT_TRACE(COMP_INIT, "PwDiff = %#x\n", priv->EEPROMTxPowerDiff);
	RT_TRACE(COMP_INIT, "ThermalMeter = %#x\n", priv->EEPROMThermalMeter);

	//
	// Read Tx Power gain offset of legacy OFDM to HT rate.
	// Read CrystalCap from EEPROM
	//
	//if(!priv->AutoloadFailFlag)
	{
		priv->EEPROMCrystalCap = *(u8 *)&hwinfo[EEPROM_CrystalCap];
	}
	//else
	//{
	//	priv->EEPROMCrystalCap = EEPROM_Default_CrystalCap;
	//}

	RT_TRACE(COMP_INIT, "CrystalCap = %#x\n", priv->EEPROMCrystalCap);

	//
	// Get Tx Power Base.
	//
	//if(!priv->AutoloadFailFlag)
	{
		priv->EEPROMTxPwrBase = *(u8 *)&hwinfo[EEPROM_TxPowerBase];
	}
	//else
	//{
	//	priv->EEPROMTxPwrBase = EEPROM_Default_TxPowerBase;
	//}

	RT_TRACE(COMP_INIT, "TxPwrBase = %#x\n", priv->EEPROMTxPwrBase);


	//
	// Get TSSI value for each path.
	//
	//if(!priv->AutoloadFailFlag)
	{
		priv->EEPROMTSSI_A = *(u8 *)&hwinfo[EEPROM_TSSI_A];
		priv->EEPROMTSSI_B = *(u8 *)&hwinfo[EEPROM_TSSI_B];
	}
	//else
	//{ // Default setting for Empty EEPROM
	//	priv->EEPROMTSSI_A = EEPROM_Default_TSSI;
	//	priv->EEPROMTSSI_B = EEPROM_Default_TSSI;
	//}

	RT_TRACE(COMP_INIT, "TSSI_A = %#x, TSSI_B = %#x\n", priv->EEPROMTSSI_A, priv->EEPROMTSSI_B);

	//
	// Get Tx Power tracking mode.
	//
	//if(!priv->AutoloadFailFlag)
	{
		priv->EEPROMTxPwrTkMode = *(u8 *)&hwinfo[EEPROM_TxPwTkMode];
	}

	RT_TRACE(COMP_INIT, "TxPwrTkMod = %#x\n", priv->EEPROMTxPwrTkMode);


	{
		//
		// Buffer TxPwIdx(i.e., from offset 0x55~0x66, total 18Bytes)
		// Update CCK, OFDM (1T/2T)Tx Power Index from above buffer.
		//

		//
		// Get Tx Power Level by Channel
		//
		//if(!priv->AutoloadFailFlag)
		{
			// Read Tx power of Channel 1 ~ 14 from EFUSE.
			// 92S suupport RF A & B
			for (rf_path = 0; rf_path < 2; rf_path++)
			{
				for (i = 0; i < 3; i++)
				{
					// Read CCK RF A & B Tx power
					priv->RfCckChnlAreaTxPwr[rf_path][i] =
					hwinfo[EEPROM_TxPwIndex+rf_path*3+i];

					// Read OFDM RF A & B Tx power for 1T
					priv->RfOfdmChnlAreaTxPwr1T[rf_path][i] =
					hwinfo[EEPROM_TxPwIndex+6+rf_path*3+i];

					// Read OFDM RF A & B Tx power for 2T
					priv->RfOfdmChnlAreaTxPwr2T[rf_path][i] =
					hwinfo[EEPROM_TxPwIndex+12+rf_path*3+i];
				}
			}

		}
//
		// Update Tx Power HAL variables.
//
		for (rf_path = 0; rf_path < 2; rf_path++)
		{
			for (i = 0; i < 3; i++)
			{
				RT_TRACE((COMP_INIT),  "CCK RF-%d CHan_Area-%d = 0x%x\n",  rf_path, i,
				priv->RfCckChnlAreaTxPwr[rf_path][i]);
				RT_TRACE((COMP_INIT), "OFDM-1T RF-%d CHan_Area-%d = 0x%x\n",  rf_path, i,
				priv->RfOfdmChnlAreaTxPwr1T[rf_path][i]);
				RT_TRACE((COMP_INIT), "OFDM-2T RF-%d CHan_Area-%d = 0x%x\n",  rf_path, i, priv->RfOfdmChnlAreaTxPwr2T[rf_path][i]);
			}

			// Assign dedicated channel tx power
			for(i=0; i<14; i++)	// channel 1~3 use the same Tx Power Level.
			{
				if (i < 3)			// Cjanel 1-3
					index = 0;
				else if (i < 9)		// Channel 4-9
					index = 1;
				else				// Channel 10-14
					index = 2;

				// Record A & B CCK /OFDM - 1T/2T Channel area tx power
				priv->RfTxPwrLevelCck[rf_path][i]  =
				priv->RfCckChnlAreaTxPwr[rf_path][index];
				priv->RfTxPwrLevelOfdm1T[rf_path][i]  =
				priv->RfOfdmChnlAreaTxPwr1T[rf_path][index];
				priv->RfTxPwrLevelOfdm2T[rf_path][i]  =
				priv->RfOfdmChnlAreaTxPwr2T[rf_path][index];
				if (rf_path == 0)
				{
					priv->TxPowerLevelOFDM24G[i] = priv->RfTxPwrLevelOfdm1T[rf_path][i] ;
					priv->TxPowerLevelCCK[i] = priv->RfTxPwrLevelCck[rf_path][i];
				}
			}

			for(i=0; i<14; i++)
			{
				RT_TRACE((COMP_INIT),
				"Rf-%d TxPwr CH-%d CCK OFDM_1T OFDM_2T= 0x%x/0x%x/0x%x\n",
				rf_path, i, priv->RfTxPwrLevelCck[rf_path][i],
				priv->RfTxPwrLevelOfdm1T[rf_path][i] ,
				priv->RfTxPwrLevelOfdm2T[rf_path][i] );
			}
		}
	}

	//
	// 2009/02/09 Cosa add for new EEPROM format
	//
	for(i=0; i<14; i++)	// channel 1~3 use the same Tx Power Level.
	{
		// Read tx power difference between HT OFDM 20/40 MHZ
		if (i < 3)			// Cjanel 1-3
			index = 0;
		else if (i < 9)		// Channel 4-9
			index = 1;
		else				// Channel 10-14
			index = 2;

		tempval = (*(u8 *)&hwinfo[EEPROM_TX_PWR_HT20_DIFF+index])&0xff;
		priv->TxPwrHt20Diff[RF90_PATH_A][i] = (tempval&0xF);
		priv->TxPwrHt20Diff[RF90_PATH_B][i] = ((tempval>>4)&0xF);

		// Read OFDM<->HT tx power diff
		if (i < 3)			// Cjanel 1-3
			tempval = (*(u8 *)&hwinfo[EEPROM_TX_PWR_OFDM_DIFF])&0xff;
		else if (i < 9)		// Channel 4-9
			tempval = (*(u8 *)&hwinfo[EEPROM_PwDiff])&0xff;
		else				// Channel 10-14
			tempval = (*(u8 *)&hwinfo[EEPROM_TX_PWR_OFDM_DIFF+1])&0xff;

		//cosa tempval = (*(u1Byte *)&hwinfo[EEPROM_TX_PWR_OFDM_DIFF+index])&0xff;
		priv->TxPwrLegacyHtDiff[RF90_PATH_A][i] = (tempval&0xF);
		priv->TxPwrLegacyHtDiff[RF90_PATH_B][i] = ((tempval>>4)&0xF);

		//
		// Read Band Edge tx power offset and check if user enable the ability
		//
		// HT 40 band edge channel
		tempval = (*(u8 *)&hwinfo[EEPROM_TX_PWR_BAND_EDGE])&0xff;
		priv->TxPwrbandEdgeHt40[RF90_PATH_A][0] = (tempval&0xF); 		// Band edge low channel
		priv->TxPwrbandEdgeHt40[RF90_PATH_A][1] =  ((tempval>>4)&0xF);	// Band edge high channel
		tempval = (*(u8 *)&hwinfo[EEPROM_TX_PWR_BAND_EDGE+1])&0xff;
		priv->TxPwrbandEdgeHt40[RF90_PATH_B][0] = (tempval&0xF); 		// Band edge low channel
		priv->TxPwrbandEdgeHt40[RF90_PATH_B][1] =  ((tempval>>4)&0xF);	// Band edge high channel
		// HT 20 band edge channel
		tempval = (*(u8 *)&hwinfo[EEPROM_TX_PWR_BAND_EDGE+2])&0xff;
		priv->TxPwrbandEdgeHt20[RF90_PATH_A][0] = (tempval&0xF); 		// Band edge low channel
		priv->TxPwrbandEdgeHt20[RF90_PATH_A][1] =  ((tempval>>4)&0xF);	// Band edge high channel
		tempval = (*(u8 *)&hwinfo[EEPROM_TX_PWR_BAND_EDGE+3])&0xff;
		priv->TxPwrbandEdgeHt20[RF90_PATH_B][0] = (tempval&0xF); 		// Band edge low channel
		priv->TxPwrbandEdgeHt20[RF90_PATH_B][1] =  ((tempval>>4)&0xF);	// Band edge high channel
		// OFDM band edge channel
		tempval = (*(u8 *)&hwinfo[EEPROM_TX_PWR_BAND_EDGE+4])&0xff;
		priv->TxPwrbandEdgeLegacyOfdm[RF90_PATH_A][0] = (tempval&0xF); 		// Band edge low channel
		priv->TxPwrbandEdgeLegacyOfdm[RF90_PATH_A][1] =  ((tempval>>4)&0xF);	// Band edge high channel
		tempval = (*(u8 *)&hwinfo[EEPROM_TX_PWR_BAND_EDGE+5])&0xff;
		priv->TxPwrbandEdgeLegacyOfdm[RF90_PATH_B][0] = (tempval&0xF); 		// Band edge low channel
		priv->TxPwrbandEdgeLegacyOfdm[RF90_PATH_B][1] =  ((tempval>>4)&0xF);	// Band edge high channel

		priv->TxPwrbandEdgeFlag = (*(u8 *)&hwinfo[TX_PWR_BAND_EDGE_CHK]);
	}

	for(i=0; i<14; i++)
		RT_TRACE(COMP_INIT, "RF-A Ht20 to HT40 Diff[%d] = 0x%x\n", i, priv->TxPwrHt20Diff[RF90_PATH_A][i]);
	for(i=0; i<14; i++)
		RT_TRACE(COMP_INIT,  "RF-A Legacy to Ht40 Diff[%d] = 0x%x\n", i, priv->TxPwrLegacyHtDiff[RF90_PATH_A][i]);
	for(i=0; i<14; i++)
		RT_TRACE(COMP_INIT,  "RF-B Ht20 to HT40 Diff[%d] = 0x%x\n", i, priv->TxPwrHt20Diff[RF90_PATH_B][i]);
	for(i=0; i<14; i++)
		RT_TRACE(COMP_INIT,  "RF-B Legacy to HT40 Diff[%d] = 0x%x\n", i, priv->TxPwrLegacyHtDiff[RF90_PATH_B][i]);
	RT_TRACE(COMP_INIT, "RF-A HT40 band-edge low/high power diff = 0x%x/0x%x\n",
		priv->TxPwrbandEdgeHt40[RF90_PATH_A][0],
		priv->TxPwrbandEdgeHt40[RF90_PATH_A][1]);
	RT_TRACE((COMP_INIT&COMP_DBG), "RF-B HT40 band-edge low/high power diff = 0x%x/0x%x\n",
		priv->TxPwrbandEdgeHt40[RF90_PATH_B][0],
		priv->TxPwrbandEdgeHt40[RF90_PATH_B][1]);

	RT_TRACE((COMP_INIT&COMP_DBG), "RF-A HT20 band-edge low/high power diff = 0x%x/0x%x\n",
		priv->TxPwrbandEdgeHt20[RF90_PATH_A][0],
		priv->TxPwrbandEdgeHt20[RF90_PATH_A][1]);
	RT_TRACE((COMP_INIT&COMP_DBG), "RF-B HT20 band-edge low/high power diff = 0x%x/0x%x\n",
		priv->TxPwrbandEdgeHt20[RF90_PATH_B][0],
		priv->TxPwrbandEdgeHt20[RF90_PATH_B][1]);

	RT_TRACE((COMP_INIT&COMP_DBG), "RF-A OFDM band-edge low/high power diff = 0x%x/0x%x\n",
		priv->TxPwrbandEdgeLegacyOfdm[RF90_PATH_A][0],
		priv->TxPwrbandEdgeLegacyOfdm[RF90_PATH_A][1]);
	RT_TRACE((COMP_INIT&COMP_DBG), "RF-B OFDM band-edge low/high power diff = 0x%x/0x%x\n",
		priv->TxPwrbandEdgeLegacyOfdm[RF90_PATH_B][0],
		priv->TxPwrbandEdgeLegacyOfdm[RF90_PATH_B][1]);
	RT_TRACE((COMP_INIT&COMP_DBG), "Band-edge enable flag = %d\n", priv->TxPwrbandEdgeFlag);

	//
	// Update remained HAL variables.
	//
	priv->TSSI_13dBm = priv->EEPROMThermalMeter *100;
	priv->LegacyHTTxPowerDiff = priv->EEPROMTxPowerDiff;
	priv->TxPowerDiff = priv->EEPROMTxPowerDiff;
	//priv->AntennaTxPwDiff[0] = (priv->EEPROMTxPowerDiff & 0xf);// Antenna B gain offset to antenna A, bit[3:0]
	//priv->AntennaTxPwDiff[1] = ((priv->EEPROMTxPowerDiff & 0xf0)>>4);// Antenna C gain offset to antenna A, bit[7:4]
	priv->CrystalCap = priv->EEPROMCrystalCap;	// CrystalCap, bit[15:12]
	priv->ThermalMeter[0] = (priv->EEPROMThermalMeter&0x1f);// ThermalMeter, bit0~3 for RFIC1, bit4~7 for RFIC2
	priv->LedStrategy = SW_LED_MODE0;

	init_rate_adaptive(dev);

	RT_TRACE(COMP_INIT, "<==== ReadAdapterInfo8192SUsb\n");

	//return RT_STATUS_SUCCESS;
}


//
//	Description:
//		Read HW adapter information by E-Fuse or EEPROM according CR9346 reported.
//
//	Assumption:
//		1. CR9346 regiser has verified.
//		2. PASSIVE_LEVEL (USB interface)
//
//	Created by Roger, 2008.10.21.
//
static void rtl8192SU_read_eeprom_info(struct net_device *dev)
{
	struct r8192_priv 	*priv = ieee80211_priv(dev);
	u8			tmpU1b;

	RT_TRACE(COMP_INIT, "====> ReadAdapterInfo8192SUsb\n");

	// Retrieve Chip version.
	priv->card_8192_version = (VERSION_8192S)((read_nic_dword(dev, PMC_FSM)>>16)&0xF);
	RT_TRACE(COMP_INIT, "Chip Version ID: 0x%2x\n", priv->card_8192_version);

	tmpU1b = read_nic_byte(dev, EPROM_CMD);//CR9346

	// To check system boot selection.
	if (tmpU1b & CmdEERPOMSEL)
	{
		RT_TRACE(COMP_INIT, "Boot from EEPROM\n");
		priv->EepromOrEfuse = TRUE;
	}
	else
	{
		RT_TRACE(COMP_INIT, "Boot from EFUSE\n");
		priv->EepromOrEfuse = FALSE;
	}

	// To check autoload success or not.
	if (tmpU1b & CmdEEPROM_En)
	{
		RT_TRACE(COMP_INIT, "Autoload OK!!\n");
		priv->AutoloadFailFlag=FALSE;
		rtl8192SU_ReadAdapterInfo8192SUsb(dev);//eeprom or e-fuse
	}
	else
	{ // Auto load fail.
		RT_TRACE(COMP_INIT, "AutoLoad Fail reported from CR9346!!\n");
		priv->AutoloadFailFlag=TRUE;
		rtl8192SU_ConfigAdapterInfo8192SForAutoLoadFail(dev);

		//if (IS_BOOT_FROM_EFUSE(Adapter))
		if(!priv->EepromOrEfuse)
		{
			RT_TRACE(COMP_INIT, "Update shadow map for EFuse future use!!\n");
			EFUSE_ShadowMapUpdate(dev);
		}
	}
#ifdef TO_DO_LIST
	if((priv->RegChannelPlan >= RT_CHANNEL_DOMAIN_MAX) || (pHalData->EEPROMChannelPlan & EEPROM_CHANNEL_PLAN_BY_HW_MASK))
	{
		pMgntInfo->ChannelPlan = HalMapChannelPlan8192S(Adapter, (pHalData->EEPROMChannelPlan & (~(EEPROM_CHANNEL_PLAN_BY_HW_MASK))));
		pMgntInfo->bChnlPlanFromHW = (pHalData->EEPROMChannelPlan & EEPROM_CHANNEL_PLAN_BY_HW_MASK) ? TRUE : FALSE; // User cannot change  channel plan.
	}
	else
	{
		pMgntInfo->ChannelPlan = (RT_CHANNEL_DOMAIN)pMgntInfo->RegChannelPlan;
	}

	switch(pMgntInfo->ChannelPlan)
	{
		case RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN:
		{
			PRT_DOT11D_INFO	pDot11dInfo = GET_DOT11D_INFO(pMgntInfo);

			pDot11dInfo->bEnabled = TRUE;
		}
		RT_TRACE(COMP_INIT, DBG_LOUD, ("ReadAdapterInfo8187(): Enable dot11d when RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN!\n"));
		break;
	}

	RT_TRACE(COMP_INIT, DBG_LOUD, ("RegChannelPlan(%d) EEPROMChannelPlan(%d)", pMgntInfo->RegChannelPlan, pHalData->EEPROMChannelPlan));
	RT_TRACE(COMP_INIT, DBG_LOUD, ("ChannelPlan = %d\n" , pMgntInfo->ChannelPlan));

	RT_TRACE(COMP_INIT, DBG_LOUD, ("<==== ReadAdapterInfo8192S\n"));
#endif

	RT_TRACE(COMP_INIT, "<==== ReadAdapterInfo8192SUsb\n");

	//return RT_STATUS_SUCCESS;
}

short rtl8192_get_channel_map(struct net_device * dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	if(priv->ChannelPlan > COUNTRY_CODE_GLOBAL_DOMAIN){
		printk("rtl8180_init:Error channel plan! Set to default.\n");
		priv->ChannelPlan= 0;
	}
	RT_TRACE(COMP_INIT, "Channel plan is %d\n",priv->ChannelPlan);

	rtl819x_set_channel_map(priv->ChannelPlan, priv);
	return 0;
}

short rtl8192_init(struct net_device *dev)
{

	struct r8192_priv *priv = ieee80211_priv(dev);

	rtl8192_init_priv_variable(dev);
	rtl8192_init_priv_lock(priv);
	rtl8192_init_priv_task(dev);
	rtl8192_get_eeprom_size(dev);
	priv->ops->rtl819x_read_eeprom_info(dev);
	rtl8192_get_channel_map(dev);
	init_hal_dm(dev);
	init_timer(&priv->watch_dog_timer);
	priv->watch_dog_timer.data = (unsigned long)dev;
	priv->watch_dog_timer.function = watch_dog_timer_callback;

	//rtl8192_adapter_start(dev);
#ifdef DEBUG_EPROM
	dump_eprom(dev);
#endif
	return 0;
}

/******************************************************************************
 *function:  This function actually only set RRSR, RATR and BW_OPMODE registers
 *	     not to do all the hw config as its name says
 *   input:  net_device dev
 *  output:  none
 *  return:  none
 *  notice:  This part need to modified according to the rate set we filtered
 * ****************************************************************************/
void rtl8192_hwconfig(struct net_device* dev)
{
	u32 regRATR = 0, regRRSR = 0;
	u8 regBwOpMode = 0, regTmp = 0;
	struct r8192_priv *priv = ieee80211_priv(dev);

// Set RRSR, RATR, and BW_OPMODE registers
	//
	switch(priv->ieee80211->mode)
	{
	case WIRELESS_MODE_B:
		regBwOpMode = BW_OPMODE_20MHZ;
		regRATR = RATE_ALL_CCK;
		regRRSR = RATE_ALL_CCK;
		break;
	case WIRELESS_MODE_A:
		regBwOpMode = BW_OPMODE_5G |BW_OPMODE_20MHZ;
		regRATR = RATE_ALL_OFDM_AG;
		regRRSR = RATE_ALL_OFDM_AG;
		break;
	case WIRELESS_MODE_G:
		regBwOpMode = BW_OPMODE_20MHZ;
		regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		break;
	case WIRELESS_MODE_AUTO:
#ifdef TO_DO_LIST
		if (Adapter->bInHctTest)
		{
		    regBwOpMode = BW_OPMODE_20MHZ;
		    regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		    regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		}
		else
#endif
		{
		    regBwOpMode = BW_OPMODE_20MHZ;
		    regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG | RATE_ALL_OFDM_1SS | RATE_ALL_OFDM_2SS;
		    regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		}
		break;
	case WIRELESS_MODE_N_24G:
		// It support CCK rate by default.
		// CCK rate will be filtered out only when associated AP does not support it.
		regBwOpMode = BW_OPMODE_20MHZ;
			regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG | RATE_ALL_OFDM_1SS | RATE_ALL_OFDM_2SS;
			regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		break;
	case WIRELESS_MODE_N_5G:
		regBwOpMode = BW_OPMODE_5G;
		regRATR = RATE_ALL_OFDM_AG | RATE_ALL_OFDM_1SS | RATE_ALL_OFDM_2SS;
		regRRSR = RATE_ALL_OFDM_AG;
		break;
	}

	write_nic_byte(dev, BW_OPMODE, regBwOpMode);
	{
		u32 ratr_value = 0;
		ratr_value = regRATR;
		if (priv->rf_type == RF_1T2R)
		{
			ratr_value &= ~(RATE_ALL_OFDM_2SS);
		}
		write_nic_dword(dev, RATR0, ratr_value);
		write_nic_byte(dev, UFWP, 1);
	}
	regTmp = read_nic_byte(dev, 0x313);
	regRRSR = ((regTmp) << 24) | (regRRSR & 0x00ffffff);
	write_nic_dword(dev, RRSR, regRRSR);

	//
	// Set Retry Limit here
	//
	write_nic_word(dev, RETRY_LIMIT,
			priv->ShortRetryLimit << RETRY_LIMIT_SHORT_SHIFT | \
			priv->LongRetryLimit << RETRY_LIMIT_LONG_SHIFT);
	// Set Contention Window here

	// Set Tx AGC

	// Set Tx Antenna including Feedback control

	// Set Auto Rate fallback control


}


//
//	Description:
//		Initial HW relted registers.
//
//	Assumption:
//		Config RTL8192S USB MAC, we should config MAC before download FW.
//
//	2008.09.03, Added by Roger.
//
static void rtl8192SU_MacConfigBeforeFwDownloadASIC(struct net_device *dev)
{
	u8				tmpU1b;// i;
//	u16				tmpU2b;
//	u32				tmpU4b;
	u8				PollingCnt = 20;

	RT_TRACE(COMP_INIT, "--->MacConfigBeforeFwDownloadASIC()\n");

	//2MAC Initialization for power on sequence, Revised by Roger. 2008.09.03.

	//
	//<Roger_Notes> Set control path switch to HW control and reset Digital Core,  CPU Core and
	// MAC I/O to solve FW download fail when system from resume sate.
	// 2008.11.04.
	//
       tmpU1b = read_nic_byte(dev, SYS_CLKR+1);
       if(tmpU1b & 0x80)
	{
       	tmpU1b &= 0x3f;
              write_nic_byte(dev, SYS_CLKR+1, tmpU1b);
       }
	// Clear FW RPWM for FW control LPS. by tynli. 2009.02.23
	write_nic_byte(dev, RPWM, 0x0);

       tmpU1b = read_nic_byte(dev, SYS_FUNC_EN+1);
       tmpU1b &= 0x73;
       write_nic_byte(dev, SYS_FUNC_EN+1, tmpU1b);
       udelay(1000);

	//Revised POS, suggested by SD1 Alex, 2008.09.27.
	write_nic_byte(dev, SPS0_CTRL+1, 0x53);
	write_nic_byte(dev, SPS0_CTRL, 0x57);

	//Enable AFE Macro Block's Bandgap adn Enable AFE Macro Block's Mbias
	tmpU1b = read_nic_byte(dev, AFE_MISC);
	write_nic_byte(dev, AFE_MISC, (tmpU1b|AFE_BGEN|AFE_MBEN));

	//Enable PLL Power (LDOA15V)
	tmpU1b = read_nic_byte(dev, LDOA15_CTRL);
	write_nic_byte(dev, LDOA15_CTRL, (tmpU1b|LDA15_EN));

	//Enable LDOV12D block
	tmpU1b = read_nic_byte(dev, LDOV12D_CTRL);
	write_nic_byte(dev, LDOV12D_CTRL, (tmpU1b|LDV12_EN));

	//mpU1b = read_nic_byte(Adapter, SPS1_CTRL);
	//write_nic_byte(dev, SPS1_CTRL, (tmpU1b|SPS1_LDEN));

	//PlatformSleepUs(2000);

	//Enable Switch Regulator Block
	//tmpU1b = read_nic_byte(Adapter, SPS1_CTRL);
	//write_nic_byte(dev, SPS1_CTRL, (tmpU1b|SPS1_SWEN));

	//write_nic_dword(Adapter, SPS1_CTRL, 0x00a7b267);

	tmpU1b = read_nic_byte(dev, SYS_ISO_CTRL+1);
	write_nic_byte(dev, SYS_ISO_CTRL+1, (tmpU1b|0x08));

	//Engineer Packet CP test Enable
	tmpU1b = read_nic_byte(dev, SYS_FUNC_EN+1);
	write_nic_byte(dev, SYS_FUNC_EN+1, (tmpU1b|0x20));

	//Support 64k IMEM, suggested by SD1 Alex.
	tmpU1b = read_nic_byte(dev, SYS_ISO_CTRL+1);
	write_nic_byte(dev, SYS_ISO_CTRL+1, (tmpU1b& 0x68));

	//Enable AFE clock
	tmpU1b = read_nic_byte(dev, AFE_XTAL_CTRL+1);
	write_nic_byte(dev, AFE_XTAL_CTRL+1, (tmpU1b& 0xfb));

	//Enable AFE PLL Macro Block
	tmpU1b = read_nic_byte(dev, AFE_PLL_CTRL);
	write_nic_byte(dev, AFE_PLL_CTRL, (tmpU1b|0x11));

	//Attatch AFE PLL to MACTOP/BB/PCIe Digital
	tmpU1b = read_nic_byte(dev, SYS_ISO_CTRL);
	write_nic_byte(dev, SYS_ISO_CTRL, (tmpU1b&0xEE));

	// Switch to 40M clock
	write_nic_byte(dev, SYS_CLKR, 0x00);

	//SSC Disable
	tmpU1b = read_nic_byte(dev, SYS_CLKR);
	//write_nic_byte(dev, SYS_CLKR, (tmpU1b&0x5f));
	write_nic_byte(dev, SYS_CLKR, (tmpU1b|0xa0));

	//Enable MAC clock
	tmpU1b = read_nic_byte(dev, SYS_CLKR+1);
	write_nic_byte(dev, SYS_CLKR+1, (tmpU1b|0x18));

	//Revised POS, suggested by SD1 Alex, 2008.09.27.
	write_nic_byte(dev, PMC_FSM, 0x02);

	//Enable Core digital and enable IOREG R/W
	tmpU1b = read_nic_byte(dev, SYS_FUNC_EN+1);
	write_nic_byte(dev, SYS_FUNC_EN+1, (tmpU1b|0x08));

	//Enable REG_EN
	tmpU1b = read_nic_byte(dev, SYS_FUNC_EN+1);
	write_nic_byte(dev, SYS_FUNC_EN+1, (tmpU1b|0x80));

	//Switch the control path to FW
	tmpU1b = read_nic_byte(dev, SYS_CLKR+1);
	write_nic_byte(dev, SYS_CLKR+1, (tmpU1b|0x80)& 0xBF);

	write_nic_byte(dev, CMDR, 0xFC);
	write_nic_byte(dev, CMDR+1, 0x37);

	//Fix the RX FIFO issue(usb error), 970410
	tmpU1b = read_nic_byte_E(dev, 0x5c);
	write_nic_byte_E(dev, 0x5c, (tmpU1b|BIT7));

	 //For power save, used this in the bit file after 970621
	tmpU1b = read_nic_byte(dev, SYS_CLKR);
	write_nic_byte(dev, SYS_CLKR, tmpU1b&(~SYS_CPU_CLKSEL));

	// Revised for 8051 ROM code wrong operation. Added by Roger. 2008.10.16.
	write_nic_byte_E(dev, 0x1c, 0x80);

	//
	// <Roger_EXP> To make sure that TxDMA can ready to download FW.
	// We should reset TxDMA if IMEM RPT was not ready.
	// Suggested by SD1 Alex. 2008.10.23.
	//
	do
	{
		tmpU1b = read_nic_byte(dev, TCR);
		if((tmpU1b & TXDMA_INIT_VALUE) == TXDMA_INIT_VALUE)
			break;
		//PlatformStallExecution(5);
		udelay(5);
	}while(PollingCnt--);	// Delay 1ms

	if(PollingCnt <= 0 )
	{
		RT_TRACE(COMP_INIT, "MacConfigBeforeFwDownloadASIC(): Polling TXDMA_INIT_VALUE timeout!! Current TCR(%#x)\n", tmpU1b);
		tmpU1b = read_nic_byte(dev, CMDR);
		write_nic_byte(dev, CMDR, tmpU1b&(~TXDMA_EN));
		udelay(2);
		write_nic_byte(dev, CMDR, tmpU1b|TXDMA_EN);// Reset TxDMA
	}


	RT_TRACE(COMP_INIT, "<---MacConfigBeforeFwDownloadASIC()\n");
}

//
//	Description:
//		Initial HW relted registers.
//
//	Assumption:
//		1. This function is only invoked at driver intialization once.
//		2. PASSIVE LEVEL.
//
//	2008.06.10, Added by Roger.
//
static void rtl8192SU_MacConfigAfterFwDownload(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv((struct net_device *)dev);
	//PRT_HIGH_THROUGHPUT	pHTInfo = priv->ieee80211->pHTInfo;
	//u8	tmpU1b, RxPageCfg, i;
	u16	tmpU2b;
	u8	tmpU1b;//, i;


	RT_TRACE(COMP_INIT, "--->MacConfigAfterFwDownload()\n");

	// Enable Tx/Rx
	tmpU2b = (BBRSTn|BB_GLB_RSTn|SCHEDULE_EN|MACRXEN|MACTXEN|DDMA_EN|
			 FW2HW_EN|RXDMA_EN|TXDMA_EN|HCI_RXDMA_EN|HCI_TXDMA_EN);		//3
	//Adapter->HalFunc.SetHwRegHandler( Adapter, HW_VAR_COMMAND, &tmpU1b );
	write_nic_word(dev, CMDR, tmpU2b); //LZM REGISTER COM 090305

	// Loopback mode or not
	priv->LoopbackMode = RTL8192SU_NO_LOOPBACK; // Set no loopback as default.
	if(priv->LoopbackMode == RTL8192SU_NO_LOOPBACK)
		tmpU1b = LBK_NORMAL;
	else if (priv->LoopbackMode == RTL8192SU_MAC_LOOPBACK )
		tmpU1b = LBK_MAC_DLB;
	else
		RT_TRACE(COMP_INIT, "Serious error: wrong loopback mode setting\n");

	//Adapter->HalFunc.SetHwRegHandler( Adapter, HW_VAR_LBK_MODE, &tmpU1b);
	write_nic_byte(dev, LBKMD_SEL, tmpU1b);

	// Set RCR
	write_nic_dword(dev, RCR, priv->ReceiveConfig);
	RT_TRACE(COMP_INIT, "MacConfigAfterFwDownload(): Current RCR settings(%#x)\n", priv->ReceiveConfig);


	// Set RQPN
	//
	// <Roger_Notes> 2008.08.18.
	// 6 endpoints:
	// (1) Page number on CMDQ is 0x03.
	// (2) Page number on BCNQ, HQ and MGTQ is 0.
	// (3) Page number on BKQ, BEQ, VIQ and VOQ are 0x07.
	// (4) Page number on PUBQ is 0xdd
	//
	// 11 endpoints:
	// (1) Page number on CMDQ is 0x00.
	// (2) Page number on BCNQ is 0x02, HQ and MGTQ are 0x03.
	// (3) Page number on BKQ, BEQ, VIQ and VOQ are 0x07.
	// (4) Page number on PUBQ is 0xd8
	//
	//write_nic_dword(Adapter, 0xa0, 0x07070707); //BKQ, BEQ, VIQ and VOQ
	//write_nic_byte(dev, 0xa4, 0x00); // HCCAQ

	// Fix the RX FIFO issue(USB error), Rivesed by Roger, 2008-06-14
	tmpU1b = read_nic_byte_E(dev, 0x5C);
	write_nic_byte_E(dev, 0x5C, tmpU1b|BIT7);

	// For EFUSE init configuration.
	//if (IS_BOOT_FROM_EFUSE(Adapter))	// We may R/W EFUSE in EFUSE mode
	if (priv->bBootFromEfuse)
	{
		u8	tempval;

		tempval = read_nic_byte(dev, SYS_ISO_CTRL+1);
		tempval &= 0xFE;
		write_nic_byte(dev, SYS_ISO_CTRL+1, tempval);

		// Enable LDO 2.5V for write action
		//tempval = read_nic_byte(Adapter, EFUSE_TEST+3);
		//write_nic_byte(Adapter, EFUSE_TEST+3, (tempval | 0x80));

		// Change Efuse Clock for write action
		//write_nic_byte(Adapter, EFUSE_CLK, 0x03);

		// Change Program timing
		write_nic_byte(dev, EFUSE_CTRL+3, 0x72);
		//printk("!!!!!!!!!!!!!!!!!!!!!%s: write 0x33 with 0x72\n",__FUNCTION__);
		RT_TRACE(COMP_INIT, "EFUSE CONFIG OK\n");
	}


	RT_TRACE(COMP_INIT, "<---MacConfigAfterFwDownload()\n");
}

void rtl8192SU_HwConfigureRTL8192SUsb(struct net_device *dev)
{

	struct r8192_priv *priv = ieee80211_priv(dev);
	u8			regBwOpMode = 0;
	u32			regRATR = 0, regRRSR = 0;
	u8			regTmp = 0;
	u32 			i = 0;

	//1 This part need to modified according to the rate set we filtered!!
	//
	// Set RRSR, RATR, and BW_OPMODE registers
	//
	switch(priv->ieee80211->mode)
	{
	case WIRELESS_MODE_B:
		regBwOpMode = BW_OPMODE_20MHZ;
		regRATR = RATE_ALL_CCK;
		regRRSR = RATE_ALL_CCK;
		break;
	case WIRELESS_MODE_A:
		regBwOpMode = BW_OPMODE_5G |BW_OPMODE_20MHZ;
		regRATR = RATE_ALL_OFDM_AG;
		regRRSR = RATE_ALL_OFDM_AG;
		break;
	case WIRELESS_MODE_G:
		regBwOpMode = BW_OPMODE_20MHZ;
		regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		break;
	case WIRELESS_MODE_AUTO:
		if (priv->bInHctTest)
		{
		    regBwOpMode = BW_OPMODE_20MHZ;
		    regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		    regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		}
		else
		{
		    regBwOpMode = BW_OPMODE_20MHZ;
		    regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG | RATE_ALL_OFDM_1SS | RATE_ALL_OFDM_2SS;
		    regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		}
		break;
	case WIRELESS_MODE_N_24G:
		// It support CCK rate by default.
		// CCK rate will be filtered out only when associated AP does not support it.
		regBwOpMode = BW_OPMODE_20MHZ;
			regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG | RATE_ALL_OFDM_1SS | RATE_ALL_OFDM_2SS;
			regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		break;
	case WIRELESS_MODE_N_5G:
		regBwOpMode = BW_OPMODE_5G;
		regRATR = RATE_ALL_OFDM_AG | RATE_ALL_OFDM_1SS | RATE_ALL_OFDM_2SS;
		regRRSR = RATE_ALL_OFDM_AG;
		break;
	}

	//
	// <Roger_Notes> We disable CCK response rate until FIB CCK rate IC's back.
	// 2008.09.23.
	//
	regTmp = read_nic_byte(dev, INIRTSMCS_SEL);
	regRRSR = ((regRRSR & 0x000fffff)<<8) | regTmp;

	//
	// Update SIFS timing.
	//
	//priv->SifsTime = 0x0e0e0a0a;
	//Adapter->HalFunc.SetHwRegHandler( Adapter, HW_VAR_SIFS,  (pu1Byte)&pHalData->SifsTime);
	{	u8 val[4] = {0x0e, 0x0e, 0x0a, 0x0a};
		// SIFS for CCK Data ACK
		write_nic_byte(dev, SIFS_CCK, val[0]);
		// SIFS for CCK consecutive tx like CTS data!
		write_nic_byte(dev, SIFS_CCK+1, val[1]);

		// SIFS for OFDM Data ACK
		write_nic_byte(dev, SIFS_OFDM, val[2]);
		// SIFS for OFDM consecutive tx like CTS data!
		write_nic_byte(dev, SIFS_OFDM+1, val[3]);
	}

	write_nic_dword(dev, INIRTSMCS_SEL, regRRSR);
	write_nic_byte(dev, BW_OPMODE, regBwOpMode);

	//
	// Suggested by SD1 Alex, 2008-06-14.
	//
	//PlatformEFIOWrite1Byte(Adapter, TXOP_STALL_CTRL, 0x80);//NAV to protect all TXOP.

	//
	// Set Data Auto Rate Fallback Retry Count register.
	//
	write_nic_dword(dev, DARFRC, 0x02010000);
	write_nic_dword(dev, DARFRC+4, 0x06050403);
	write_nic_dword(dev, RARFRC, 0x02010000);
	write_nic_dword(dev, RARFRC+4, 0x06050403);

	// Set Data Auto Rate Fallback Reg. Added by Roger, 2008.09.22.
	for (i = 0; i < 8; i++)
		write_nic_dword(dev, ARFR0+i*4, 0x1f0ffff0);

	//
	// Aggregation length limit. Revised by Roger. 2008.09.22.
	//
	write_nic_byte(dev, AGGLEN_LMT_H, 0x0f);	// Set AMPDU length to 12Kbytes for ShortGI case.
	write_nic_dword(dev, AGGLEN_LMT_L, 0xddd77442); // Long GI
	write_nic_dword(dev, AGGLEN_LMT_L+4, 0xfffdd772);

	// Set NAV protection length
	write_nic_word(dev, NAV_PROT_LEN, 0x0080);

	// Set TXOP stall control for several queue/HI/BCN/MGT/
	write_nic_byte(dev, TXOP_STALL_CTRL, 0x00); // NAV Protect next packet.

	// Set MSDU lifetime.
	write_nic_byte(dev, MLT, 0x8f);

	// Set CCK/OFDM SIFS
	write_nic_word(dev, SIFS_CCK, 0x0a0a); // CCK SIFS shall always be 10us.
	write_nic_word(dev, SIFS_OFDM, 0x0e0e);

	write_nic_byte(dev, ACK_TIMEOUT, 0x40);

	// CF-END Threshold
	write_nic_byte(dev, CFEND_TH, 0xFF);

	//
	// For Min Spacing configuration.
	//
	switch(priv->rf_type)
	{
		case RF_1T2R:
		case RF_1T1R:
			RT_TRACE(COMP_INIT, "Initializeadapter: RF_Type%s\n", (priv->rf_type==RF_1T1R? "(1T1R)":"(1T2R)"));
			priv->MinSpaceCfg = (MAX_MSS_DENSITY_1T<<3);
			break;
		case RF_2T2R:
		case RF_2T2R_GREEN:
			RT_TRACE(COMP_INIT, "Initializeadapter:RF_Type(2T2R)\n");
			priv->MinSpaceCfg = (MAX_MSS_DENSITY_2T<<3);
			break;
	}
	write_nic_byte(dev, AMPDU_MIN_SPACE, priv->MinSpaceCfg);

	//LZM 090219
	//
	// For Min Spacing configuration.
	//
	//priv->MinSpaceCfg = 0x00;
	//rtl8192SU_SetHwRegAmpduMinSpace(dev, priv->MinSpaceCfg);
}


//	Description:	Initial HW relted registers.
//
//	Assumption:	This function is only invoked at driver intialization once.
//
//	2008.06.10, Added by Roger.
bool rtl8192SU_adapter_start(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	//u32 					dwRegRead = 0;
	//bool 					init_status = true;
	//u32					ulRegRead;
	bool             				rtStatus = true;
	//u8					PipeIndex;
	//u8					eRFPath, tmpU1b;
	u8 fw_download_times = 1;


	RT_TRACE(COMP_INIT, "--->InitializeAdapter8192SUsb()\n");

	//pHalData->bGPIOChangeRF = FALSE;


	//
	// <Roger_Notes> 2008.06.15.
	//
	// Initialization Steps on RTL8192SU:
	// a. MAC initialization prior to sending down firmware code.
	// b. Download firmware code step by step(i.e., IMEM, EMEM, DMEM).
	// c. MAC configuration after firmware has been download successfully.
	// d. Initialize BB related configurations.
	// e. Initialize RF related configurations.
	// f.  Start to BulkIn transfer.
	//

	//
	//a. MAC initialization prior to send down firmware code.
	//
start:
	rtl8192SU_MacConfigBeforeFwDownloadASIC(dev);

	//
	//b. Download firmware code step by step(i.e., IMEM, EMEM, DMEM).
	//
	rtStatus = FirmwareDownload92S(dev);
	if(rtStatus != true)
	{
		if(fw_download_times == 1){
			RT_TRACE(COMP_INIT, "InitializeAdapter8192SUsb(): Download Firmware failed once, Download again!!\n");
			fw_download_times = fw_download_times + 1;
			goto start;
		}else{
			RT_TRACE(COMP_INIT, "InitializeAdapter8192SUsb(): Download Firmware failed twice, end!!\n");
		goto end;
	}
	}
	//
	//c. MAC configuration after firmware has been download successfully.
	//
	rtl8192SU_MacConfigAfterFwDownload(dev);

	//priv->bLbusEnable = TRUE;
	//if(priv->RegRfOff == TRUE)
	//	priv->eRFPowerState = eRfOff;

	// Save target channel
	// <Roger_Notes> Current Channel will be updated again later.
	//priv->CurrentChannel = Channel;
	rtStatus = PHY_MACConfig8192S(dev);//===>ok
	if(rtStatus != true)
	{
		RT_TRACE(COMP_INIT, "InitializeAdapter8192SUsb(): Fail to configure MAC!!\n");
		goto end;
	}
	if (1){
		int i;
		for (i=0; i<4; i++)
			write_nic_dword(dev,WDCAPARA_ADD[i], 0x5e4322);
		write_nic_byte(dev,AcmHwCtrl, 0x01);
	}


	//
	//d. Initialize BB related configurations.
	//

	rtStatus = PHY_BBConfig8192S(dev);//===>ok
	if(rtStatus != true)
	{
		RT_TRACE(COMP_INIT, "InitializeAdapter8192SUsb(): Fail to configure BB!!\n");
		goto end;
	}

	rtl8192_setBBreg(dev, rFPGA0_AnalogParameter2, 0xff, 0x58);//===>ok

	//
	// e. Initialize RF related configurations.
	//
	// 2007/11/02 MH Before initalizing RF. We can not use FW to do RF-R/W.
	priv->Rf_Mode = RF_OP_By_SW_3wire;

	// For RF test only from Scott's suggestion
	//write_nic_byte(dev, 0x27, 0xDB);
	//write_nic_byte(dev, 0x1B, 0x07);


	write_nic_byte(dev, AFE_XTAL_CTRL+1, 0xDB);

	// <Roger_Notes> The following IOs are configured for each RF modules.
	// Enable RF module and reset RF and SDM module. 2008.11.17.
	if(priv->card_8192_version == VERSION_8192S_ACUT)
		write_nic_byte(dev, SPS1_CTRL+3, (u8)(RF_EN|RF_RSTB|RF_SDMRSTB)); // Fix A-Cut bug.
	else
		write_nic_byte(dev, RF_CTRL, (u8)(RF_EN|RF_RSTB|RF_SDMRSTB));

	rtStatus = PHY_RFConfig8192S(dev);//===>ok
	if(rtStatus != true)
	{
		RT_TRACE(COMP_INIT, "InitializeAdapter8192SUsb(): Fail to configure RF!!\n");
		goto end;
	}


	// Set CCK and OFDM Block "ON"
	rtl8192_setBBreg(dev, rFPGA0_RFMOD, bCCKEn, 0x1);
	rtl8192_setBBreg(dev, rFPGA0_RFMOD, bOFDMEn, 0x1);

	//
	// Turn off Radio B while RF type is 1T1R by SD3 Wilsion's request.
	// Revised by Roger, 2008.12.18.
	//
	if(priv->rf_type == RF_1T1R)
	{
		// This is needed for PHY_REG after 20081219
		rtl8192_setBBreg(dev, rFPGA0_RFMOD, 0xff000000, 0x03);
		// This is needed for PHY_REG before 20081219
		//PHY_SetBBReg(Adapter, rOFDM0_TRxPathEnable, bMaskByte0, 0x11);
	}


	//LZM 090219
	// Set CCK and OFDM Block "ON"
	//rtl8192_setBBreg(dev, rFPGA0_RFMOD, bCCKEn, 0x1);
	//rtl8192_setBBreg(dev, rFPGA0_RFMOD, bOFDMEn, 0x1);


	//3//Get hardware version, do it in read eeprom?
	//GetHardwareVersion819xUsb(Adapter);

	//3//
	//3 //Set Hardware
	//3//
	rtl8192SU_HwConfigureRTL8192SUsb(dev);//==>ok

	//
	// <Roger_Notes> We set MAC address here if autoload was failed before,
	// otherwise IDR0 will NOT contain any value.
	//
	write_nic_dword(dev, IDR0, ((u32*)dev->dev_addr)[0]);
	write_nic_word(dev, IDR4, ((u16*)(dev->dev_addr + 4))[0]);
	if(!priv->bInHctTest)
	{
		if(priv->ResetProgress == RESET_TYPE_NORESET)
		{
			//RT_TRACE(COMP_MLME, DBG_LOUD, ("Initializeadapter8192SUsb():RegWirelessMode(%#x) \n", Adapter->RegWirelessMode));
			//Adapter->HalFunc.SetWirelessModeHandler(Adapter, Adapter->RegWirelessMode);
			rtl8192_SetWirelessMode(dev, priv->ieee80211->mode);//===>ok
	        }
	}
	else
	{
		priv->ieee80211->mode = WIRELESS_MODE_G;
	 	rtl8192_SetWirelessMode(dev, WIRELESS_MODE_G);
	}

	//Security related.
	//-----------------------------------------------------------------------------
	// Set up security related. 070106, by rcnjko:
	// 1. Clear all H/W keys.
	// 2. Enable H/W encryption/decryption.
	//-----------------------------------------------------------------------------
	//CamResetAllEntry(Adapter);
	//Adapter->HalFunc.EnableHWSecCfgHandler(Adapter);

	//SecClearAllKeys(Adapter);
	CamResetAllEntry(dev);
	//SecInit(Adapter);
	{
		u8 SECR_value = 0x0;
		SECR_value |= SCR_TxEncEnable;
		SECR_value |= SCR_RxDecEnable;
		SECR_value |= SCR_NoSKMC;
		write_nic_byte(dev, SECR, SECR_value);
	}

#ifdef TO_DO_LIST

	//PHY_UpdateInitialGain(dev);

	if(priv->RegRfOff == true)
	{ // User disable RF via registry.
		u8 eRFPath = 0;

		RT_TRACE((COMP_INIT|COMP_RF), "InitializeAdapter8192SUsb(): Turn off RF for RegRfOff ----------\n");
		MgntActSet_RF_State(dev, eRfOff, RF_CHANGE_BY_SW);
		// Those action will be discard in MgntActSet_RF_State because off the same state
		for(eRFPath = 0; eRFPath <priv->NumTotalRFPath; eRFPath++)
			rtl8192_setBBreg(dev, (RF90_RADIO_PATH_E)eRFPath, 0x4, 0xC00, 0x0);
	}
	else if(priv->RfOffReason > RF_CHANGE_BY_PS)
	{ // H/W or S/W RF OFF before sleep.
		RT_TRACE((COMP_INIT|COMP_RF), "InitializeAdapter8192SUsb(): Turn off RF for RfOffReason(%d) ----------\n", priv->RfOffReason);
		MgntActSet_RF_State(dev, eRfOff, priv->RfOffReason);
	}
	else
	{
		priv->eRFPowerState = eRfOn;
		priv->RfOffReason = 0;
		RT_TRACE((COMP_INIT|COMP_RF), "InitializeAdapter8192SUsb(): RF is on ----------\n");
	}

#endif


//
// f. Start to BulkIn transfer.
//
#ifdef TO_DO_LIST

#ifndef UNDER_VISTA
	{
		u8	i;
		PlatformAcquireSpinLock(Adapter, RT_RX_SPINLOCK);

		for(PipeIndex=0; PipeIndex < MAX_RX_QUEUE; PipeIndex++)
		{
			if (PipeIndex == 0)
			{
				for(i=0; i<32; i++)
				HalUsbInMpdu(Adapter, PipeIndex);
			}
			else
			{
				//HalUsbInMpdu(Adapter, PipeIndex);
				//HalUsbInMpdu(Adapter, PipeIndex);
				//HalUsbInMpdu(Adapter, PipeIndex);
			}
		}
		PlatformReleaseSpinLock(Adapter, RT_RX_SPINLOCK);
	}
#else
		// Joseph add to 819X code base for Vista USB platform.
		// This part may need to be add to Hal819xU code base. too.
	        PlatformUsbEnableInPipes(Adapter);
#endif

	RT_TRACE(COMP_INIT, "HighestOperaRate = %x\n", Adapter->MgntInfo.HighestOperaRate);

	PlatformStartWorkItem( &(pHalData->RtUsbCheckForHangWorkItem) );

	//
	// <Roger_EXP> The following  configurations are for ASIC verification temporally.
	// 2008.07.10.
	//

#endif

	//
	// Read EEPROM TX power index and PHY_REG_PG.txt to capture correct
	// TX power index for different rate set.
	//
	//if(priv->card_8192_version >= VERSION_8192S_ACUT)
	{
		// Get original hw reg values
		PHY_GetHWRegOriginalValue(dev);

		// Write correct tx power index//FIXLZM
		PHY_SetTxPowerLevel8192S(dev, priv->chan);
	}

	{
	u8  tmpU1b = 0;
	// EEPROM R/W workaround
	tmpU1b = read_nic_byte(dev, MAC_PINMUX_CFG);
	write_nic_byte(dev, MAC_PINMUX_CFG, tmpU1b&(~GPIOMUX_EN));
	}

//
//<Roger_Notes> 2008.08.19.
// We return status here for temporal FPGA verification, 2008.08.19.

#ifdef RTL8192SU_FW_IQK
	write_nic_dword(dev, WFM5, FW_IQK_ENABLE);
	ChkFwCmdIoDone(dev);
#endif

	//
	// <Roger_Notes> We enable high power mechanism after NIC initialized.
	// 2008.11.27.
	//
	write_nic_dword(dev, WFM5, FW_RA_RESET);
	ChkFwCmdIoDone(dev);
	write_nic_dword(dev, WFM5, FW_RA_ACTIVE);
	ChkFwCmdIoDone(dev);
	write_nic_dword(dev, WFM5, FW_RA_REFRESH);
	ChkFwCmdIoDone(dev);
	write_nic_dword(dev, WFM5, FW_BB_RESET_ENABLE);

// <Roger_Notes> We return status here for temporal FPGA verification. 2008.05.12.
//

end:
return rtStatus;
}

/***************************************************************************
    -------------------------------NET STUFF---------------------------
***************************************************************************/

static struct net_device_stats *rtl8192_stats(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	return &priv->ieee80211->stats;
}

bool
HalTxCheckStuck819xUsb(
	struct net_device *dev
	)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u16 		RegTxCounter = read_nic_word(dev, 0x128);
	bool		bStuck = FALSE;
	RT_TRACE(COMP_RESET,"%s():RegTxCounter is %d,TxCounter is %d\n",__FUNCTION__,RegTxCounter,priv->TxCounter);
	if(priv->TxCounter==RegTxCounter)
		bStuck = TRUE;

	priv->TxCounter = RegTxCounter;

	return bStuck;
}

/*
*	<Assumption: RT_TX_SPINLOCK is acquired.>
*	First added: 2006.11.19 by emily
*/
RESET_TYPE
TxCheckStuck(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8			QueueID;
//	PRT_TCB			pTcb;
//	u8			ResetThreshold;
	bool			bCheckFwTxCnt = false;
	//unsigned long flags;

	//
	// Decide Stuch threshold according to current power save mode
	//

//     RT_TRACE(COMP_RESET, " ==> TxCheckStuck()\n");
//	     PlatformAcquireSpinLock(Adapter, RT_TX_SPINLOCK);
//	     spin_lock_irqsave(&priv->ieee80211->lock,flags);
	     for (QueueID = 0; QueueID<=BEACON_QUEUE;QueueID ++)
	     {
	     		if(QueueID == TXCMD_QUEUE)
		         continue;
#if 1
		     	if((skb_queue_len(&priv->ieee80211->skb_waitQ[QueueID]) == 0)  && (skb_queue_len(&priv->ieee80211->skb_aggQ[QueueID]) == 0))
			 	continue;
#endif

	             bCheckFwTxCnt = true;
	     }
//	     PlatformReleaseSpinLock(Adapter, RT_TX_SPINLOCK);
//	spin_unlock_irqrestore(&priv->ieee80211->lock,flags);
//	RT_TRACE(COMP_RESET,"bCheckFwTxCnt is %d\n",bCheckFwTxCnt);
#if 1
	if(bCheckFwTxCnt)
	{
		if(HalTxCheckStuck819xUsb(dev))
		{
			RT_TRACE(COMP_RESET, "TxCheckStuck(): Fw indicates no Tx condition! \n");
			return RESET_TYPE_SILENT;
		}
	}
#endif
	return RESET_TYPE_NORESET;
}

bool
HalRxCheckStuck819xUsb(struct net_device *dev)
{
	u16 	RegRxCounter = read_nic_word(dev, 0x130);
	struct r8192_priv *priv = ieee80211_priv(dev);
	bool bStuck = FALSE;
//#ifdef RTL8192SU

//#else
	static u8	rx_chk_cnt = 0;
	RT_TRACE(COMP_RESET,"%s(): RegRxCounter is %d,RxCounter is %d\n",__FUNCTION__,RegRxCounter,priv->RxCounter);
	// If rssi is small, we should check rx for long time because of bad rx.
	// or maybe it will continuous silent reset every 2 seconds.
	rx_chk_cnt++;
	if(priv->undecorated_smoothed_pwdb >= (RateAdaptiveTH_High+5))
	{
		rx_chk_cnt = 0;	//high rssi, check rx stuck right now.
	}
	else if(priv->undecorated_smoothed_pwdb < (RateAdaptiveTH_High+5) &&
		((priv->CurrentChannelBW!=HT_CHANNEL_WIDTH_20&&priv->undecorated_smoothed_pwdb>=RateAdaptiveTH_Low_40M) ||
		(priv->CurrentChannelBW==HT_CHANNEL_WIDTH_20&&priv->undecorated_smoothed_pwdb>=RateAdaptiveTH_Low_20M)) )
	{
		if(rx_chk_cnt < 2)
		{
			return bStuck;
		}
		else
		{
			rx_chk_cnt = 0;
		}
	}
	else if(((priv->CurrentChannelBW!=HT_CHANNEL_WIDTH_20&&priv->undecorated_smoothed_pwdb<RateAdaptiveTH_Low_40M) ||
		(priv->CurrentChannelBW==HT_CHANNEL_WIDTH_20&&priv->undecorated_smoothed_pwdb<RateAdaptiveTH_Low_20M)) &&
		priv->undecorated_smoothed_pwdb >= VeryLowRSSI)
	{
		if(rx_chk_cnt < 4)
		{
			//DbgPrint("RSSI < %d && RSSI >= %d, no check this time \n", RateAdaptiveTH_Low, VeryLowRSSI);
			return bStuck;
		}
		else
		{
			rx_chk_cnt = 0;
			//DbgPrint("RSSI < %d && RSSI >= %d, check this time \n", RateAdaptiveTH_Low, VeryLowRSSI);
		}
	}
	else
	{
		if(rx_chk_cnt < 8)
		{
			//DbgPrint("RSSI <= %d, no check this time \n", VeryLowRSSI);
			return bStuck;
		}
		else
		{
			rx_chk_cnt = 0;
			//DbgPrint("RSSI <= %d, check this time \n", VeryLowRSSI);
		}
	}
//#endif

	if(priv->RxCounter==RegRxCounter)
		bStuck = TRUE;

	priv->RxCounter = RegRxCounter;

	return bStuck;
}

RESET_TYPE
RxCheckStuck(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	//int                     i;
	bool        bRxCheck = FALSE;

//       RT_TRACE(COMP_RESET," ==> RxCheckStuck()\n");
	//PlatformAcquireSpinLock(Adapter, RT_RX_SPINLOCK);

	 if(priv->IrpPendingCount > 1)
	 	bRxCheck = TRUE;
       //PlatformReleaseSpinLock(Adapter, RT_RX_SPINLOCK);

//       RT_TRACE(COMP_RESET,"bRxCheck is %d \n",bRxCheck);
	if(bRxCheck)
	{
		if(HalRxCheckStuck819xUsb(dev))
		{
			RT_TRACE(COMP_RESET, "RxStuck Condition\n");
			return RESET_TYPE_SILENT;
		}
	}
	return RESET_TYPE_NORESET;
}


/**
*	This function is called by Checkforhang to check whether we should ask OS to reset driver
*
*	\param pAdapter	The adapter context for this miniport
*
*	Note:NIC with USB interface sholud not call this function because we cannot scan descriptor
*	to judge whether there is tx stuck.
*	Note: This function may be required to be rewrite for Vista OS.
*	<<<Assumption: Tx spinlock has been acquired >>>
*
*	8185 and 8185b does not implement this function. This is added by Emily at 2006.11.24
*/
RESET_TYPE
rtl819x_ifcheck_resetornot(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	RESET_TYPE	TxResetType = RESET_TYPE_NORESET;
	RESET_TYPE	RxResetType = RESET_TYPE_NORESET;
	RT_RF_POWER_STATE 	rfState;

	return RESET_TYPE_NORESET;

	rfState = priv->ieee80211->eRFPowerState;

	TxResetType = TxCheckStuck(dev);
#if 1
	if( rfState != eRfOff ||
		/*ADAPTER_TEST_STATUS_FLAG(Adapter, ADAPTER_STATUS_FW_DOWNLOAD_FAILURE)) &&*/
		(priv->ieee80211->iw_mode != IW_MODE_ADHOC))
	{
		// If driver is in the status of firmware download failure , driver skips RF initialization and RF is
		// in turned off state. Driver should check whether Rx stuck and do silent reset. And
		// if driver is in firmware download failure status, driver should initialize RF in the following
		// silent reset procedure Emily, 2008.01.21

		// Driver should not check RX stuck in IBSS mode because it is required to
		// set Check BSSID in order to send beacon, however, if check BSSID is
		// set, STA cannot hear any packet a all. Emily, 2008.04.12
		RxResetType = RxCheckStuck(dev);
	}
#endif
	if(TxResetType==RESET_TYPE_NORMAL || RxResetType==RESET_TYPE_NORMAL)
		return RESET_TYPE_NORMAL;
	else if(TxResetType==RESET_TYPE_SILENT || RxResetType==RESET_TYPE_SILENT){
		RT_TRACE(COMP_RESET,"%s():silent reset\n",__FUNCTION__);
		return RESET_TYPE_SILENT;
	}
	else
		return RESET_TYPE_NORESET;

}

void rtl8192_cancel_deferred_work(struct r8192_priv* priv);
int _rtl8192_up(struct net_device *dev);
int rtl8192_close(struct net_device *dev);



void
CamRestoreAllEntry(	struct net_device *dev)
{
	u8 EntryId = 0;
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8*	MacAddr = priv->ieee80211->current_network.bssid;

	static u8	CAM_CONST_ADDR[4][6] = {
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x01},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x02},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x03}};
	static u8	CAM_CONST_BROAD[] =
		{0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	RT_TRACE(COMP_SEC, "CamRestoreAllEntry: \n");


	if ((priv->ieee80211->pairwise_key_type == KEY_TYPE_WEP40)||
	    (priv->ieee80211->pairwise_key_type == KEY_TYPE_WEP104))
	{

		for(EntryId=0; EntryId<4; EntryId++)
		{
			{
				MacAddr = CAM_CONST_ADDR[EntryId];
				setKey(dev,
						EntryId ,
						EntryId,
						priv->ieee80211->pairwise_key_type,
						MacAddr,
						0,
						NULL);
			}
		}

	}
	else if(priv->ieee80211->pairwise_key_type == KEY_TYPE_TKIP)
	{

		{
			if(priv->ieee80211->iw_mode == IW_MODE_ADHOC)
				setKey(dev,
						4,
						0,
						priv->ieee80211->pairwise_key_type,
						(u8*)dev->dev_addr,
						0,
						NULL);
			else
				setKey(dev,
						4,
						0,
						priv->ieee80211->pairwise_key_type,
						MacAddr,
						0,
						NULL);
		}
	}
	else if(priv->ieee80211->pairwise_key_type == KEY_TYPE_CCMP)
	{

		{
			if(priv->ieee80211->iw_mode == IW_MODE_ADHOC)
				setKey(dev,
						4,
						0,
						priv->ieee80211->pairwise_key_type,
						(u8*)dev->dev_addr,
						0,
						NULL);
			else
				setKey(dev,
						4,
						0,
						priv->ieee80211->pairwise_key_type,
						MacAddr,
						0,
						NULL);
		}
	}



	if(priv->ieee80211->group_key_type == KEY_TYPE_TKIP)
	{
		MacAddr = CAM_CONST_BROAD;
		for(EntryId=1 ; EntryId<4 ; EntryId++)
		{
			{
				setKey(dev,
						EntryId,
						EntryId,
						priv->ieee80211->group_key_type,
						MacAddr,
						0,
						NULL);
			}
		}
		if(priv->ieee80211->iw_mode == IW_MODE_ADHOC)
				setKey(dev,
						0,
						0,
						priv->ieee80211->group_key_type,
						CAM_CONST_ADDR[0],
						0,
						NULL);
	}
	else if(priv->ieee80211->group_key_type == KEY_TYPE_CCMP)
	{
		MacAddr = CAM_CONST_BROAD;
		for(EntryId=1; EntryId<4 ; EntryId++)
		{
			{
				setKey(dev,
						EntryId ,
						EntryId,
						priv->ieee80211->group_key_type,
						MacAddr,
						0,
						NULL);
			}
		}

		if(priv->ieee80211->iw_mode == IW_MODE_ADHOC)
				setKey(dev,
						0 ,
						0,
						priv->ieee80211->group_key_type,
						CAM_CONST_ADDR[0],
						0,
						NULL);
	}
}
//////////////////////////////////////////////////////////////
// This function is used to fix Tx/Rx stop bug temporarily.
// This function will do "system reset" to NIC when Tx or Rx is stuck.
// The method checking Tx/Rx stuck of this function is supported by FW,
// which reports Tx and Rx counter to register 0x128 and 0x130.
//////////////////////////////////////////////////////////////
void
rtl819x_ifsilentreset(struct net_device *dev)
{
	//OCTET_STRING asocpdu;
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8	reset_times = 0;
	int reset_status = 0;
	struct ieee80211_device *ieee = priv->ieee80211;


	// 2007.07.20. If we need to check CCK stop, please uncomment this line.
	//bStuck = Adapter->HalFunc.CheckHWStopHandler(Adapter);

	if(priv->ResetProgress==RESET_TYPE_NORESET)
	{
RESET_START:

		RT_TRACE(COMP_RESET,"=========>Reset progress!! \n");

		// Set the variable for reset.
		priv->ResetProgress = RESET_TYPE_SILENT;
//		rtl8192_close(dev);
#if 1
		down(&priv->wx_sem);
		if(priv->up == 0)
		{
			RT_TRACE(COMP_ERR,"%s():the driver is not up! return\n",__FUNCTION__);
			up(&priv->wx_sem);
			return ;
		}
		priv->up = 0;
		RT_TRACE(COMP_RESET,"%s():======>start to down the driver\n",__FUNCTION__);
//		if(!netif_queue_stopped(dev))
//			netif_stop_queue(dev);

		rtl8192_rtx_disable(dev);
		rtl8192_cancel_deferred_work(priv);
		deinit_hal_dm(dev);
		del_timer_sync(&priv->watch_dog_timer);

		ieee->sync_scan_hurryup = 1;
		if(ieee->state == IEEE80211_LINKED)
		{
			down(&ieee->wx_sem);
			printk("ieee->state is IEEE80211_LINKED\n");
			ieee80211_stop_send_beacons(priv->ieee80211);
			del_timer_sync(&ieee->associate_timer);
			cancel_delayed_work(&ieee->associate_retry_wq);
			ieee80211_stop_scan(ieee);
			netif_carrier_off(dev);
			up(&ieee->wx_sem);
		}
		else{
			printk("ieee->state is NOT LINKED\n");
			ieee80211_softmac_stop_protocol(priv->ieee80211);			}
		up(&priv->wx_sem);
		RT_TRACE(COMP_RESET,"%s():<==========down process is finished\n",__FUNCTION__);
	//rtl8192_irq_disable(dev);
		RT_TRACE(COMP_RESET,"%s():===========>start to up the driver\n",__FUNCTION__);
		reset_status = _rtl8192_up(dev);

		RT_TRACE(COMP_RESET,"%s():<===========up process is finished\n",__FUNCTION__);
		if(reset_status == -EAGAIN)
		{
			if(reset_times < 3)
			{
				reset_times++;
				goto RESET_START;
			}
			else
			{
				RT_TRACE(COMP_ERR," ERR!!! %s():  Reset Failed!!\n", __FUNCTION__);
			}
		}
#endif
		ieee->is_silent_reset = 1;
#if 1
		EnableHWSecurityConfig8192(dev);
#if 1
		if(ieee->state == IEEE80211_LINKED && ieee->iw_mode == IW_MODE_INFRA)
		{
			ieee->set_chan(ieee->dev, ieee->current_network.channel);

#if 1
			queue_work(ieee->wq, &ieee->associate_complete_wq);
#endif

		}
		else if(ieee->state == IEEE80211_LINKED && ieee->iw_mode == IW_MODE_ADHOC)
		{
			ieee->set_chan(ieee->dev, ieee->current_network.channel);
			ieee->link_change(ieee->dev);

		//	notify_wx_assoc_event(ieee);

			ieee80211_start_send_beacons(ieee);

			if (ieee->data_hard_resume)
				ieee->data_hard_resume(ieee->dev);
			netif_carrier_on(ieee->dev);
		}
#endif

		CamRestoreAllEntry(dev);

		priv->ResetProgress = RESET_TYPE_NORESET;
		priv->reset_count++;

		priv->bForcedSilentReset =false;
		priv->bResetInProgress = false;

		// For test --> force write UFWP.
		write_nic_byte(dev, UFWP, 1);
		RT_TRACE(COMP_RESET, "Reset finished!! ====>[%d]\n", priv->reset_count);
#endif
	}
}

void CAM_read_entry(
	struct net_device *dev,
	u32	 		iIndex
)
{
 	u32 target_command=0;
	 u32 target_content=0;
	 u8 entry_i=0;
	 u32 ulStatus;
	s32 i=100;
//	printk("=======>start read CAM\n");
 	for(entry_i=0;entry_i<CAM_CONTENT_COUNT;entry_i++)
 	{
   	// polling bit, and No Write enable, and address
		target_command= entry_i+CAM_CONTENT_COUNT*iIndex;
		target_command= target_command | BIT31;

	//Check polling bit is clear
//	mdelay(1);
#if 1
		while((i--)>=0)
		{
			ulStatus = read_nic_dword(dev, RWCAM);
			if(ulStatus & BIT31){
				continue;
			}
			else{
				break;
			}
		}
#endif
  		write_nic_dword(dev, RWCAM, target_command);
   	 	RT_TRACE(COMP_SEC,"CAM_read_entry(): WRITE A0: %x \n",target_command);
   	 //	printk("CAM_read_entry(): WRITE A0: %lx \n",target_command);
  	 	target_content = read_nic_dword(dev, RCAMO);
  	 	RT_TRACE(COMP_SEC, "CAM_read_entry(): WRITE A8: %x \n",target_content);
  	 //	printk("CAM_read_entry(): WRITE A8: %lx \n",target_content);
 	}
	printk("\n");
}

void rtl819x_update_rxcounts(
	struct r8192_priv *priv,
	u32* TotalRxBcnNum,
	u32* TotalRxDataNum
)
{
	u16 			SlotIndex;
	u8			i;

	*TotalRxBcnNum = 0;
	*TotalRxDataNum = 0;

	SlotIndex = (priv->ieee80211->LinkDetectInfo.SlotIndex++)%(priv->ieee80211->LinkDetectInfo.SlotNum);
	priv->ieee80211->LinkDetectInfo.RxBcnNum[SlotIndex] = priv->ieee80211->LinkDetectInfo.NumRecvBcnInPeriod;
	priv->ieee80211->LinkDetectInfo.RxDataNum[SlotIndex] = priv->ieee80211->LinkDetectInfo.NumRecvDataInPeriod;
	for( i=0; i<priv->ieee80211->LinkDetectInfo.SlotNum; i++ ){
		*TotalRxBcnNum += priv->ieee80211->LinkDetectInfo.RxBcnNum[i];
		*TotalRxDataNum += priv->ieee80211->LinkDetectInfo.RxDataNum[i];
	}
}

extern	void	rtl819x_watchdog_wqcallback(struct work_struct *work)
{
	struct delayed_work *dwork = container_of(work,struct delayed_work,work);
       struct r8192_priv *priv = container_of(dwork,struct r8192_priv,watch_dog_wq);
       struct net_device *dev = priv->ieee80211->dev;
	struct ieee80211_device* ieee = priv->ieee80211;
	RESET_TYPE	ResetType = RESET_TYPE_NORESET;
      	static u8	check_reset_cnt=0;
	bool bBusyTraffic = false;

	if(!priv->up)
		return;
	hal_dm_watchdog(dev);

	{//to get busy traffic condition
		if(ieee->state == IEEE80211_LINKED)
		{
			//windows mod 666 to 100.
			//if(	ieee->LinkDetectInfo.NumRxOkInPeriod> 666 ||
			//	ieee->LinkDetectInfo.NumTxOkInPeriod> 666 ) {
			if(	ieee->LinkDetectInfo.NumRxOkInPeriod> 100 ||
				ieee->LinkDetectInfo.NumTxOkInPeriod> 100 ) {
				bBusyTraffic = true;
			}
			ieee->LinkDetectInfo.NumRxOkInPeriod = 0;
			ieee->LinkDetectInfo.NumTxOkInPeriod = 0;
			ieee->LinkDetectInfo.bBusyTraffic = bBusyTraffic;
		}
	}
	//added by amy for AP roaming
	{
		if(priv->ieee80211->state == IEEE80211_LINKED && priv->ieee80211->iw_mode == IW_MODE_INFRA)
		{
			u32	TotalRxBcnNum = 0;
			u32	TotalRxDataNum = 0;

			rtl819x_update_rxcounts(priv, &TotalRxBcnNum, &TotalRxDataNum);
			if((TotalRxBcnNum+TotalRxDataNum) == 0)
			{
				#ifdef TODO
				if(rfState == eRfOff)
					RT_TRACE(COMP_ERR,"========>%s()\n",__FUNCTION__);
				#endif
				printk("===>%s(): AP is power off,connect another one\n",__FUNCTION__);
			//	Dot11d_Reset(dev);
				priv->ieee80211->state = IEEE80211_ASSOCIATING;
				notify_wx_assoc_event(priv->ieee80211);
				RemovePeerTS(priv->ieee80211,priv->ieee80211->current_network.bssid);
				ieee->is_roaming = true;
				priv->ieee80211->link_change(dev);
                                queue_work(priv->ieee80211->wq, &priv->ieee80211->associate_procedure_wq);
			}
		}
		priv->ieee80211->LinkDetectInfo.NumRecvBcnInPeriod=0;
		priv->ieee80211->LinkDetectInfo.NumRecvDataInPeriod=0;
	}
//	CAM_read_entry(dev,4);
	//check if reset the driver
	if(check_reset_cnt++ >= 3 && !ieee->is_roaming)
	{
    		ResetType = rtl819x_ifcheck_resetornot(dev);
		check_reset_cnt = 3;
		//DbgPrint("Start to check silent reset\n");
	}
	//	RT_TRACE(COMP_RESET,"%s():priv->force_reset is %d,priv->ResetProgress is %d, priv->bForcedSilentReset is %d,priv->bDisableNormalResetCheck is %d,ResetType is %d\n",__FUNCTION__,priv->force_reset,priv->ResetProgress,priv->bForcedSilentReset,priv->bDisableNormalResetCheck,ResetType);
#if 1
	if( (priv->force_reset) || (priv->ResetProgress==RESET_TYPE_NORESET &&
		(priv->bForcedSilentReset ||
		(!priv->bDisableNormalResetCheck && ResetType==RESET_TYPE_SILENT)))) // This is control by OID set in Pomelo
	{
		RT_TRACE(COMP_RESET,"%s():priv->force_reset is %d,priv->ResetProgress is %d, priv->bForcedSilentReset is %d,priv->bDisableNormalResetCheck is %d,ResetType is %d\n",__FUNCTION__,priv->force_reset,priv->ResetProgress,priv->bForcedSilentReset,priv->bDisableNormalResetCheck,ResetType);
		rtl819x_ifsilentreset(dev);
	}
#endif
	priv->force_reset = false;
	priv->bForcedSilentReset = false;
	priv->bResetInProgress = false;
	RT_TRACE(COMP_TRACE, " <==RtUsbCheckForHangWorkItemCallback()\n");

}

void watch_dog_timer_callback(unsigned long data)
{
	struct r8192_priv *priv = ieee80211_priv((struct net_device *) data);
	//printk("===============>watch_dog  timer\n");
	queue_delayed_work(priv->priv_wq,&priv->watch_dog_wq, 0);
	mod_timer(&priv->watch_dog_timer, jiffies + MSECS(IEEE80211_WATCH_DOG_TIME));
}
int _rtl8192_up(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	//int i;
	int init_status = 0;
	priv->up=1;
	priv->ieee80211->ieee_up=1;
	RT_TRACE(COMP_INIT, "Bringing up iface");
	init_status = priv->ops->rtl819x_adapter_start(dev);
	if(!init_status)
	{
		RT_TRACE(COMP_ERR,"ERR!!! %s(): initialization is failed!\n", __FUNCTION__);
		priv->up=priv->ieee80211->ieee_up = 0;
		return -EAGAIN;
	}
	RT_TRACE(COMP_INIT, "start adapter finished\n");
	rtl8192_rx_enable(dev);
//	rtl8192_tx_enable(dev);
	if(priv->ieee80211->state != IEEE80211_LINKED)
	ieee80211_softmac_start_protocol(priv->ieee80211);
	ieee80211_reset_queue(priv->ieee80211);
	watch_dog_timer_callback((unsigned long) dev);
	if(!netif_queue_stopped(dev))
		netif_start_queue(dev);
	else
		netif_wake_queue(dev);

	/*
	 * Make sure that drop_unencrypted is initialized as "0"
	 * No packets will be sent in non-security mode if we had set drop_unencrypted.
	 * ex, After kill wpa_supplicant process, make the driver up again.
	 * drop_unencrypted remains as "1", which is set by wpa_supplicant. 2008/12/04.john
	 */
	priv->ieee80211->drop_unencrypted = 0;

	return 0;
}


int rtl8192_open(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	int ret;
	down(&priv->wx_sem);
	ret = rtl8192_up(dev);
	up(&priv->wx_sem);
	return ret;

}


int rtl8192_up(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	if (priv->up == 1) return -1;

	return _rtl8192_up(dev);
}


int rtl8192_close(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	int ret;

	down(&priv->wx_sem);

	ret = rtl8192_down(dev);

	up(&priv->wx_sem);

	return ret;

}

int rtl8192_down(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	int i;

	if (priv->up == 0) return -1;

	priv->up=0;
	priv->ieee80211->ieee_up = 0;
	RT_TRACE(COMP_DOWN, "==========>%s()\n", __FUNCTION__);
/* FIXME */
	if (!netif_queue_stopped(dev))
		netif_stop_queue(dev);

	rtl8192_rtx_disable(dev);
	//rtl8192_irq_disable(dev);

 /* Tx related queue release */
        for(i = 0; i < MAX_QUEUE_SIZE; i++) {
                skb_queue_purge(&priv->ieee80211->skb_waitQ [i]);
        }
        for(i = 0; i < MAX_QUEUE_SIZE; i++) {
                skb_queue_purge(&priv->ieee80211->skb_aggQ [i]);
        }

        for(i = 0; i < MAX_QUEUE_SIZE; i++) {
                skb_queue_purge(&priv->ieee80211->skb_drv_aggQ [i]);
        }

        //as cancel_delayed_work will del work->timer, so if work is not definedas struct delayed_work, it will corrupt
//	flush_scheduled_work();
	rtl8192_cancel_deferred_work(priv);
	deinit_hal_dm(dev);
	del_timer_sync(&priv->watch_dog_timer);


	ieee80211_softmac_stop_protocol(priv->ieee80211);
	memset(&priv->ieee80211->current_network, 0 , offsetof(struct ieee80211_network, list));
	RT_TRACE(COMP_DOWN, "<==========%s()\n", __FUNCTION__);

		return 0;
}


void rtl8192_commit(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	int reset_status = 0;
	//u8 reset_times = 0;
	if (priv->up == 0) return ;
	priv->up = 0;

	rtl8192_cancel_deferred_work(priv);
	del_timer_sync(&priv->watch_dog_timer);
	//cancel_delayed_work(&priv->SwChnlWorkItem);

	ieee80211_softmac_stop_protocol(priv->ieee80211);

	//rtl8192_irq_disable(dev);
	rtl8192_rtx_disable(dev);
	reset_status = _rtl8192_up(dev);

}

/*
void rtl8192_restart(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
*/
void rtl8192_restart(struct work_struct *work)
{
        struct r8192_priv *priv = container_of(work, struct r8192_priv, reset_wq);
        struct net_device *dev = priv->ieee80211->dev;

	down(&priv->wx_sem);

	rtl8192_commit(dev);

	up(&priv->wx_sem);
}

static void r8192_set_multicast(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	short promisc;

	//down(&priv->wx_sem);

	/* FIXME FIXME */

	promisc = (dev->flags & IFF_PROMISC) ? 1:0;

	if (promisc != priv->promisc)
	//	rtl8192_commit(dev);

	priv->promisc = promisc;

	//schedule_work(&priv->reset_wq);
	//up(&priv->wx_sem);
}


int r8192_set_mac_adr(struct net_device *dev, void *mac)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	struct sockaddr *addr = mac;

	down(&priv->wx_sem);

	memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);

	schedule_work(&priv->reset_wq);

	up(&priv->wx_sem);

	return 0;
}

/* based on ipw2200 driver */
int rtl8192_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
	struct iwreq *wrq = (struct iwreq *)rq;
	int ret=-1;
	struct ieee80211_device *ieee = priv->ieee80211;
	u32 key[4];
	u8 broadcast_addr[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
	u8 zero_addr[6] = {0};
	struct iw_point *p = &wrq->u.data;
	struct ieee_param *ipw = NULL;//(struct ieee_param *)wrq->u.data.pointer;

	down(&priv->wx_sem);


     if (p->length < sizeof(struct ieee_param) || !p->pointer){
             ret = -EINVAL;
             goto out;
	}

     ipw = (struct ieee_param *)kmalloc(p->length, GFP_KERNEL);
     if (ipw == NULL){
             ret = -ENOMEM;
             goto out;
     }
     if (copy_from_user(ipw, p->pointer, p->length)) {
		kfree(ipw);
            ret = -EFAULT;
            goto out;
	}

	switch (cmd) {
	    case RTL_IOCTL_WPA_SUPPLICANT:
	//parse here for HW security
			if (ipw->cmd == IEEE_CMD_SET_ENCRYPTION)
			{
				if (ipw->u.crypt.set_tx)
				{
					if (strcmp(ipw->u.crypt.alg, "CCMP") == 0)
						ieee->pairwise_key_type = KEY_TYPE_CCMP;
					else if (strcmp(ipw->u.crypt.alg, "TKIP") == 0)
						ieee->pairwise_key_type = KEY_TYPE_TKIP;
					else if (strcmp(ipw->u.crypt.alg, "WEP") == 0)
					{
						if (ipw->u.crypt.key_len == 13)
							ieee->pairwise_key_type = KEY_TYPE_WEP104;
						else if (ipw->u.crypt.key_len == 5)
							ieee->pairwise_key_type = KEY_TYPE_WEP40;
					}
					else
						ieee->pairwise_key_type = KEY_TYPE_NA;

					if (ieee->pairwise_key_type)
					{
				//	FIXME:these two lines below just to fix ipw interface bug, that is, it will never set mode down to driver. So treat it as ADHOC mode, if no association procedure. WB. 2009.02.04
						if (memcmp(ieee->ap_mac_addr, zero_addr, 6) == 0)
							ieee->iw_mode = IW_MODE_ADHOC;
						memcpy((u8*)key, ipw->u.crypt.key, 16);
						EnableHWSecurityConfig8192(dev);
					//we fill both index entry and 4th entry for pairwise key as in IPW interface, adhoc will only get here, so we need index entry for its default key serching!
					//added by WB.
						setKey(dev, 4, ipw->u.crypt.idx, ieee->pairwise_key_type, (u8*)ieee->ap_mac_addr, 0, key);
						if (ieee->iw_mode == IW_MODE_ADHOC)
						setKey(dev, ipw->u.crypt.idx, ipw->u.crypt.idx, ieee->pairwise_key_type, (u8*)ieee->ap_mac_addr, 0, key);
					}
				}
				else //if (ipw->u.crypt.idx) //group key use idx > 0
				{
					memcpy((u8*)key, ipw->u.crypt.key, 16);
					if (strcmp(ipw->u.crypt.alg, "CCMP") == 0)
						ieee->group_key_type= KEY_TYPE_CCMP;
					else if (strcmp(ipw->u.crypt.alg, "TKIP") == 0)
						ieee->group_key_type = KEY_TYPE_TKIP;
					else if (strcmp(ipw->u.crypt.alg, "WEP") == 0)
					{
						if (ipw->u.crypt.key_len == 13)
							ieee->group_key_type = KEY_TYPE_WEP104;
						else if (ipw->u.crypt.key_len == 5)
							ieee->group_key_type = KEY_TYPE_WEP40;
					}
					else
						ieee->group_key_type = KEY_TYPE_NA;

					if (ieee->group_key_type)
					{
							setKey(	dev,
								ipw->u.crypt.idx,
								ipw->u.crypt.idx,		//KeyIndex
						     		ieee->group_key_type,	//KeyType
						            	broadcast_addr,	//MacAddr
								0,		//DefaultKey
							      	key);		//KeyContent
					}
				}
			}
#ifdef JOHN_HWSEC_DEBUG
		//john's test 0711
		printk("@@ wrq->u pointer = ");
		for(i=0;i<wrq->u.data.length;i++){
			if(i%10==0) printk("\n");
			printk( "%8x|", ((u32*)wrq->u.data.pointer)[i] );
		}
		printk("\n");
#endif /*JOHN_HWSEC_DEBUG*/
		ret = ieee80211_wpa_supplicant_ioctl(priv->ieee80211, &wrq->u.data);
		break;

	    default:
		ret = -EOPNOTSUPP;
		break;
	}
	kfree(ipw);
        ipw = NULL;
out:
	up(&priv->wx_sem);
	return ret;
}

u8 rtl8192SU_HwRateToMRate(bool bIsHT, u8 rate,bool bFirstAMPDU)
{

	u8	ret_rate = 0x02;

	if( bFirstAMPDU )
	{
	if(!bIsHT)
	{
		switch(rate)
		{

			case DESC92S_RATE1M:		ret_rate = MGN_1M;		break;
			case DESC92S_RATE2M:		ret_rate = MGN_2M;		break;
			case DESC92S_RATE5_5M:		ret_rate = MGN_5_5M;		break;
			case DESC92S_RATE11M:		ret_rate = MGN_11M;		break;
			case DESC92S_RATE6M:		ret_rate = MGN_6M;		break;
			case DESC92S_RATE9M:		ret_rate = MGN_9M;		break;
			case DESC92S_RATE12M:		ret_rate = MGN_12M;		break;
			case DESC92S_RATE18M:		ret_rate = MGN_18M;		break;
			case DESC92S_RATE24M:		ret_rate = MGN_24M;		break;
			case DESC92S_RATE36M:		ret_rate = MGN_36M;		break;
			case DESC92S_RATE48M:		ret_rate = MGN_48M;		break;
			case DESC92S_RATE54M:		ret_rate = MGN_54M;		break;

			default:
				RT_TRACE(COMP_RECV, "HwRateToMRate90(): Non supported Rate [%x], bIsHT = %d!!!\n", rate, bIsHT);
					break;
	}
		}
		else
	{
		switch(rate)
		{

			case DESC92S_RATEMCS0:	ret_rate = MGN_MCS0;		break;
			case DESC92S_RATEMCS1:	ret_rate = MGN_MCS1;		break;
			case DESC92S_RATEMCS2:	ret_rate = MGN_MCS2;		break;
			case DESC92S_RATEMCS3:	ret_rate = MGN_MCS3;		break;
			case DESC92S_RATEMCS4:	ret_rate = MGN_MCS4;		break;
			case DESC92S_RATEMCS5:	ret_rate = MGN_MCS5;		break;
			case DESC92S_RATEMCS6:	ret_rate = MGN_MCS6;		break;
			case DESC92S_RATEMCS7:	ret_rate = MGN_MCS7;		break;
			case DESC92S_RATEMCS8:	ret_rate = MGN_MCS8;		break;
			case DESC92S_RATEMCS9:	ret_rate = MGN_MCS9;		break;
			case DESC92S_RATEMCS10:	ret_rate = MGN_MCS10;	break;
			case DESC92S_RATEMCS11:	ret_rate = MGN_MCS11;	break;
			case DESC92S_RATEMCS12:	ret_rate = MGN_MCS12;	break;
			case DESC92S_RATEMCS13:	ret_rate = MGN_MCS13;	break;
			case DESC92S_RATEMCS14:	ret_rate = MGN_MCS14;	break;
			case DESC92S_RATEMCS15:	ret_rate = MGN_MCS15;	break;
			case DESC92S_RATEMCS32:	ret_rate = (0x80|0x20);	break;

			default:
					RT_TRACE(COMP_RECV, "HwRateToMRate92S(): Non supported Rate [%x], bIsHT = %d!!!\n",rate, bIsHT );
				break;
		}

	}
	}
	else
	{
		switch(rate)
		{

			case DESC92S_RATE1M:	ret_rate = MGN_1M;		break;
			case DESC92S_RATE2M:	ret_rate = MGN_2M;		break;
			case DESC92S_RATE5_5M:	ret_rate = MGN_5_5M;		break;
			case DESC92S_RATE11M:	ret_rate = MGN_11M;		break;
			case DESC92S_RATE6M:	ret_rate = MGN_6M;		break;
			case DESC92S_RATE9M:	ret_rate = MGN_9M;		break;
			case DESC92S_RATE12M:	ret_rate = MGN_12M;		break;
			case DESC92S_RATE18M:	ret_rate = MGN_18M;		break;
			case DESC92S_RATE24M:	ret_rate = MGN_24M;		break;
			case DESC92S_RATE36M:	ret_rate = MGN_36M;		break;
			case DESC92S_RATE48M:	ret_rate = MGN_48M;		break;
			case DESC92S_RATE54M:	ret_rate = MGN_54M;		break;
			case DESC92S_RATEMCS0:	ret_rate = MGN_MCS0;		break;
			case DESC92S_RATEMCS1:	ret_rate = MGN_MCS1;		break;
			case DESC92S_RATEMCS2:	ret_rate = MGN_MCS2;		break;
			case DESC92S_RATEMCS3:	ret_rate = MGN_MCS3;		break;
			case DESC92S_RATEMCS4:	ret_rate = MGN_MCS4;		break;
			case DESC92S_RATEMCS5:	ret_rate = MGN_MCS5;		break;
			case DESC92S_RATEMCS6:	ret_rate = MGN_MCS6;		break;
			case DESC92S_RATEMCS7:	ret_rate = MGN_MCS7;		break;
			case DESC92S_RATEMCS8:	ret_rate = MGN_MCS8;		break;
			case DESC92S_RATEMCS9:	ret_rate = MGN_MCS9;		break;
			case DESC92S_RATEMCS10:	ret_rate = MGN_MCS10;	break;
			case DESC92S_RATEMCS11:	ret_rate = MGN_MCS11;	break;
			case DESC92S_RATEMCS12:	ret_rate = MGN_MCS12;	break;
			case DESC92S_RATEMCS13:	ret_rate = MGN_MCS13;	break;
			case DESC92S_RATEMCS14:	ret_rate = MGN_MCS14;	break;
			case DESC92S_RATEMCS15:	ret_rate = MGN_MCS15;	break;
			case DESC92S_RATEMCS32:	ret_rate = (0x80|0x20);	break;

			default:
				RT_TRACE(COMP_RECV, "HwRateToMRate92S(): Non supported Rate [%x], bIsHT = %d!!!\n",rate, bIsHT );
				break;
			}
	}
	return ret_rate;
}

u8 HwRateToMRate90(bool bIsHT, u8 rate)
{
	u8  ret_rate = 0xff;

	if(!bIsHT) {
		switch(rate) {
			case DESC90_RATE1M:   ret_rate = MGN_1M;         break;
			case DESC90_RATE2M:   ret_rate = MGN_2M;         break;
			case DESC90_RATE5_5M: ret_rate = MGN_5_5M;       break;
			case DESC90_RATE11M:  ret_rate = MGN_11M;        break;
			case DESC90_RATE6M:   ret_rate = MGN_6M;         break;
			case DESC90_RATE9M:   ret_rate = MGN_9M;         break;
			case DESC90_RATE12M:  ret_rate = MGN_12M;        break;
			case DESC90_RATE18M:  ret_rate = MGN_18M;        break;
			case DESC90_RATE24M:  ret_rate = MGN_24M;        break;
			case DESC90_RATE36M:  ret_rate = MGN_36M;        break;
			case DESC90_RATE48M:  ret_rate = MGN_48M;        break;
			case DESC90_RATE54M:  ret_rate = MGN_54M;        break;

			default:
				ret_rate = 0xff;
				RT_TRACE(COMP_RECV, "HwRateToMRate90(): Non supported Rate [%x], bIsHT = %d!!!\n", rate, bIsHT);
				break;
		}

	} else {
		switch(rate) {
			case DESC90_RATEMCS0:   ret_rate = MGN_MCS0;    break;
			case DESC90_RATEMCS1:   ret_rate = MGN_MCS1;    break;
			case DESC90_RATEMCS2:   ret_rate = MGN_MCS2;    break;
			case DESC90_RATEMCS3:   ret_rate = MGN_MCS3;    break;
			case DESC90_RATEMCS4:   ret_rate = MGN_MCS4;    break;
			case DESC90_RATEMCS5:   ret_rate = MGN_MCS5;    break;
			case DESC90_RATEMCS6:   ret_rate = MGN_MCS6;    break;
			case DESC90_RATEMCS7:   ret_rate = MGN_MCS7;    break;
			case DESC90_RATEMCS8:   ret_rate = MGN_MCS8;    break;
			case DESC90_RATEMCS9:   ret_rate = MGN_MCS9;    break;
			case DESC90_RATEMCS10:  ret_rate = MGN_MCS10;   break;
			case DESC90_RATEMCS11:  ret_rate = MGN_MCS11;   break;
			case DESC90_RATEMCS12:  ret_rate = MGN_MCS12;   break;
			case DESC90_RATEMCS13:  ret_rate = MGN_MCS13;   break;
			case DESC90_RATEMCS14:  ret_rate = MGN_MCS14;   break;
			case DESC90_RATEMCS15:  ret_rate = MGN_MCS15;   break;
			case DESC90_RATEMCS32:  ret_rate = (0x80|0x20); break;

			default:
				ret_rate = 0xff;
				RT_TRACE(COMP_RECV, "HwRateToMRate90(): Non supported Rate [%x], bIsHT = %d!!!\n",rate, bIsHT);
				break;
		}
	}

	return ret_rate;
}

/**
 * Function:     UpdateRxPktTimeStamp
 * Overview:     Recored down the TSF time stamp when receiving a packet
 *
 * Input:
 *       PADAPTER        Adapter
 *       PRT_RFD         pRfd,
 *
 * Output:
 *       PRT_RFD         pRfd
 *                               (pRfd->Status.TimeStampHigh is updated)
 *                               (pRfd->Status.TimeStampLow is updated)
 * Return:
 *               None
 */
void UpdateRxPktTimeStamp8190 (struct net_device *dev, struct ieee80211_rx_stats *stats)
{
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);

	if(stats->bIsAMPDU && !stats->bFirstMPDU) {
		stats->mac_time[0] = priv->LastRxDescTSFLow;
		stats->mac_time[1] = priv->LastRxDescTSFHigh;
	} else {
		priv->LastRxDescTSFLow = stats->mac_time[0];
		priv->LastRxDescTSFHigh = stats->mac_time[1];
	}
}

//by amy 080606

long rtl819x_translate_todbm(u8 signal_strength_index	)// 0-100 index.
{
	long	signal_power; // in dBm.

	// Translate to dBm (x=0.5y-95).
	signal_power = (long)((signal_strength_index + 1) >> 1);
	signal_power -= 95;

	return signal_power;
}


/* 2008/01/22 MH We can not delcare RSSI/EVM total value of sliding window to
    be a local static. Otherwise, it may increase when we return from S3/S4. The
    value will be kept in memory or disk. We must delcare the value in adapter
    and it will be reinitialized when return from S3/S4. */
void rtl8192_process_phyinfo(struct r8192_priv * priv,u8* buffer, struct ieee80211_rx_stats * pprevious_stats, struct ieee80211_rx_stats * pcurrent_stats)
{
	bool bcheck = false;
	u8	rfpath;
	u32	nspatial_stream, tmp_val;
	//u8	i;
	static u32 slide_rssi_index=0, slide_rssi_statistics=0;
	static u32 slide_evm_index=0, slide_evm_statistics=0;
	static u32 last_rssi=0, last_evm=0;

	static u32 slide_beacon_adc_pwdb_index=0, slide_beacon_adc_pwdb_statistics=0;
	static u32 last_beacon_adc_pwdb=0;

	struct ieee80211_hdr_3addr *hdr;
	u16 sc ;
	unsigned int frag,seq;
	hdr = (struct ieee80211_hdr_3addr *)buffer;
	sc = le16_to_cpu(hdr->seq_ctl);
	frag = WLAN_GET_SEQ_FRAG(sc);
	seq = WLAN_GET_SEQ_SEQ(sc);
	//cosa add 04292008 to record the sequence number
	pcurrent_stats->Seq_Num = seq;
	//
	// Check whether we should take the previous packet into accounting
	//
	if(!pprevious_stats->bIsAMPDU)
	{
		// if previous packet is not aggregated packet
		bcheck = true;
	}else
	{
	}


	if(slide_rssi_statistics++ >= PHY_RSSI_SLID_WIN_MAX)
	{
		slide_rssi_statistics = PHY_RSSI_SLID_WIN_MAX;
		last_rssi = priv->stats.slide_signal_strength[slide_rssi_index];
		priv->stats.slide_rssi_total -= last_rssi;
	}
	priv->stats.slide_rssi_total += pprevious_stats->SignalStrength;

	priv->stats.slide_signal_strength[slide_rssi_index++] = pprevious_stats->SignalStrength;
	if(slide_rssi_index >= PHY_RSSI_SLID_WIN_MAX)
		slide_rssi_index = 0;

	// <1> Showed on UI for user, in dbm
	tmp_val = priv->stats.slide_rssi_total/slide_rssi_statistics;
	priv->stats.signal_strength = rtl819x_translate_todbm((u8)tmp_val);
	pcurrent_stats->rssi = priv->stats.signal_strength;
	//
	// If the previous packet does not match the criteria, neglect it
	//
	if(!pprevious_stats->bPacketMatchBSSID)
	{
		if(!pprevious_stats->bToSelfBA)
			return;
	}

	if(!bcheck)
		return;


	//rtl8190_process_cck_rxpathsel(priv,pprevious_stats);//only rtl8190 supported

	//
	// Check RSSI
	//
	priv->stats.num_process_phyinfo++;

	/* record the general signal strength to the sliding window. */


	// <2> Showed on UI for engineering
	// hardware does not provide rssi information for each rf path in CCK
	if(!pprevious_stats->bIsCCK && (pprevious_stats->bPacketToSelf || pprevious_stats->bToSelfBA))
	{
		for (rfpath = RF90_PATH_A; rfpath < priv->NumTotalRFPath; rfpath++)
		{
                     if (!rtl8192_phy_CheckIsLegalRFPath(priv->ieee80211->dev, rfpath))
			         continue;

			//Fixed by Jacken 2008-03-20
			if(priv->stats.rx_rssi_percentage[rfpath] == 0)
			{
				priv->stats.rx_rssi_percentage[rfpath] = pprevious_stats->RxMIMOSignalStrength[rfpath];
				//DbgPrint("MIMO RSSI initialize \n");
			}
			if(pprevious_stats->RxMIMOSignalStrength[rfpath]  > priv->stats.rx_rssi_percentage[rfpath])
			{
				priv->stats.rx_rssi_percentage[rfpath] =
					( (priv->stats.rx_rssi_percentage[rfpath]*(Rx_Smooth_Factor-1)) +
					(pprevious_stats->RxMIMOSignalStrength[rfpath])) /(Rx_Smooth_Factor);
				priv->stats.rx_rssi_percentage[rfpath] = priv->stats.rx_rssi_percentage[rfpath]  + 1;
			}
			else
			{
				priv->stats.rx_rssi_percentage[rfpath] =
					( (priv->stats.rx_rssi_percentage[rfpath]*(Rx_Smooth_Factor-1)) +
					(pprevious_stats->RxMIMOSignalStrength[rfpath])) /(Rx_Smooth_Factor);
			}
			RT_TRACE(COMP_DBG,"priv->stats.rx_rssi_percentage[rfPath]  = %d \n" ,priv->stats.rx_rssi_percentage[rfpath] );
		}
	}


	//
	// Check PWDB.
	//
	RT_TRACE(COMP_RXDESC, "Smooth %s PWDB = %d\n",
				pprevious_stats->bIsCCK? "CCK": "OFDM",
				pprevious_stats->RxPWDBAll);

	if(pprevious_stats->bPacketBeacon)
	{
/* record the beacon pwdb to the sliding window. */
		if(slide_beacon_adc_pwdb_statistics++ >= PHY_Beacon_RSSI_SLID_WIN_MAX)
		{
			slide_beacon_adc_pwdb_statistics = PHY_Beacon_RSSI_SLID_WIN_MAX;
			last_beacon_adc_pwdb = priv->stats.Slide_Beacon_pwdb[slide_beacon_adc_pwdb_index];
			priv->stats.Slide_Beacon_Total -= last_beacon_adc_pwdb;
			//DbgPrint("slide_beacon_adc_pwdb_index = %d, last_beacon_adc_pwdb = %d, Adapter->RxStats.Slide_Beacon_Total = %d\n",
			//	slide_beacon_adc_pwdb_index, last_beacon_adc_pwdb, Adapter->RxStats.Slide_Beacon_Total);
		}
		priv->stats.Slide_Beacon_Total += pprevious_stats->RxPWDBAll;
		priv->stats.Slide_Beacon_pwdb[slide_beacon_adc_pwdb_index] = pprevious_stats->RxPWDBAll;
		//DbgPrint("slide_beacon_adc_pwdb_index = %d, pPreviousRfd->Status.RxPWDBAll = %d\n", slide_beacon_adc_pwdb_index, pPreviousRfd->Status.RxPWDBAll);
		slide_beacon_adc_pwdb_index++;
		if(slide_beacon_adc_pwdb_index >= PHY_Beacon_RSSI_SLID_WIN_MAX)
			slide_beacon_adc_pwdb_index = 0;
		pprevious_stats->RxPWDBAll = priv->stats.Slide_Beacon_Total/slide_beacon_adc_pwdb_statistics;
		if(pprevious_stats->RxPWDBAll >= 3)
			pprevious_stats->RxPWDBAll -= 3;
	}

	RT_TRACE(COMP_RXDESC, "Smooth %s PWDB = %d\n",
				pprevious_stats->bIsCCK? "CCK": "OFDM",
				pprevious_stats->RxPWDBAll);


	if(pprevious_stats->bPacketToSelf || pprevious_stats->bPacketBeacon || pprevious_stats->bToSelfBA)
	{
		if(priv->undecorated_smoothed_pwdb < 0)	// initialize
		{
			priv->undecorated_smoothed_pwdb = pprevious_stats->RxPWDBAll;
			//DbgPrint("First pwdb initialize \n");
		}
#if 1
		if(pprevious_stats->RxPWDBAll > (u32)priv->undecorated_smoothed_pwdb)
		{
			priv->undecorated_smoothed_pwdb =
					( ((priv->undecorated_smoothed_pwdb)*(Rx_Smooth_Factor-1)) +
					(pprevious_stats->RxPWDBAll)) /(Rx_Smooth_Factor);
			priv->undecorated_smoothed_pwdb = priv->undecorated_smoothed_pwdb + 1;
		}
		else
		{
			priv->undecorated_smoothed_pwdb =
					( ((priv->undecorated_smoothed_pwdb)*(Rx_Smooth_Factor-1)) +
					(pprevious_stats->RxPWDBAll)) /(Rx_Smooth_Factor);
		}
#else
		//Fixed by Jacken 2008-03-20
		if(pPreviousRfd->Status.RxPWDBAll > (u32)pHalData->UndecoratedSmoothedPWDB)
		{
			pHalData->UndecoratedSmoothedPWDB =
					( ((pHalData->UndecoratedSmoothedPWDB)* 5) + (pPreviousRfd->Status.RxPWDBAll)) / 6;
			pHalData->UndecoratedSmoothedPWDB = pHalData->UndecoratedSmoothedPWDB + 1;
		}
		else
		{
			pHalData->UndecoratedSmoothedPWDB =
					( ((pHalData->UndecoratedSmoothedPWDB)* 5) + (pPreviousRfd->Status.RxPWDBAll)) / 6;
		}
#endif

	}

	//
	// Check EVM
	//
	/* record the general EVM to the sliding window. */
	if(pprevious_stats->SignalQuality == 0)
	{
	}
	else
	{
		if(pprevious_stats->bPacketToSelf || pprevious_stats->bPacketBeacon || pprevious_stats->bToSelfBA){
			if(slide_evm_statistics++ >= PHY_RSSI_SLID_WIN_MAX){
				slide_evm_statistics = PHY_RSSI_SLID_WIN_MAX;
				last_evm = priv->stats.slide_evm[slide_evm_index];
				priv->stats.slide_evm_total -= last_evm;
			}

			priv->stats.slide_evm_total += pprevious_stats->SignalQuality;

			priv->stats.slide_evm[slide_evm_index++] = pprevious_stats->SignalQuality;
			if(slide_evm_index >= PHY_RSSI_SLID_WIN_MAX)
				slide_evm_index = 0;

			// <1> Showed on UI for user, in percentage.
			tmp_val = priv->stats.slide_evm_total/slide_evm_statistics;
			priv->stats.signal_quality = tmp_val;
			//cosa add 10/11/2007, Showed on UI for user in Windows Vista, for Link quality.
			priv->stats.last_signal_strength_inpercent = tmp_val;
		}

		// <2> Showed on UI for engineering
		if(pprevious_stats->bPacketToSelf || pprevious_stats->bPacketBeacon || pprevious_stats->bToSelfBA)
		{
			for(nspatial_stream = 0; nspatial_stream<2 ; nspatial_stream++) // 2 spatial stream
			{
				if(pprevious_stats->RxMIMOSignalQuality[nspatial_stream] != -1)
				{
					if(priv->stats.rx_evm_percentage[nspatial_stream] == 0)	// initialize
					{
						priv->stats.rx_evm_percentage[nspatial_stream] = pprevious_stats->RxMIMOSignalQuality[nspatial_stream];
					}
					priv->stats.rx_evm_percentage[nspatial_stream] =
						( (priv->stats.rx_evm_percentage[nspatial_stream]* (Rx_Smooth_Factor-1)) +
						(pprevious_stats->RxMIMOSignalQuality[nspatial_stream]* 1)) / (Rx_Smooth_Factor);
				}
			}
		}
	}


}

/*-----------------------------------------------------------------------------
 * Function:	rtl819x_query_rxpwrpercentage()
 *
 * Overview:
 *
 * Input:		char		antpower
 *
 * Output:		NONE
 *
 * Return:		0-100 percentage
 *
 * Revised History:
 *	When		Who		Remark
 *	05/26/2008	amy		Create Version 0 porting from windows code.
 *
 *---------------------------------------------------------------------------*/
static u8 rtl819x_query_rxpwrpercentage(
	char		antpower
	)
{
	if ((antpower <= -100) || (antpower >= 20))
	{
		return	0;
	}
	else if (antpower >= 0)
	{
		return	100;
	}
	else
	{
		return	(100+antpower);
	}

}	/* QueryRxPwrPercentage */

static u8
rtl819x_evm_dbtopercentage(
    char value
    )
{
    char ret_val;

    ret_val = value;

    if(ret_val >= 0)
        ret_val = 0;
    if(ret_val <= -33)
        ret_val = -33;
    ret_val = 0 - ret_val;
    ret_val*=3;
	if(ret_val == 99)
		ret_val = 100;
    return(ret_val);
}
//
//	Description:
// 	We want good-looking for signal strength/quality
//	2007/7/19 01:09, by cosa.
//
long
rtl819x_signal_scale_mapping(
	long currsig
	)
{
	long retsig;

	// Step 1. Scale mapping.
	if(currsig >= 61 && currsig <= 100)
	{
		retsig = 90 + ((currsig - 60) / 4);
	}
	else if(currsig >= 41 && currsig <= 60)
	{
		retsig = 78 + ((currsig - 40) / 2);
	}
	else if(currsig >= 31 && currsig <= 40)
	{
		retsig = 66 + (currsig - 30);
	}
	else if(currsig >= 21 && currsig <= 30)
	{
		retsig = 54 + (currsig - 20);
	}
	else if(currsig >= 5 && currsig <= 20)
	{
		retsig = 42 + (((currsig - 5) * 2) / 3);
	}
	else if(currsig == 4)
	{
		retsig = 36;
	}
	else if(currsig == 3)
	{
		retsig = 27;
	}
	else if(currsig == 2)
	{
		retsig = 18;
	}
	else if(currsig == 1)
	{
		retsig = 9;
	}
	else
	{
		retsig = currsig;
	}

	return retsig;
}

/*-----------------------------------------------------------------------------
 * Function:	QueryRxPhyStatus8192S()
 *
 * Overview:
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	06/01/2007	MHC		Create Version 0.
 *	06/05/2007	MHC		Accordign to HW's new data sheet, we add CCK and OFDM
 *						descriptor definition.
 *	07/04/2007	MHC		According to Jerry and Bryant's document. We read
 *						ir_isolation and ext_lna for RF's init value and use
 *						to compensate RSSI after receiving packets.
 *	09/10/2008	MHC		Modify name and PHY status field for 92SE.
 *	09/19/2008	MHC		Add CCK/OFDM SS/SQ for 92S series.
 *
 *---------------------------------------------------------------------------*/
static void rtl8192SU_query_rxphystatus(
	struct r8192_priv * priv,
	struct ieee80211_rx_stats * pstats,
	rx_desc_819x_usb	*pDesc,
	rx_drvinfo_819x_usb  * pdrvinfo,
	struct ieee80211_rx_stats * precord_stats,
	bool bpacket_match_bssid,
	bool bpacket_toself,
	bool bPacketBeacon,
	bool bToSelfBA
	)
{
	//PRT_RFD_STATUS		pRtRfdStatus = &(pRfd->Status);
	//PHY_STS_CCK_8192S_T	*pCck_buf;
	phy_sts_cck_819xusb_t	*	pcck_buf;
	phy_ofdm_rx_status_rxsc_sgien_exintfflag* prxsc;
	//u8				*prxpkt;
	//u8				i, max_spatial_stream, tmp_rxsnr, tmp_rxevm, rxsc_sgien_exflg;
	u8				i, max_spatial_stream, rxsc_sgien_exflg;
	char				rx_pwr[4], rx_pwr_all=0;
	//long				rx_avg_pwr = 0;
	//char				rx_snrX, rx_evmX;
	u8				evm, pwdb_all;
	u32				RSSI, total_rssi=0;//, total_evm=0;
//	long				signal_strength_index = 0;
	u8				is_cck_rate=0;
	u8				rf_rx_num = 0;



	priv->stats.numqry_phystatus++;

	is_cck_rate = rx_hal_is_cck_rate(pDesc);

	// Record it for next packet processing
	memset(precord_stats, 0, sizeof(struct ieee80211_rx_stats));
	pstats->bPacketMatchBSSID = precord_stats->bPacketMatchBSSID = bpacket_match_bssid;
	pstats->bPacketToSelf = precord_stats->bPacketToSelf = bpacket_toself;
	pstats->bIsCCK = precord_stats->bIsCCK = is_cck_rate;//RX_HAL_IS_CCK_RATE(pDrvInfo);
	pstats->bPacketBeacon = precord_stats->bPacketBeacon = bPacketBeacon;
	pstats->bToSelfBA = precord_stats->bToSelfBA = bToSelfBA;


	pstats->RxMIMOSignalQuality[0] = -1;
	pstats->RxMIMOSignalQuality[1] = -1;
	precord_stats->RxMIMOSignalQuality[0] = -1;
	precord_stats->RxMIMOSignalQuality[1] = -1;

	if(is_cck_rate)
	{
		u8 report;//, tmp_pwdb;
		//char cck_adc_pwdb[4];

		// CCK Driver info Structure is not the same as OFDM packet.
		pcck_buf = (phy_sts_cck_819xusb_t *)pdrvinfo;

		//
		// (1)Hardware does not provide RSSI for CCK
		//

		//
		// (2)PWDB, Average PWDB cacluated by hardware (for rate adaptive)
		//

		priv->stats.numqry_phystatusCCK++;

		if(!priv->bCckHighPower)
		{
			report = pcck_buf->cck_agc_rpt & 0xc0;
			report = report>>6;
			switch(report)
			{
				//Fixed by Jacken from Bryant 2008-03-20
				//Original value is -38 , -26 , -14 , -2
				//Fixed value is -35 , -23 , -11 , 6
				case 0x3:
					rx_pwr_all = -35 - (pcck_buf->cck_agc_rpt & 0x3e);
					break;
				case 0x2:
					rx_pwr_all = -23 - (pcck_buf->cck_agc_rpt & 0x3e);
					break;
				case 0x1:
					rx_pwr_all = -11 - (pcck_buf->cck_agc_rpt & 0x3e);
					break;
				case 0x0:
					rx_pwr_all = 8 - (pcck_buf->cck_agc_rpt & 0x3e);//6->8
					break;
			}
		}
		else
		{
			report = pdrvinfo->cfosho[0] & 0x60;
			report = report>>5;
			switch(report)
			{
				case 0x3:
					rx_pwr_all = -35 - ((pcck_buf->cck_agc_rpt & 0x1f)<<1) ;
					break;
				case 0x2:
					rx_pwr_all = -23 - ((pcck_buf->cck_agc_rpt & 0x1f)<<1);
					break;
				case 0x1:
					rx_pwr_all = -11 - ((pcck_buf->cck_agc_rpt & 0x1f)<<1) ;
					break;
				case 0x0:
					rx_pwr_all = -8 - ((pcck_buf->cck_agc_rpt & 0x1f)<<1) ;//6->-8
					break;
			}
		}

		pwdb_all = rtl819x_query_rxpwrpercentage(rx_pwr_all);//check it
		pstats->RxPWDBAll = precord_stats->RxPWDBAll = pwdb_all;
		//pstats->RecvSignalPower = pwdb_all;
		pstats->RecvSignalPower = rx_pwr_all;

		//
		// (3) Get Signal Quality (EVM)
		//
	//if(bpacket_match_bssid)
	{
			u8	sq;

			if(pstats->RxPWDBAll > 40)
			{
				sq = 100;
			}else
			{
				sq = pcck_buf->sq_rpt;

				if(pcck_buf->sq_rpt > 64)
					sq = 0;
				else if (pcck_buf->sq_rpt < 20)
					sq = 100;
				else
					sq = ((64-sq) * 100) / 44;
			}
			pstats->SignalQuality = precord_stats->SignalQuality = sq;
			pstats->RxMIMOSignalQuality[0] = precord_stats->RxMIMOSignalQuality[0] = sq;
			pstats->RxMIMOSignalQuality[1] = precord_stats->RxMIMOSignalQuality[1] = -1;
		}
	}
	else
	{
		priv->stats.numqry_phystatusHT++;

		// 2008/09/19 MH For 92S debug, RX RF path always enable!!
		priv->brfpath_rxenable[0] = priv->brfpath_rxenable[1] = TRUE;

		//
		// (1)Get RSSI for HT rate
		//
		//for(i=RF90_PATH_A; i<priv->NumTotalRFPath; i++)
		for(i=RF90_PATH_A; i<RF90_PATH_MAX; i++)
		{
			// 2008/01/30 MH we will judge RF RX path now.
			if (priv->brfpath_rxenable[i])
				rf_rx_num++;
			//else
			//	continue;

		//if (!rtl8192_phy_CheckIsLegalRFPath(priv->ieee80211->dev, i))
		//		continue;

			//Fixed by Jacken from Bryant 2008-03-20
			//Original value is 106
			//rx_pwr[i] = ((pofdm_buf->trsw_gain_X[i]&0x3F)*2) - 106;
			rx_pwr[i] = ((pdrvinfo->gain_trsw[i]&0x3F)*2) - 110;

			/* Translate DBM to percentage. */
			RSSI = rtl819x_query_rxpwrpercentage(rx_pwr[i]);	//check ok
			total_rssi += RSSI;
			RT_TRACE(COMP_RF, "RF-%d RXPWR=%x RSSI=%d\n", i, rx_pwr[i], RSSI);

			//Get Rx snr value in DB
			//tmp_rxsnr =	pofdm_buf->rxsnr_X[i];
			//rx_snrX = (char)(tmp_rxsnr);
			//rx_snrX /= 2;
			//priv->stats.rxSNRdB[i] = (long)rx_snrX;
			priv->stats.rxSNRdB[i] = (long)(pdrvinfo->rxsnr[i]/2);

			/* Translate DBM to percentage. */
			//RSSI = rtl819x_query_rxpwrpercentage(rx_pwr[i]);
			//total_rssi += RSSI;

			/* Record Signal Strength for next packet */
			//if(bpacket_match_bssid)
			{
				pstats->RxMIMOSignalStrength[i] =(u8) RSSI;
				precord_stats->RxMIMOSignalStrength[i] =(u8) RSSI;
			}
		}


		//
		// (2)PWDB, Average PWDB cacluated by hardware (for rate adaptive)
		//
		//Fixed by Jacken from Bryant 2008-03-20
		//Original value is 106
		//rx_pwr_all = (((pofdm_buf->pwdb_all ) >> 1 )& 0x7f) -106;
		rx_pwr_all = (((pdrvinfo->pwdb_all ) >> 1 )& 0x7f) -106;
		pwdb_all = rtl819x_query_rxpwrpercentage(rx_pwr_all);

		pstats->RxPWDBAll = precord_stats->RxPWDBAll = pwdb_all;
		pstats->RxPower = precord_stats->RxPower =  rx_pwr_all;
		pstats->RecvSignalPower = rx_pwr_all;

		//
		// (3)EVM of HT rate
		//
		//if(pdrvinfo->RxHT && pdrvinfo->RxRate>=DESC90_RATEMCS8 &&
		 //	pdrvinfo->RxRate<=DESC90_RATEMCS15)
		 if(pDesc->RxHT && pDesc->RxMCS>=DESC92S_RATEMCS8 &&
		 	pDesc->RxMCS<=DESC92S_RATEMCS15)
			max_spatial_stream = 2; //both spatial stream make sense
		else
			max_spatial_stream = 1; //only spatial stream 1 makes sense

		for(i=0; i<max_spatial_stream; i++)
		{
			//tmp_rxevm =	pofdm_buf->rxevm_X[i];
			//rx_evmX = (char)(tmp_rxevm);

			// Do not use shift operation like "rx_evmX >>= 1" because the compilor of free build environment
			// fill most significant bit to "zero" when doing shifting operation which may change a negative
			// value to positive one, then the dbm value (which is supposed to be negative)  is not correct anymore.
			//rx_evmX /= 2;	//dbm

			//evm = rtl819x_evm_dbtopercentage(rx_evmX);
			evm = rtl819x_evm_dbtopercentage( (pdrvinfo->rxevm[i] /*/ 2*/));	//dbm
			RT_TRACE(COMP_RF, "RXRATE=%x RXEVM=%x EVM=%s%d\n", pDesc->RxMCS, pdrvinfo->rxevm[i], "%", evm);

			//if(bpacket_match_bssid)
			{
				if(i==0) // Fill value in RFD, Get the first spatial stream only
					pstats->SignalQuality = precord_stats->SignalQuality = (u8)(evm & 0xff);
				pstats->RxMIMOSignalQuality[i] = precord_stats->RxMIMOSignalQuality[i] = (u8)(evm & 0xff);
			}
		}


		/* record rx statistics for debug */
		//rxsc_sgien_exflg = pofdm_buf->rxsc_sgien_exflg;
		prxsc =	(phy_ofdm_rx_status_rxsc_sgien_exintfflag *)&rxsc_sgien_exflg;
		//if(pdrvinfo->BW)	//40M channel
		if(pDesc->BW)	//40M channel
			priv->stats.received_bwtype[1+pdrvinfo->rxsc]++;
		else				//20M channel
			priv->stats.received_bwtype[0]++;
	}

	//UI BSS List signal strength(in percentage), make it good looking, from 0~100.
	//It is assigned to the BSS List in GetValueFromBeaconOrProbeRsp().
	if(is_cck_rate)
	{
		pstats->SignalStrength = precord_stats->SignalStrength = (u8)(rtl819x_signal_scale_mapping((long)pwdb_all));//PWDB_ALL;//check ok

	}
	else
	{
		//pRfd->Status.SignalStrength = pRecordRfd->Status.SignalStrength = (u8)(SignalScaleMapping(total_rssi/=RF90_PATH_MAX));//(u8)(total_rssi/=RF90_PATH_MAX);
		// We can judge RX path number now.
		if (rf_rx_num != 0)
			pstats->SignalStrength = precord_stats->SignalStrength = (u8)(rtl819x_signal_scale_mapping((long)(total_rssi/=rf_rx_num)));
	}
}/* QueryRxPhyStatus8192S */

void
rtl8192_record_rxdesc_forlateruse(
	struct ieee80211_rx_stats *	psrc_stats,
	struct ieee80211_rx_stats *	ptarget_stats
)
{
	ptarget_stats->bIsAMPDU = psrc_stats->bIsAMPDU;
	ptarget_stats->bFirstMPDU = psrc_stats->bFirstMPDU;
	ptarget_stats->Seq_Num = psrc_stats->Seq_Num;
}

static void rtl8192SU_query_rxphystatus(
	struct r8192_priv * priv,
	struct ieee80211_rx_stats * pstats,
	rx_desc_819x_usb	*pDesc,
	rx_drvinfo_819x_usb  * pdrvinfo,
	struct ieee80211_rx_stats * precord_stats,
	bool bpacket_match_bssid,
	bool bpacket_toself,
	bool bPacketBeacon,
	bool bToSelfBA
	);
void rtl8192SU_TranslateRxSignalStuff(struct sk_buff *skb,
				   struct ieee80211_rx_stats * pstats,
				   rx_desc_819x_usb	*pDesc,
                                   rx_drvinfo_819x_usb  *pdrvinfo)
{
	// TODO: We must only check packet for current MAC address. Not finish
	rtl8192_rx_info *info = (struct rtl8192_rx_info *)skb->cb;
	struct net_device *dev=info->dev;
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
	bool bpacket_match_bssid, bpacket_toself;
	bool bPacketBeacon=FALSE, bToSelfBA=FALSE;
	static struct ieee80211_rx_stats  previous_stats;
	struct ieee80211_hdr_3addr *hdr;//by amy
       u16 fc,type;

	// Get Signal Quality for only RX data queue (but not command queue)

	u8* tmp_buf;
	//u16 tmp_buf_len = 0;
	u8  *praddr;

	/* Get MAC frame start address. */
	tmp_buf = (u8*)skb->data;// + get_rxpacket_shiftbytes_819xusb(pstats);

	hdr = (struct ieee80211_hdr_3addr *)tmp_buf;
	fc = le16_to_cpu(hdr->frame_ctl);
	type = WLAN_FC_GET_TYPE(fc);
	praddr = hdr->addr1;

	/* Check if the received packet is acceptabe. */
	bpacket_match_bssid = ((IEEE80211_FTYPE_CTL != type) &&
                                			(eqMacAddr(priv->ieee80211->current_network.bssid,  (fc & IEEE80211_FCTL_TODS)? hdr->addr1 : (fc & IEEE80211_FCTL_FROMDS )? hdr->addr2 : hdr->addr3))
                                				 && (!pstats->bHwError) && (!pstats->bCRC)&& (!pstats->bICV));
	bpacket_toself =  bpacket_match_bssid & (eqMacAddr(praddr, priv->ieee80211->dev->dev_addr));

#if 1//cosa
		if(WLAN_FC_GET_FRAMETYPE(fc)== IEEE80211_STYPE_BEACON)
		{
			bPacketBeacon = true;
			//DbgPrint("Beacon 2, MatchBSSID = %d, ToSelf = %d \n", bPacketMatchBSSID, bPacketToSelf);
		}
		if(WLAN_FC_GET_FRAMETYPE(fc) == IEEE80211_STYPE_BLOCKACK)
		{
			if((eqMacAddr(praddr,dev->dev_addr)))
				bToSelfBA = true;
				//DbgPrint("BlockAck, MatchBSSID = %d, ToSelf = %d \n", bPacketMatchBSSID, bPacketToSelf);
		}

#endif


	if(bpacket_match_bssid)
	{
		priv->stats.numpacket_matchbssid++;
	}
	if(bpacket_toself){
		priv->stats.numpacket_toself++;
	}
	//
	// Process PHY information for previous packet (RSSI/PWDB/EVM)
	//
	// Because phy information is contained in the last packet of AMPDU only, so driver
	// should process phy information of previous packet
	rtl8192_process_phyinfo(priv, tmp_buf, &previous_stats, pstats);
	rtl8192SU_query_rxphystatus(priv, pstats, pDesc, pdrvinfo, &previous_stats, bpacket_match_bssid,bpacket_toself,bPacketBeacon,bToSelfBA);
	rtl8192_record_rxdesc_forlateruse(pstats, &previous_stats);

}

/**
* Function:	UpdateReceivedRateHistogramStatistics
* Overview:	Recored down the received data rate
*
* Input:
* 	struct net_device *dev
*	struct ieee80211_rx_stats *stats
*
* Output:
*
*			(priv->stats.ReceivedRateHistogram[] is updated)
* Return:
*		None
*/
void
UpdateReceivedRateHistogramStatistics8190(
	struct net_device *dev,
	struct ieee80211_rx_stats *stats
	)
{
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
    	u32 rcvType=1;   //0: Total, 1:OK, 2:CRC, 3:ICV
    	u32 rateIndex;
    	u32 preamble_guardinterval;  //1: short preamble/GI, 0: long preamble/GI


    	if(stats->bCRC)
       	rcvType = 2;
   	else if(stats->bICV)
       	rcvType = 3;

   	if(stats->bShortPreamble)
       	preamble_guardinterval = 1;// short
    	else
       	preamble_guardinterval = 0;// long

	switch(stats->rate)
	{
		//
		// CCK rate
		//
		case MGN_1M:    rateIndex = 0;  break;
		case MGN_2M:    rateIndex = 1;  break;
		case MGN_5_5M:  rateIndex = 2;  break;
		case MGN_11M:   rateIndex = 3;  break;
		//
		// Legacy OFDM rate
		//
		case MGN_6M:    rateIndex = 4;  break;
		case MGN_9M:    rateIndex = 5;  break;
		case MGN_12M:   rateIndex = 6;  break;
		case MGN_18M:   rateIndex = 7;  break;
		case MGN_24M:   rateIndex = 8;  break;
		case MGN_36M:   rateIndex = 9;  break;
		case MGN_48M:   rateIndex = 10; break;
		case MGN_54M:   rateIndex = 11; break;
		//
		// 11n High throughput rate
		//
		case MGN_MCS0:  rateIndex = 12; break;
		case MGN_MCS1:  rateIndex = 13; break;
		case MGN_MCS2:  rateIndex = 14; break;
		case MGN_MCS3:  rateIndex = 15; break;
		case MGN_MCS4:  rateIndex = 16; break;
		case MGN_MCS5:  rateIndex = 17; break;
		case MGN_MCS6:  rateIndex = 18; break;
		case MGN_MCS7:  rateIndex = 19; break;
		case MGN_MCS8:  rateIndex = 20; break;
		case MGN_MCS9:  rateIndex = 21; break;
		case MGN_MCS10: rateIndex = 22; break;
		case MGN_MCS11: rateIndex = 23; break;
		case MGN_MCS12: rateIndex = 24; break;
		case MGN_MCS13: rateIndex = 25; break;
		case MGN_MCS14: rateIndex = 26; break;
		case MGN_MCS15: rateIndex = 27; break;
		default:        rateIndex = 28; break;
	}
    priv->stats.received_preamble_GI[preamble_guardinterval][rateIndex]++;
    priv->stats.received_rate_histogram[0][rateIndex]++; //total
    priv->stats.received_rate_histogram[rcvType][rateIndex]++;
}

void rtl8192SU_query_rxdesc_status(struct sk_buff *skb, struct ieee80211_rx_stats *stats, bool bIsRxAggrSubframe)
{
	rtl8192_rx_info *info = (struct rtl8192_rx_info *)skb->cb;
	struct net_device *dev=info->dev;
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
	//rx_desc_819x_usb *desc = (rx_desc_819x_usb *)skb->data;
	rx_drvinfo_819x_usb  *driver_info = NULL;

	//PRT_RFD_STATUS				pRtRfdStatus = &pRfd->Status;
	//PHAL_DATA_8192SUSB			pHalData = GET_HAL_DATA(Adapter);
	//pu1Byte		pDesc = (pu1Byte)pDescIn;
	//PRX_DRIVER_INFO_8192S		pDrvInfo;

	rx_desc_819x_usb *desc = (rx_desc_819x_usb *)skb->data;

	if(0)
	{
		int m = 0;
		printk("========================");
		for(m=0; m<skb->len; m++){
			if((m%32) == 0)
				printk("\n");
			printk("%2x ",((u8*)skb->data)[m]);
		}
		printk("\n========================\n");

	}


	//
	//Get Rx Descriptor Raw Information
	//
	stats->Length = desc->Length ;
	stats->RxDrvInfoSize = desc->RxDrvInfoSize*RX_DRV_INFO_SIZE_UNIT;
	stats->RxBufShift = (desc->Shift)&0x03;
	stats->bICV = desc->ICV;
	stats->bCRC = desc->CRC32;
	stats->bHwError = stats->bCRC|stats->bICV;
	stats->Decrypted = !desc->SWDec;//RTL8190 set this bit to indicate that Hw does not decrypt packet
	stats->bIsAMPDU = (desc->AMSDU==1);
	stats->bFirstMPDU = (desc->PAGGR==1) && (desc->FAGGR==1);
	stats->bShortPreamble = desc->SPLCP;
	stats->RxIs40MHzPacket = (desc->BW==1);
	stats->TimeStampLow = desc->TSFL;

	if((desc->FAGGR==1) || (desc->PAGGR==1))
	{// Rx A-MPDU
		RT_TRACE(COMP_RXDESC, "FirstAGGR = %d, PartAggr = %d\n", desc->FAGGR, desc->PAGGR);
	}
//YJ,test,090310
if(stats->bHwError)
{
	if(stats->bICV)
		printk("%s: Receive ICV error!!!!!!!!!!!!!!!!!!!!!!\n", __FUNCTION__);
	if(stats->bCRC)
		printk("%s: Receive CRC error!!!!!!!!!!!!!!!!!!!!!!\n", __FUNCTION__);
}

	if(IS_UNDER_11N_AES_MODE(priv->ieee80211))
	{
		// Always received ICV error packets in AES mode.
		// This fixed HW later MIC write bug.
		if(stats->bICV && !stats->bCRC)
		{
			stats->bICV = FALSE;
			stats->bHwError = FALSE;
		}
	}

	// Transform HwRate to MRate
	if(!stats->bHwError)
		//stats->DataRate = HwRateToMRate(
		//	(BOOLEAN)GET_RX_DESC_RXHT(pDesc),
		//	(u1Byte)GET_RX_DESC_RXMCS(pDesc),
		//	(BOOLEAN)GET_RX_DESC_PAGGR(pDesc));
		stats->rate = rtl8192SU_HwRateToMRate(desc->RxHT, desc->RxMCS, desc->PAGGR);
	else
		stats->rate = MGN_1M;

	//
	// Collect Rx rate/AMPDU/TSFL
	//
	//UpdateRxdRateHistogramStatistics8192S(Adapter, pRfd);
	//UpdateRxAMPDUHistogramStatistics8192S(Adapter, pRfd);
	//UpdateRxPktTimeStamp8192S(Adapter, pRfd);
	UpdateReceivedRateHistogramStatistics8190(dev, stats);
	//UpdateRxAMPDUHistogramStatistics8192S(dev, stats);	//FIXLZM
	UpdateRxPktTimeStamp8190(dev, stats);

	//
	// Get PHY Status and RSVD parts.
	// <Roger_Notes> It only appears on last aggregated packet.
	//
	if (desc->PHYStatus)
	{
		//driver_info = (rx_drvinfo_819x_usb *)(skb->data + RX_DESC_SIZE + stats->RxBufShift);
		driver_info = (rx_drvinfo_819x_usb *)(skb->data + sizeof(rx_desc_819x_usb) + \
				stats->RxBufShift);
		if(0)
		{
			int m = 0;
			printk("========================\n");
			printk("RX_DESC_SIZE:%d, RxBufShift:%d, RxDrvInfoSize:%d\n",
					RX_DESC_SIZE, stats->RxBufShift, stats->RxDrvInfoSize);
			for(m=0; m<32; m++){
			       printk("%2x ",((u8*)driver_info)[m]);
			}
			printk("\n========================\n");

		}

	}

	//YJ,add,090107
	skb_pull(skb, sizeof(rx_desc_819x_usb));
	//YJ,add,090107,end

	//
	// Get Total offset of MPDU Frame Body
	//
	if((stats->RxBufShift + stats->RxDrvInfoSize) > 0)
	{
		stats->bShift = 1;
		//YJ,add,090107
		skb_pull(skb, stats->RxBufShift + stats->RxDrvInfoSize);
		//YJ,add,090107,end
	}

	//
	// Get PHY Status and RSVD parts.
	// <Roger_Notes> It only appears on last aggregated packet.
	//
	if (desc->PHYStatus)
	{
		rtl8192SU_TranslateRxSignalStuff(skb, stats, desc, driver_info);
	}
}

//
// Description:
// 	The strarting address of wireless lan header will shift 1 or 2 or 3 or "more" bytes for the following reason :
// 	(1) QoS control : shift 2 bytes
// 	(2) Mesh Network : shift 1 or 3 bytes
// 	(3) RxDriverInfo occupies  the front parts of Rx Packets buffer(shift units is in 8Bytes)
//
//  	It is because Lextra CPU used by 8186 or 865x series assert exception if the statrting address
//	of IP header is not double word alignment.
//	This features is supported in 818xb and 8190 only, but not 818x.
//
//	parameter: PRT_RFD, Pointer of Reeceive frame descriptor which is initialized according to
//					     Rx Descriptor
//	return value: unsigned int,  number of total shifted bytes
//
//	Notes: 2008/06/28, created by Roger
//
u32 GetRxPacketShiftBytes8192SU(struct ieee80211_rx_stats  *Status, bool bIsRxAggrSubframe)
{
	//PRT_RFD_STATUS	pRtRfdStatus = &pRfd->Status;

	return (sizeof(rx_desc_819x_usb) + Status->RxDrvInfoSize + Status->RxBufShift);
}

void rtl8192SU_rx_nomal(struct sk_buff* skb)
{
	rtl8192_rx_info *info = (struct rtl8192_rx_info *)skb->cb;
	struct net_device *dev=info->dev;
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
	struct ieee80211_rx_stats stats = {
		.signal = 0,
		.noise = -98,
		.rate = 0,
		//      .mac_time = jiffies,
		.freq = IEEE80211_24GHZ_BAND,
	};
	u32 rx_pkt_len = 0;
	struct ieee80211_hdr_1addr *ieee80211_hdr = NULL;
	bool unicast_packet = false;

	//printk("**********skb->len = %d\n", skb->len);
	/* 20 is for ps-poll */
	if((skb->len >=(20 + sizeof(rx_desc_819x_usb))) && (skb->len < RX_URB_SIZE)) {

		/* first packet should not contain Rx aggregation header */
		rtl8192SU_query_rxdesc_status(skb, &stats, false);
		/* TODO */

		/* hardware related info */
		priv->stats.rxoktotal++;  //YJ,test,090108

		/* Process the MPDU recevied */
		skb_trim(skb, skb->len - 4/*sCrcLng*/);//FIXLZM

		rx_pkt_len = skb->len;
		ieee80211_hdr = (struct ieee80211_hdr_1addr *)skb->data;
		unicast_packet = false;
		if(is_broadcast_ether_addr(ieee80211_hdr->addr1)) {
			//TODO
		}else if(is_multicast_ether_addr(ieee80211_hdr->addr1)){
			//TODO
		}else {
			/* unicast packet */
			unicast_packet = true;
		}

		if(!ieee80211_rx(priv->ieee80211,skb, &stats)) {
			dev_kfree_skb_any(skb);
		} else {
		//	priv->stats.rxoktotal++;  //YJ,test,090108
			if(unicast_packet) {
				priv->stats.rxbytesunicast += rx_pkt_len;
			}
		}

		//up is firs pkt, follow is next and next
	}
	else
	{
		priv->stats.rxurberr++;
		printk("actual_length:%d\n", skb->len);
		dev_kfree_skb_any(skb);
	}

}

void
rtl819xusb_process_received_packet(
	struct net_device *dev,
	struct ieee80211_rx_stats *pstats
	)
{
//	bool bfreerfd=false, bqueued=false;
	u8* 	frame;
	u16     frame_len=0;
	struct r8192_priv *priv = ieee80211_priv(dev);
//	u8			index = 0;
//	u8			TID = 0;
	//u16			seqnum = 0;
	//PRX_TS_RECORD	pts = NULL;

	// Get shifted bytes of Starting address of 802.11 header. 2006.09.28, by Emily
	//porting by amy 080508
	pstats->virtual_address += get_rxpacket_shiftbytes_819xusb(pstats);
	frame = pstats->virtual_address;
	frame_len = pstats->packetlength;
#ifdef TODO	// by amy about HCT
	if(!Adapter->bInHctTest)
		CountRxErrStatistics(Adapter, pRfd);
#endif
	{
	#ifdef ENABLE_PS  //by amy for adding ps function in future
		RT_RF_POWER_STATE rtState;
		// When RF is off, we should not count the packet for hw/sw synchronize
		// reason, ie. there may be a duration while sw switch is changed and hw
		// switch is being changed. 2006.12.04, by shien chang.
		Adapter->HalFunc.GetHwRegHandler(Adapter, HW_VAR_RF_STATE, (u8* )(&rtState));
		if (rtState == eRfOff)
		{
			return;
		}
	#endif
	priv->stats.rxframgment++;

	}
#ifdef TODO
	RmMonitorSignalStrength(Adapter, pRfd);
#endif
	/* 2007/01/16 MH Add RX command packet handle here. */
	/* 2007/03/01 MH We have to release RFD and return if rx pkt is cmd pkt. */
	if (rtl819xusb_rx_command_packet(dev, pstats))
	{
		return;
	}

#ifdef SW_CRC_CHECK
	SwCrcCheck();
#endif


}

void query_rx_cmdpkt_desc_status(struct sk_buff *skb, struct ieee80211_rx_stats *stats)
{
//	rtl8192_rx_info *info = (struct rtl8192_rx_info *)skb->cb;
//	struct net_device *dev=info->dev;
//	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
	rx_desc_819x_usb *desc = (rx_desc_819x_usb *)skb->data;
//	rx_drvinfo_819x_usb  *driver_info;

	//
	//Get Rx Descriptor Information
	//
	stats->virtual_address = (u8*)skb->data;
	stats->Length = desc->Length;
	stats->RxDrvInfoSize = 0;
	stats->RxBufShift = 0;
	stats->packetlength = stats->Length-scrclng;
	stats->fraglength = stats->packetlength;
	stats->fragoffset = 0;
	stats->ntotalfrag = 1;
}

void rtl8192SU_rx_cmd(struct sk_buff *skb)
{
	struct rtl8192_rx_info *info = (struct rtl8192_rx_info *)skb->cb;
	struct net_device *dev = info->dev;

	/* TODO */
	struct ieee80211_rx_stats stats = {
		.signal = 0,
		.noise = -98,
		.rate = 0,
		//      .mac_time = jiffies,
		.freq = IEEE80211_24GHZ_BAND,
	};

	//
	// Check buffer length to determine if this is a valid MPDU.
	//
	if( (skb->len >= sizeof(rx_desc_819x_usb)) && (skb->len <= RX_URB_SIZE) )//&&
		//(pHalData->SwChnlInProgress == FALSE))
	{
		//
		// Collection information in Rx descriptor.
		//
		query_rx_cmdpkt_desc_status(skb,&stats);
		// this is to be done by amy 080508     prfd->queue_id = 1;

		//
		// Process the MPDU recevied.
		//
		rtl819xusb_process_received_packet(dev,&stats);

		dev_kfree_skb_any(skb);
	}
	else
	{
		//RTInsertTailListWithCnt(&pAdapter->RfdIdleQueue, &pRfd->List, &pAdapter->NumIdleRfd);
		//RT_ASSERT(pAdapter->NumIdleRfd <= pAdapter->NumRfd, ("HalUsbInCommandComplete8192SUsb(): Adapter->NumIdleRfd(%d)\n", pAdapter->NumIdleRfd));
		//RT_TRACE(COMP_RECV, DBG_LOUD, ("HalUsbInCommandComplete8192SUsb(): NOT enough Resources!! BufLenUsed(%d), NumIdleRfd(%d)\n",
			//pContext->BufLenUsed, pAdapter->NumIdleRfd));
	}

	//
	// Reuse USB_IN_CONTEXT since we had finished processing the
	// buffer in USB_IN_CONTEXT.
	//
	//HalUsbReturnInContext(pAdapter, pContext);

	//
	// Issue another bulk IN transfer.
	//
	//HalUsbInMpdu(pAdapter, PipeIndex);

	RT_TRACE(COMP_RECV, "<--- HalUsbInCommandComplete8192SUsb()\n");

}

void rtl8192_irq_rx_tasklet(struct r8192_priv *priv)
{
        struct sk_buff *skb;
	struct rtl8192_rx_info *info;

        while (NULL != (skb = skb_dequeue(&priv->skb_queue))) {
		info = (struct rtl8192_rx_info *)skb->cb;
                switch (info->out_pipe) {
		/* Nomal packet pipe */
			case 3:
				//RT_TRACE(COMP_RECV, "normal in-pipe index(%d)\n",info->out_pipe);
				priv->IrpPendingCount--;
				priv->ops->rtl819x_rx_nomal(skb);
				break;

				/* Command packet pipe */
			case 9:
				RT_TRACE(COMP_RECV, "command in-pipe index(%d)\n",\
						info->out_pipe);
				priv->ops->rtl819x_rx_cmd(skb);
				break;

			default: /* should never get here! */
				RT_TRACE(COMP_ERR, "Unknown in-pipe index(%d)\n",\
						info->out_pipe);
				dev_kfree_skb(skb);
				break;

		}
        }
}



/****************************************************************************
     ---------------------------- USB_STUFF---------------------------
*****************************************************************************/
//LZM Merge from windows HalUsbSetQueuePipeMapping8192SUsb 090319
static void HalUsbSetQueuePipeMapping8192SUsb(struct usb_interface *intf, struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	u8 i = 0;

	priv->ep_in_num = 0;
	priv->ep_out_num = 0;
	memset(priv->RtOutPipes,0,16);
	memset(priv->RtInPipes,0,16);

	iface_desc = intf->cur_altsetting;
	priv->ep_num = iface_desc->desc.bNumEndpoints;

	for (i = 0; i < priv->ep_num; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;
		if (usb_endpoint_is_bulk_in(endpoint)) {
			priv->RtInPipes[priv->ep_in_num] = usb_endpoint_num(endpoint);
			priv->ep_in_num ++;
			//printk("in_endpoint_idx = %d\n", usb_endpoint_num(endpoint));
		} else if (usb_endpoint_is_bulk_out(endpoint)) {
			priv->RtOutPipes[priv->ep_out_num] = usb_endpoint_num(endpoint);
			priv->ep_out_num ++;
			//printk("out_endpoint_idx = %d\n", usb_endpoint_num(endpoint));
		}
	}
	{
		memset(priv->txqueue_to_outpipemap,0,9);
		if (priv->ep_num == 6) {
			// BK, BE, VI, VO, HCCA, TXCMD, MGNT, HIGH, BEACON
			u8 queuetopipe[] = {3, 2, 1, 0, 4, 4, 4, 4, 4};

			memcpy(priv->txqueue_to_outpipemap,queuetopipe,9);
		} else if (priv->ep_num == 4) {
			// BK, BE, VI, VO, HCCA, TXCMD, MGNT, HIGH, BEACON
			u8 queuetopipe[] = {1, 1, 0, 0, 2, 2, 2, 2, 2};

			memcpy(priv->txqueue_to_outpipemap,queuetopipe,9);
		} else if (priv->ep_num > 9) {
			// BK, BE, VI, VO, HCCA, TXCMD, MGNT, HIGH, BEACON
			u8 queuetopipe[] = {3, 2, 1, 0, 4, 8, 7, 6, 5};

			memcpy(priv->txqueue_to_outpipemap,queuetopipe,9);
		} else {//use sigle pipe
			// BK, BE, VI, VO, HCCA, TXCMD, MGNT, HIGH, BEACON
			u8 queuetopipe[] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
			memcpy(priv->txqueue_to_outpipemap,queuetopipe,9);
		}
	}
	printk("==>ep_num:%d, in_ep_num:%d, out_ep_num:%d\n", priv->ep_num, priv->ep_in_num, priv->ep_out_num);

	printk("==>RtInPipes:");
	for(i=0; i < priv->ep_in_num; i++)
		printk("%d  ", priv->RtInPipes[i]);
	printk("\n");

	printk("==>RtOutPipes:");
	for(i=0; i < priv->ep_out_num; i++)
		printk("%d  ", priv->RtOutPipes[i]);
	printk("\n");

	printk("==>txqueue_to_outpipemap for BK, BE, VI, VO, HCCA, TXCMD, MGNT, HIGH, BEACON:\n");
	for(i=0; i < 9; i++)
		printk("%d  ", priv->txqueue_to_outpipemap[i]);
	printk("\n");

	return;
}

static const struct net_device_ops rtl8192_netdev_ops = {
	.ndo_open		= rtl8192_open,
	.ndo_stop		= rtl8192_close,
	.ndo_get_stats		= rtl8192_stats,
	.ndo_tx_timeout		= tx_timeout,
	.ndo_do_ioctl		= rtl8192_ioctl,
	.ndo_set_multicast_list	= r8192_set_multicast,
	.ndo_set_mac_address	= r8192_set_mac_adr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_start_xmit		= rtl8192_ieee80211_xmit,
};

static int __devinit rtl8192_usb_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
//	unsigned long ioaddr = 0;
	struct net_device *dev = NULL;
	struct r8192_priv *priv= NULL;
	struct usb_device *udev = interface_to_usbdev(intf);

        RT_TRACE(COMP_INIT, "Oops: i'm coming\n");

	dev = alloc_ieee80211(sizeof(struct r8192_priv));

	usb_set_intfdata(intf, dev);
	SET_NETDEV_DEV(dev, &intf->dev);
	priv = ieee80211_priv(dev);
	priv->ieee80211 = netdev_priv(dev);
	priv->udev=udev;

	HalUsbSetQueuePipeMapping8192SUsb(intf, dev);

	//printk("===============>NIC 8192SU\n");
	priv->ops = &rtl8192su_ops;

	dev->netdev_ops = &rtl8192_netdev_ops;

         //DMESG("Oops: i'm coming\n");
        dev->wireless_handlers = (struct iw_handler_def *) &r8192_wx_handlers_def;

	dev->type=ARPHRD_ETHER;

	dev->watchdog_timeo = HZ*3;	//modified by john, 0805

	if (dev_alloc_name(dev, ifname) < 0){
                RT_TRACE(COMP_INIT, "Oops: devname already taken! Trying wlan%%d...\n");
		ifname = "wlan%d";
		dev_alloc_name(dev, ifname);
        }

	RT_TRACE(COMP_INIT, "Driver probe completed1\n");
#if 1
	if(rtl8192_init(dev)!=0){
		RT_TRACE(COMP_ERR, "Initialization failed");
		goto fail;
	}
#endif
	netif_carrier_off(dev);
	netif_stop_queue(dev);

	register_netdev(dev);
	RT_TRACE(COMP_INIT, "dev name=======> %s\n",dev->name);
	rtl8192_proc_init_one(dev);


	RT_TRACE(COMP_INIT, "Driver probe completed\n");
	return 0;
fail:
	free_ieee80211(dev);

	RT_TRACE(COMP_ERR, "wlan driver load failed\n");
	return -ENODEV;
}

//detach all the work and timer structure declared or inititialize in r8192U_init function.
void rtl8192_cancel_deferred_work(struct r8192_priv* priv)
{
	cancel_work_sync(&priv->reset_wq);
	cancel_work_sync(&priv->qos_activate);
	cancel_delayed_work(&priv->watch_dog_wq);
	cancel_delayed_work(&priv->update_beacon_wq);
	cancel_delayed_work(&priv->ieee80211->hw_wakeup_wq);
	cancel_delayed_work(&priv->ieee80211->hw_sleep_wq);
	//cancel_work_sync(&priv->SetBWModeWorkItem);
	//cancel_work_sync(&priv->SwChnlWorkItem);
}

static void __devexit rtl8192_usb_disconnect(struct usb_interface *intf)
{
	struct net_device *dev = usb_get_intfdata(intf);
	struct r8192_priv *priv = ieee80211_priv(dev);
 	if(dev){

		unregister_netdev(dev);

		RT_TRACE(COMP_DOWN, "=============>wlan driver to be removed\n");
		rtl8192_proc_remove_one(dev);

			rtl8192_down(dev);
		if (priv->pFirmware)
		{
			vfree(priv->pFirmware);
			priv->pFirmware = NULL;
		}
	//	priv->rf_close(dev);
//		rtl8192_SetRFPowerState(dev, eRfOff);
		destroy_workqueue(priv->priv_wq);
		//rtl8192_irq_disable(dev);
		//rtl8192_reset(dev);
		mdelay(10);

	}
	free_ieee80211(dev);
	RT_TRACE(COMP_DOWN, "wlan driver removed\n");
}

/* fun with the built-in ieee80211 stack... */
extern int ieee80211_debug_init(void);
extern void ieee80211_debug_exit(void);
extern int ieee80211_crypto_init(void);
extern void ieee80211_crypto_deinit(void);
extern int ieee80211_crypto_tkip_init(void);
extern void ieee80211_crypto_tkip_exit(void);
extern int ieee80211_crypto_ccmp_init(void);
extern void ieee80211_crypto_ccmp_exit(void);
extern int ieee80211_crypto_wep_init(void);
extern void ieee80211_crypto_wep_exit(void);

static int __init rtl8192_usb_module_init(void)
{
	int ret;

#ifdef CONFIG_IEEE80211_DEBUG
	ret = ieee80211_debug_init();
	if (ret) {
		printk(KERN_ERR "ieee80211_debug_init() failed %d\n", ret);
		return ret;
	}
#endif
	ret = ieee80211_crypto_init();
	if (ret) {
		printk(KERN_ERR "ieee80211_crypto_init() failed %d\n", ret);
		goto fail_crypto;
	}

	ret = ieee80211_crypto_tkip_init();
	if (ret) {
		printk(KERN_ERR "ieee80211_crypto_tkip_init() failed %d\n",
			ret);
		goto fail_crypto_tkip;
	}

	ret = ieee80211_crypto_ccmp_init();
	if (ret) {
		printk(KERN_ERR "ieee80211_crypto_ccmp_init() failed %d\n",
			ret);
		goto fail_crypto_ccmp;
	}

	ret = ieee80211_crypto_wep_init();
	if (ret) {
		printk(KERN_ERR "ieee80211_crypto_wep_init() failed %d\n", ret);
		goto fail_crypto_wep;
	}

	printk(KERN_INFO "\nLinux kernel driver for RTL8192 based WLAN cards\n");
	printk(KERN_INFO "Copyright (c) 2007-2008, Realsil Wlan\n");
	RT_TRACE(COMP_INIT, "Initializing module");
	RT_TRACE(COMP_INIT, "Wireless extensions version %d", WIRELESS_EXT);

	ret = rtl8192_proc_module_init();
	if (ret) {
		pr_err("rtl8192_proc_module_init() failed %d\n", ret);
		goto fail_proc;
	}

	ret = usb_register(&rtl8192_usb_driver);
	if (ret) {
		pr_err("usb_register() failed %d\n", ret);
		goto fail_usb;
	}

	return 0;

fail_usb:
	rtl8192_proc_module_remove();
fail_proc:
	ieee80211_crypto_wep_exit();
fail_crypto_wep:
	ieee80211_crypto_ccmp_exit();
fail_crypto_ccmp:
	ieee80211_crypto_tkip_exit();
fail_crypto_tkip:
	ieee80211_crypto_deinit();
fail_crypto:
#ifdef CONFIG_IEEE80211_DEBUG
	ieee80211_debug_exit();
#endif
	return ret;
}


static void __exit rtl8192_usb_module_exit(void)
{
	usb_deregister(&rtl8192_usb_driver);

	RT_TRACE(COMP_DOWN, "Exiting");
	rtl8192_proc_module_remove();

	ieee80211_crypto_tkip_exit();
	ieee80211_crypto_ccmp_exit();
	ieee80211_crypto_wep_exit();
	ieee80211_crypto_deinit();
#ifdef CONFIG_IEEE80211_DEBUG
	ieee80211_debug_exit();
#endif
}


void rtl8192_try_wake_queue(struct net_device *dev, int pri)
{
	unsigned long flags;
	short enough_desc;
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);

	spin_lock_irqsave(&priv->tx_lock,flags);
	enough_desc = check_nic_enough_desc(dev,pri);
        spin_unlock_irqrestore(&priv->tx_lock,flags);

	if(enough_desc)
		ieee80211_wake_queue(priv->ieee80211);
}

void EnableHWSecurityConfig8192(struct net_device *dev)
{
        u8 SECR_value = 0x0;
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
	 struct ieee80211_device* ieee = priv->ieee80211;

	SECR_value = SCR_TxEncEnable | SCR_RxDecEnable;
#if 1
	if (((KEY_TYPE_WEP40 == ieee->pairwise_key_type) || (KEY_TYPE_WEP104 == ieee->pairwise_key_type)) && (priv->ieee80211->auth_mode != 2))
	{
		SECR_value |= SCR_RxUseDK;
		SECR_value |= SCR_TxUseDK;
	}
	else if ((ieee->iw_mode == IW_MODE_ADHOC) && (ieee->pairwise_key_type & (KEY_TYPE_CCMP | KEY_TYPE_TKIP)))
	{
		SECR_value |= SCR_RxUseDK;
		SECR_value |= SCR_TxUseDK;
	}
#endif
        //add HWSec active enable here.
//default using hwsec. when peer AP is in N mode only and pairwise_key_type is none_aes(which HT_IOT_ACT_PURE_N_MODE indicates it), use software security. when peer AP is in b,g,n mode mixed and pairwise_key_type is none_aes, use g mode hw security. WB on 2008.7.4

	ieee->hwsec_active = 1;

	if ((ieee->pHTInfo->IOTAction&HT_IOT_ACT_PURE_N_MODE) || !hwwep)//!ieee->hwsec_support) //add hwsec_support flag to totol control hw_sec on/off
	{
		ieee->hwsec_active = 0;
		SECR_value &= ~SCR_RxDecEnable;
	}

	RT_TRACE(COMP_SEC,"%s:, hwsec:%d, pairwise_key:%d, SECR_value:%x\n", __FUNCTION__, \
			ieee->hwsec_active, ieee->pairwise_key_type, SECR_value);
	{
                write_nic_byte(dev, SECR,  SECR_value);//SECR_value |  SCR_UseDK );
        }
}


void setKey(	struct net_device *dev,
		u8 EntryNo,
		u8 KeyIndex,
		u16 KeyType,
		u8 *MacAddr,
		u8 DefaultKey,
		u32 *KeyContent )
{
	u32 TargetCommand = 0;
	u32 TargetContent = 0;
	u16 usConfig = 0;
	u8 i;
	if (EntryNo >= TOTAL_CAM_ENTRY)
		RT_TRACE(COMP_ERR, "cam entry exceeds in setKey()\n");

	RT_TRACE(COMP_SEC, "====>to setKey(), dev:%p, EntryNo:%d, KeyIndex:%d, KeyType:%d, MacAddr"MAC_FMT"\n", dev,EntryNo, KeyIndex, KeyType, MAC_ARG(MacAddr));

	if (DefaultKey)
		usConfig |= BIT15 | (KeyType<<2);
	else
		usConfig |= BIT15 | (KeyType<<2) | KeyIndex;
//	usConfig |= BIT15 | (KeyType<<2) | (DefaultKey<<5) | KeyIndex;


	for(i=0 ; i<CAM_CONTENT_COUNT; i++){
		TargetCommand  = i+CAM_CONTENT_COUNT*EntryNo;
		TargetCommand |= BIT31|BIT16;

		if(i==0){//MAC|Config
			TargetContent = (u32)(*(MacAddr+0)) << 16|
					(u32)(*(MacAddr+1)) << 24|
					(u32)usConfig;

			write_nic_dword(dev, WCAMI, TargetContent);
			write_nic_dword(dev, RWCAM, TargetCommand);
	//		printk("setkey cam =%8x\n", read_cam(dev, i+6*EntryNo));
		}
		else if(i==1){//MAC
                        TargetContent = (u32)(*(MacAddr+2)) 	 |
                                        (u32)(*(MacAddr+3)) <<  8|
                                        (u32)(*(MacAddr+4)) << 16|
                                        (u32)(*(MacAddr+5)) << 24;
			write_nic_dword(dev, WCAMI, TargetContent);
			write_nic_dword(dev, RWCAM, TargetCommand);
		}
		else {
			//Key Material
			if(KeyContent !=NULL){
			write_nic_dword(dev, WCAMI, (u32)(*(KeyContent+i-2)) );
			write_nic_dword(dev, RWCAM, TargetCommand);
		}
	}
	}

}

/***************************************************************************
     ------------------- module init / exit stubs ----------------
****************************************************************************/
module_init(rtl8192_usb_module_init);
module_exit(rtl8192_usb_module_exit);
