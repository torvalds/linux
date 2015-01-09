/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *                                        
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
 
#ifndef __RTW_ANDROID_H__
#define __RTW_ANDROID_H__

enum ANDROID_WIFI_CMD {
	ANDROID_WIFI_CMD_START,				
	ANDROID_WIFI_CMD_STOP,			
	ANDROID_WIFI_CMD_SCAN_ACTIVE,
	ANDROID_WIFI_CMD_SCAN_PASSIVE,		
	ANDROID_WIFI_CMD_RSSI,	
	ANDROID_WIFI_CMD_LINKSPEED,
	ANDROID_WIFI_CMD_RXFILTER_START,
	ANDROID_WIFI_CMD_RXFILTER_STOP,	
	ANDROID_WIFI_CMD_RXFILTER_ADD,	
	ANDROID_WIFI_CMD_RXFILTER_REMOVE,
	ANDROID_WIFI_CMD_BTCOEXSCAN_START,
	ANDROID_WIFI_CMD_BTCOEXSCAN_STOP,
	ANDROID_WIFI_CMD_BTCOEXMODE,
	ANDROID_WIFI_CMD_SETSUSPENDOPT,
	ANDROID_WIFI_CMD_P2P_DEV_ADDR,	
	ANDROID_WIFI_CMD_SETFWPATH,		
	ANDROID_WIFI_CMD_SETBAND,		
	ANDROID_WIFI_CMD_GETBAND,			
	ANDROID_WIFI_CMD_COUNTRY,			
	ANDROID_WIFI_CMD_P2P_SET_NOA,
	ANDROID_WIFI_CMD_P2P_GET_NOA,	
	ANDROID_WIFI_CMD_P2P_SET_PS,	
	ANDROID_WIFI_CMD_SET_AP_WPS_P2P_IE,

	ANDROID_WIFI_CMD_MIRACAST,

#ifdef CONFIG_PNO_SUPPORT
	ANDROID_WIFI_CMD_PNOSSIDCLR_SET,
	ANDROID_WIFI_CMD_PNOSETUP_SET,
	ANDROID_WIFI_CMD_PNOENABLE_SET,
	ANDROID_WIFI_CMD_PNODEBUG_SET,
#endif

	ANDROID_WIFI_CMD_MACADDR,

	ANDROID_WIFI_CMD_BLOCK,

	ANDROID_WIFI_CMD_WFD_ENABLE,
	ANDROID_WIFI_CMD_WFD_DISABLE,
	
	ANDROID_WIFI_CMD_WFD_SET_TCPPORT,
	ANDROID_WIFI_CMD_WFD_SET_MAX_TPUT,
	ANDROID_WIFI_CMD_WFD_SET_DEVTYPE,
	ANDROID_WIFI_CMD_CHANGE_DTIM,
	ANDROID_WIFI_CMD_HOSTAPD_SET_MACADDR_ACL,
	ANDROID_WIFI_CMD_HOSTAPD_ACL_ADD_STA,
	ANDROID_WIFI_CMD_HOSTAPD_ACL_REMOVE_STA,
#ifdef CONFIG_GTK_OL
	ANDROID_WIFI_CMD_GTK_REKEY_OFFLOAD,
#endif //CONFIG_GTK_OL
	ANDROID_WIFI_CMD_P2P_DISABLE,
	ANDROID_WIFI_CMD_MAX
};

int rtw_android_cmdstr_to_num(char *cmdstr);
int rtw_android_priv_cmd(struct net_device *net, struct ifreq *ifr, int cmd);

#if defined(CONFIG_PNO_SUPPORT) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0))
int rtw_android_pno_enable(struct net_device *net, int pno_enable);
int rtw_android_cfg80211_pno_setup(struct net_device *net,
		struct cfg80211_ssid *ssid, int n_ssids, int interval);
#endif

#if defined(RTW_ENABLE_WIFI_CONTROL_FUNC)
int rtw_android_wifictrl_func_add(void);
void rtw_android_wifictrl_func_del(void);
void* rtw_wl_android_prealloc(int section, unsigned long size);

int wifi_get_irq_number(unsigned long *irq_flags_ptr);
int wifi_set_power(int on, unsigned long msec);
int wifi_get_mac_addr(unsigned char *buf);
void *wifi_get_country_code(char *ccode);
#else
static int rtw_android_wifictrl_func_add(void) { return 0; }
static void rtw_android_wifictrl_func_del(void) {}
#endif /* defined(RTW_ENABLE_WIFI_CONTROL_FUNC) */

#ifdef CONFIG_GPIO_WAKEUP
#ifdef CONFIG_PLATFORM_INTEL_BYT
int wifi_configure_gpio(void);
#endif //CONFIG_PLATFORM_INTEL_BYT
void wifi_free_gpio(unsigned int gpio);
#endif //CONFIG_GPIO_WAKEUP


#endif //__RTW_ANDROID_H__

