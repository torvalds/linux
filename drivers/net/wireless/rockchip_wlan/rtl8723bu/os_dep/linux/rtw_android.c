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

#ifdef CONFIG_GPIO_WAKEUP
#include <linux/gpio.h>
#endif

#include <drv_types.h>

#if defined(RTW_ENABLE_WIFI_CONTROL_FUNC)
#include <linux/platform_device.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
#include <linux/wlan_plat.h>
#else
#include <linux/wifi_tiwlan.h>
#endif
#endif /* defined(RTW_ENABLE_WIFI_CONTROL_FUNC) */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0))
#define strnicmp	strncasecmp
#endif /* Linux kernel >= 4.0.0 */

#ifdef CONFIG_GPIO_WAKEUP
#include <linux/interrupt.h>
#include <linux/irq.h>
#endif

#include "rtw_version.h"

extern void macstr2num(u8 *dst, u8 *src);

const char *android_wifi_cmd_str[ANDROID_WIFI_CMD_MAX] = {
	"START",
	"STOP",
	"SCAN-ACTIVE",
	"SCAN-PASSIVE",
	"RSSI",
	"LINKSPEED",
	"RXFILTER-START",
	"RXFILTER-STOP",
	"RXFILTER-ADD",
	"RXFILTER-REMOVE",
	"BTCOEXSCAN-START",
	"BTCOEXSCAN-STOP",
	"BTCOEXMODE",
	"SETSUSPENDOPT",
	"P2P_DEV_ADDR",
	"SETFWPATH",
	"SETBAND",
	"GETBAND",
	"COUNTRY",
	"P2P_SET_NOA",
	"P2P_GET_NOA",
	"P2P_SET_PS",
	"SET_AP_WPS_P2P_IE",

	"MIRACAST",

#ifdef CONFIG_PNO_SUPPORT
	"PNOSSIDCLR",
	"PNOSETUP",
	"PNOFORCE",
	"PNODEBUG",
#endif

	"MACADDR",

	"BLOCK_SCAN",
	"BLOCK",
	"WFD-ENABLE",
	"WFD-DISABLE",
	"WFD-SET-TCPPORT",
	"WFD-SET-MAXTPUT",
	"WFD-SET-DEVTYPE",
	"SET_DTIM",
	"HOSTAPD_SET_MACADDR_ACL",
	"HOSTAPD_ACL_ADD_STA",
	"HOSTAPD_ACL_REMOVE_STA",
#if defined(CONFIG_GTK_OL) && (LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0))
	"GTK_REKEY_OFFLOAD",
#endif //CONFIG_GTK_OL
/*	Private command for	P2P disable*/
	"P2P_DISABLE",
	"DRIVER_VERSION"
};

#ifdef CONFIG_PNO_SUPPORT
#define PNO_TLV_PREFIX			'S'
#define PNO_TLV_VERSION			'1'
#define PNO_TLV_SUBVERSION 		'2'
#define PNO_TLV_RESERVED		'0'
#define PNO_TLV_TYPE_SSID_IE	'S'
#define PNO_TLV_TYPE_TIME		'T'
#define PNO_TLV_FREQ_REPEAT		'R'
#define PNO_TLV_FREQ_EXPO_MAX	'M'

typedef struct cmd_tlv {
	char prefix;
	char version;
	char subver;
	char reserved;
} cmd_tlv_t;

#ifdef CONFIG_PNO_SET_DEBUG
char pno_in_example[] = {
		'P', 'N', 'O', 'S', 'E', 'T', 'U', 'P', ' ',
		'S', '1', '2', '0',
		'S',	//1
		0x05,
		'd', 'l', 'i', 'n', 'k',
		'S',	//2
		0x06,
		'B', 'U', 'F', 'B', 'U','F',
		'S',	//3
		0x20,
		'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '!', '@', '#', '$', '%', '^',
		'S',	//4
		0x0a,
		'!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
		'T',
		'0', '5',
		'R',
		'2',
		'M',
		'2',
		0x00
		};
#endif /* CONFIG_PNO_SET_DEBUG */
#endif /* PNO_SUPPORT */

typedef struct android_wifi_priv_cmd {
	char *buf;
	int used_len;
	int total_len;
} android_wifi_priv_cmd;

#ifdef CONFIG_COMPAT
typedef struct compat_android_wifi_priv_cmd {
	compat_uptr_t buf;
	int used_len;
	int total_len;
} compat_android_wifi_priv_cmd;
#endif /* CONFIG_COMPAT */

/**
 * Local (static) functions and variables
 */

/* Initialize g_wifi_on to 1 so dhd_bus_start will be called for the first
 * time (only) in dhd_open, subsequential wifi on will be handled by
 * wl_android_wifi_on
 */
static int g_wifi_on = _TRUE;

unsigned int oob_irq = 0;
unsigned int oob_gpio = 0;

#ifdef CONFIG_PNO_SUPPORT
/* 
 * rtw_android_pno_setup
 * Description: 
 * This is used for private command.
 * 
 * Parameter:
 * net: net_device
 * command: parameters from private command
 * total_len: the length of the command.
 *
 * */
static int rtw_android_pno_setup(struct net_device *net, char *command, int total_len) {
	pno_ssid_t pno_ssids_local[MAX_PNO_LIST_COUNT];
	int res = -1;
	int nssid = 0;
	cmd_tlv_t *cmd_tlv_temp;
	char *str_ptr;
	int tlv_size_left;
	int pno_time = 0;
	int pno_repeat = 0;
	int pno_freq_expo_max = 0;
	int cmdlen = strlen(android_wifi_cmd_str[ANDROID_WIFI_CMD_PNOSETUP_SET]) + 1; 

#ifdef CONFIG_PNO_SET_DEBUG
	int i;
	char *p;
	p = pno_in_example;

	total_len = sizeof(pno_in_example);
	str_ptr = p + cmdlen;
#else
	str_ptr = command + cmdlen;
#endif

	if (total_len < (cmdlen + sizeof(cmd_tlv_t))) {
		DBG_871X("%s argument=%d less min size\n", __func__, total_len);
		goto exit_proc;
	}

	tlv_size_left = total_len - cmdlen;

	cmd_tlv_temp = (cmd_tlv_t *)str_ptr;
	memset(pno_ssids_local, 0, sizeof(pno_ssids_local));

	if ((cmd_tlv_temp->prefix == PNO_TLV_PREFIX) &&
		(cmd_tlv_temp->version == PNO_TLV_VERSION) &&
		(cmd_tlv_temp->subver == PNO_TLV_SUBVERSION)) {

		str_ptr += sizeof(cmd_tlv_t);
		tlv_size_left -= sizeof(cmd_tlv_t);

		if ((nssid = rtw_parse_ssid_list_tlv(&str_ptr, pno_ssids_local,
			MAX_PNO_LIST_COUNT, &tlv_size_left)) <= 0) {
			DBG_871X("SSID is not presented or corrupted ret=%d\n", nssid);
			goto exit_proc;
		} else {
			if ((str_ptr[0] != PNO_TLV_TYPE_TIME) || (tlv_size_left <= 1)) {
				DBG_871X("%s scan duration corrupted field size %d\n",
					__func__, tlv_size_left);
				goto exit_proc;
			}
			str_ptr++;
			pno_time = simple_strtoul(str_ptr, &str_ptr, 16);
			DBG_871X("%s: pno_time=%d\n", __func__, pno_time);

			if (str_ptr[0] != 0) {
				if ((str_ptr[0] != PNO_TLV_FREQ_REPEAT)) {
					DBG_871X("%s pno repeat : corrupted field\n",
						__func__);
					goto exit_proc;
				}
				str_ptr++;
				pno_repeat = simple_strtoul(str_ptr, &str_ptr, 16);
				DBG_871X("%s :got pno_repeat=%d\n", __FUNCTION__, pno_repeat);
				if (str_ptr[0] != PNO_TLV_FREQ_EXPO_MAX) {
					DBG_871X("%s FREQ_EXPO_MAX corrupted field size\n",
						__func__);
					goto exit_proc;
				}
				str_ptr++;
				pno_freq_expo_max = simple_strtoul(str_ptr, &str_ptr, 16);
				DBG_871X("%s: pno_freq_expo_max=%d\n",
					__func__, pno_freq_expo_max);
			}
		}
	} else {
		DBG_871X("%s get wrong TLV command\n", __FUNCTION__);
		goto exit_proc;
	}

	res = rtw_dev_pno_set(net, pno_ssids_local, nssid, pno_time, pno_repeat, pno_freq_expo_max);

#ifdef CONFIG_PNO_SET_DEBUG
	rtw_dev_pno_debug(net);
#endif

exit_proc:
	return res;
}

/* 
 * rtw_android_cfg80211_pno_setup
 * Description: 
 * This is used for cfg80211 sched_scan.
 * 
 * Parameter:
 * net: net_device
 * request: cfg80211_request
 * */

int rtw_android_cfg80211_pno_setup(struct net_device *net,
		struct cfg80211_ssid *ssids, int n_ssids, int interval) {
	int res = -1;
	int nssid = 0;
	int pno_time = 0;
	int pno_repeat = 0;
	int pno_freq_expo_max = 0;
	int index = 0;
	pno_ssid_t pno_ssids_local[MAX_PNO_LIST_COUNT];

	if (n_ssids > MAX_PNO_LIST_COUNT || n_ssids < 0) {
		DBG_871X("%s: nssids(%d) is invalid.\n", __func__, n_ssids);
		return -EINVAL;
	}

	memset(pno_ssids_local, 0, sizeof(pno_ssids_local));

	nssid = n_ssids;

	for (index = 0 ; index < nssid ; index++) {
		pno_ssids_local[index].SSID_len = ssids[index].ssid_len;
		memcpy(pno_ssids_local[index].SSID, ssids[index].ssid,
				ssids[index].ssid_len);
	}

	pno_time = (interval / 1000);

	DBG_871X("%s: nssids: %d, pno_time=%d\n", __func__, nssid, pno_time);

	res = rtw_dev_pno_set(net, pno_ssids_local, nssid, pno_time,
			pno_repeat, pno_freq_expo_max);

exit_proc:
	return res;
}

int rtw_android_pno_enable(struct net_device *net, int pno_enable) {
	_adapter *padapter = (_adapter *)rtw_netdev_priv(net);
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);

	if (pwrctl) {
		pwrctl->wowlan_pno_enable = pno_enable;
		DBG_871X("%s: wowlan_pno_enable: %d\n", __func__, pwrctl->wowlan_pno_enable);
		if (pwrctl->wowlan_pno_enable == 0) {
			if (pwrctl->pnlo_info != NULL) {
				rtw_mfree((u8 *)pwrctl->pnlo_info, sizeof(pno_nlo_info_t));
				pwrctl->pnlo_info = NULL;
			}
			if (pwrctl->pno_ssid_list != NULL) {
				rtw_mfree((u8 *)pwrctl->pno_ssid_list, sizeof(pno_ssid_list_t));
				pwrctl->pno_ssid_list = NULL;
			}
			if (pwrctl->pscan_info != NULL) {
				rtw_mfree((u8 *)pwrctl->pscan_info, sizeof(pno_scan_info_t));
				pwrctl->pscan_info = NULL;
			}
		} 
		return 0;
	} else {
		return -1;
	}
}
#endif //CONFIG_PNO_SUPPORT

int rtw_android_cmdstr_to_num(char *cmdstr)
{
	int cmd_num;
	for(cmd_num=0 ; cmd_num<ANDROID_WIFI_CMD_MAX; cmd_num++)
		if(0 == strnicmp(cmdstr , android_wifi_cmd_str[cmd_num], strlen(android_wifi_cmd_str[cmd_num])) )
			break;

	return cmd_num;
}

int rtw_android_get_rssi(struct net_device *net, char *command, int total_len)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(net);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);	
	struct	wlan_network	*pcur_network = &pmlmepriv->cur_network;
	int bytes_written = 0;

	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) {	
		bytes_written += snprintf(&command[bytes_written], total_len, "%s rssi %d", 
			pcur_network->network.Ssid.Ssid, padapter->recvpriv.rssi);
	}

	return bytes_written;
}

int rtw_android_get_link_speed(struct net_device *net, char *command, int total_len)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(net);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);	
	struct	wlan_network	*pcur_network = &pmlmepriv->cur_network;
	int bytes_written = 0;
	u16 link_speed = 0;

	link_speed = rtw_get_cur_max_rate(padapter)/10;
	bytes_written = snprintf(command, total_len, "LinkSpeed %d", link_speed);

	return bytes_written;
}

int rtw_android_get_macaddr(struct net_device *net, char *command, int total_len)
{
	_adapter *adapter = (_adapter *)rtw_netdev_priv(net);
	int bytes_written = 0;
	
	bytes_written = snprintf(command, total_len, "Macaddr = "MAC_FMT, MAC_ARG(net->dev_addr));
	return bytes_written;
}

int rtw_android_set_country(struct net_device *net, char *command, int total_len)
{
	_adapter *adapter = (_adapter *)rtw_netdev_priv(net);
	char *country_code = command + strlen(android_wifi_cmd_str[ANDROID_WIFI_CMD_COUNTRY]) + 1;
	int ret = _FAIL;
	
	ret = rtw_set_country(adapter, country_code);

	return (ret==_SUCCESS)?0:-1;
}

int rtw_android_get_p2p_dev_addr(struct net_device *net, char *command, int total_len)
{
	int bytes_written = 0;

	//We use the same address as our HW MAC address
	_rtw_memcpy(command, net->dev_addr, ETH_ALEN);
	
	bytes_written = ETH_ALEN;
	return bytes_written;
}

int rtw_android_set_block_scan(struct net_device *net, char *command, int total_len)
{
	_adapter *adapter = (_adapter *)rtw_netdev_priv(net);
	char *block_value = command + strlen(android_wifi_cmd_str[ANDROID_WIFI_CMD_BLOCK_SCAN]) + 1;

	#ifdef CONFIG_IOCTL_CFG80211
	adapter_wdev_data(adapter)->block_scan = (*block_value == '0')?_FALSE:_TRUE;
	#endif

	return 0;
}

int rtw_android_set_block(struct net_device *net, char *command, int total_len)
{
	_adapter *adapter = (_adapter *)rtw_netdev_priv(net);
	char *block_value = command + strlen(android_wifi_cmd_str[ANDROID_WIFI_CMD_BLOCK]) + 1;

	#ifdef CONFIG_IOCTL_CFG80211
	adapter_wdev_data(adapter)->block = (*block_value=='0')?_FALSE:_TRUE;
	#endif
	
	return 0;
}

int rtw_android_setband(struct net_device *net, char *command, int total_len)
{
	_adapter *adapter = (_adapter *)rtw_netdev_priv(net);
	char *arg = command + strlen(android_wifi_cmd_str[ANDROID_WIFI_CMD_SETBAND]) + 1;
	u32 band = WIFI_FREQUENCY_BAND_AUTO;
	int ret = _FAIL;

	if (sscanf(arg, "%u", &band) >= 1)
		ret = rtw_set_band(adapter, band);

	return (ret==_SUCCESS)?0:-1;
}

int rtw_android_getband(struct net_device *net, char *command, int total_len)
{
	_adapter *adapter = (_adapter *)rtw_netdev_priv(net);
	int bytes_written = 0;

	bytes_written = snprintf(command, total_len, "%u", adapter->setband);

	return bytes_written;
}

#ifdef CONFIG_WFD
int rtw_android_set_miracast_mode(struct net_device *net, char *command, int total_len)
{
	_adapter *adapter = (_adapter *)rtw_netdev_priv(net);
	struct wifi_display_info *wfd_info = &adapter->wfd_info;
	char *arg = command + strlen(android_wifi_cmd_str[ANDROID_WIFI_CMD_MIRACAST]) + 1;
	u8 mode;
	int num;
	int ret = _FAIL;

	num = sscanf(arg, "%hhu", &mode);

	if (num < 1)
		goto exit;

	switch (mode) {
	case 1: /* soruce */
		mode = MIRACAST_SOURCE;
		break;
	case 2: /* sink */
		mode = MIRACAST_SINK;
		break;
	case 0: /* disabled */
	default:
		mode = MIRACAST_DISABLED;
		break;
	}
	wfd_info->stack_wfd_mode = mode;
	DBG_871X("stack miracast mode: %s\n", get_miracast_mode_str(wfd_info->stack_wfd_mode));

	ret = _SUCCESS;

exit:
	return (ret == _SUCCESS)?0:-1;
}
#endif /* CONFIG_WFD */

int get_int_from_command( char* pcmd )
{
	int i = 0;

	for( i = 0; i < strlen( pcmd ); i++ )
	{
		if ( pcmd[ i ] == '=' )
		{
			//	Skip the '=' and space characters.
			i += 2;
			break;
		}
	}
	return ( rtw_atoi( pcmd + i ) );
}

#if defined(CONFIG_GTK_OL) && (LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0))
int rtw_gtk_offload(struct net_device *net, u8 *cmd_ptr)
{
	int i;
	//u8 *cmd_ptr = priv_cmd.buf;
	struct sta_info * psta;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(net);
	struct mlme_priv 	*pmlmepriv = &padapter->mlmepriv;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct security_priv* psecuritypriv=&(padapter->securitypriv);
	psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
	
	
	if (psta == NULL) 
	{
		DBG_8192C("%s, : Obtain Sta_info fail \n", __func__);
	}
	else
	{
		//string command length of "GTK_REKEY_OFFLOAD"
		cmd_ptr += 18;
		
		_rtw_memcpy(psta->kek, cmd_ptr, RTW_KEK_LEN);
		cmd_ptr += RTW_KEK_LEN;
		/*
		printk("supplicant KEK: ");
		for(i=0;i<RTW_KEK_LEN; i++)
			printk(" %02x ", psta->kek[i]);
		printk("\n supplicant KCK: ");
		*/
		_rtw_memcpy(psta->kck, cmd_ptr, RTW_KCK_LEN);
		cmd_ptr += RTW_KCK_LEN;
		/*
		for(i=0;i<RTW_KEK_LEN; i++)
			printk(" %02x ", psta->kck[i]);
		*/
		_rtw_memcpy(psta->replay_ctr, cmd_ptr, RTW_REPLAY_CTR_LEN);
		psecuritypriv->binstallKCK_KEK = _TRUE;
		
		//printk("\nREPLAY_CTR: ");
		//for(i=0;i<RTW_REPLAY_CTR_LEN; i++)
			//printk(" %02x ", psta->replay_ctr[i]);
	}

	return _SUCCESS;
}
#endif //CONFIG_GTK_OL

int rtw_android_priv_cmd(struct net_device *net, struct ifreq *ifr, int cmd)
{
	int ret = 0;
	char *command = NULL;
	int cmd_num;
	int bytes_written = 0;
#ifdef CONFIG_PNO_SUPPORT
	uint cmdlen = 0;
	uint pno_enable = 0;
#endif
	android_wifi_priv_cmd priv_cmd;
	_adapter*	padapter = ( _adapter * ) rtw_netdev_priv(net);
#ifdef CONFIG_WFD
	struct wifi_display_info		*pwfd_info;
#endif

	rtw_lock_suspend();

	if (!ifr->ifr_data) {
		ret = -EINVAL;
		goto exit;
	}
	if (padapter->registrypriv.mp_mode == 1) {
			ret = -EINVAL;
			goto exit;
	}
#ifdef CONFIG_COMPAT
	if (is_compat_task()) {
		/* User space is 32-bit, use compat ioctl */
		compat_android_wifi_priv_cmd compat_priv_cmd;

		if (copy_from_user(&compat_priv_cmd, ifr->ifr_data, sizeof(compat_android_wifi_priv_cmd))) {
			ret = -EFAULT;
			goto exit;
		}
		priv_cmd.buf = compat_ptr(compat_priv_cmd.buf);
		priv_cmd.used_len = compat_priv_cmd.used_len;
		priv_cmd.total_len = compat_priv_cmd.total_len;
	} else
#endif /* CONFIG_COMPAT */
	if (copy_from_user(&priv_cmd, ifr->ifr_data, sizeof(android_wifi_priv_cmd))) {
		ret = -EFAULT;
		goto exit;
	}
	if ( padapter->registrypriv.mp_mode == 1) {
		ret = -EFAULT;
		goto exit;
	}
	/*DBG_871X("%s priv_cmd.buf=%p priv_cmd.total_len=%d  priv_cmd.used_len=%d\n",__func__,priv_cmd.buf,priv_cmd.total_len,priv_cmd.used_len);*/
	command = rtw_zmalloc(priv_cmd.total_len);
	if (!command)
	{
		DBG_871X("%s: failed to allocate memory\n", __FUNCTION__);
		ret = -ENOMEM;
		goto exit;
	}

	if (!access_ok(VERIFY_READ, priv_cmd.buf, priv_cmd.total_len)){
	 	DBG_871X("%s: failed to access memory\n", __FUNCTION__);
		ret = -EFAULT;
		goto exit;
	 }
	if (copy_from_user(command, (void *)priv_cmd.buf, priv_cmd.total_len)) {
		ret = -EFAULT;
		goto exit;
	}

	DBG_871X("%s: Android private cmd \"%s\" on %s\n"
		, __FUNCTION__, command, ifr->ifr_name);

	cmd_num = rtw_android_cmdstr_to_num(command);
	
	switch(cmd_num) {
	case ANDROID_WIFI_CMD_START:
		//bytes_written = wl_android_wifi_on(net);
		goto response;
	case ANDROID_WIFI_CMD_SETFWPATH:
		goto response;
	}

	if (!g_wifi_on) {
		DBG_871X("%s: Ignore private cmd \"%s\" - iface %s is down\n"
			,__FUNCTION__, command, ifr->ifr_name);
		ret = 0;
		goto exit;
	}

	if (!hal_chk_wl_func(padapter, WL_FUNC_MIRACAST)) {
		switch (cmd_num) {
		case ANDROID_WIFI_CMD_WFD_ENABLE:
		case ANDROID_WIFI_CMD_WFD_DISABLE:
		case ANDROID_WIFI_CMD_WFD_SET_TCPPORT:
		case ANDROID_WIFI_CMD_WFD_SET_MAX_TPUT:
		case ANDROID_WIFI_CMD_WFD_SET_DEVTYPE:
			goto response;
		}
	}

	switch(cmd_num) {

	case ANDROID_WIFI_CMD_STOP:
		//bytes_written = wl_android_wifi_off(net);
		break;
		
	case ANDROID_WIFI_CMD_SCAN_ACTIVE:
		//rtw_set_scan_mode((_adapter *)rtw_netdev_priv(net), SCAN_ACTIVE);
#ifdef CONFIG_PLATFORM_MSTAR
#ifdef CONFIG_IOCTL_CFG80211
		adapter_wdev_data((_adapter *)rtw_netdev_priv(net))->bandroid_scan = _TRUE;
#endif //CONFIG_IOCTL_CFG80211
#endif //CONFIG_PLATFORM_MSTAR
		break;
	case ANDROID_WIFI_CMD_SCAN_PASSIVE:
		//rtw_set_scan_mode((_adapter *)rtw_netdev_priv(net), SCAN_PASSIVE);
		break;
		
	case ANDROID_WIFI_CMD_RSSI:
		bytes_written = rtw_android_get_rssi(net, command, priv_cmd.total_len);
		break;
	case ANDROID_WIFI_CMD_LINKSPEED:
		bytes_written = rtw_android_get_link_speed(net, command, priv_cmd.total_len);
		break;

	case ANDROID_WIFI_CMD_MACADDR:
		bytes_written = rtw_android_get_macaddr(net, command, priv_cmd.total_len);
		break;

	case ANDROID_WIFI_CMD_BLOCK_SCAN:
		bytes_written = rtw_android_set_block_scan(net, command, priv_cmd.total_len);
		break;

	case ANDROID_WIFI_CMD_BLOCK:
		bytes_written = rtw_android_set_block(net, command, priv_cmd.total_len);
		break;
		
	case ANDROID_WIFI_CMD_RXFILTER_START:
		//bytes_written = net_os_set_packet_filter(net, 1);
		break;
	case ANDROID_WIFI_CMD_RXFILTER_STOP:
		//bytes_written = net_os_set_packet_filter(net, 0);
		break;
	case ANDROID_WIFI_CMD_RXFILTER_ADD:
		//int filter_num = *(command + strlen(CMD_RXFILTER_ADD) + 1) - '0';
		//bytes_written = net_os_rxfilter_add_remove(net, TRUE, filter_num);
		break;
	case ANDROID_WIFI_CMD_RXFILTER_REMOVE:
		//int filter_num = *(command + strlen(CMD_RXFILTER_REMOVE) + 1) - '0';
		//bytes_written = net_os_rxfilter_add_remove(net, FALSE, filter_num);
		break;
		
	case ANDROID_WIFI_CMD_BTCOEXSCAN_START:
		/* TBD: BTCOEXSCAN-START */
		break;
	case ANDROID_WIFI_CMD_BTCOEXSCAN_STOP:
		/* TBD: BTCOEXSCAN-STOP */
		break;
	case ANDROID_WIFI_CMD_BTCOEXMODE:
		#if 0
		uint mode = *(command + strlen(CMD_BTCOEXMODE) + 1) - '0';
		if (mode == 1)
			net_os_set_packet_filter(net, 0); /* DHCP starts */
		else
			net_os_set_packet_filter(net, 1); /* DHCP ends */
#ifdef WL_CFG80211
		bytes_written = wl_cfg80211_set_btcoex_dhcp(net, command);
#endif
		#endif
		break;
		
	case ANDROID_WIFI_CMD_SETSUSPENDOPT:
		//bytes_written = wl_android_set_suspendopt(net, command, priv_cmd.total_len);
		break;
		
	case ANDROID_WIFI_CMD_SETBAND:
		bytes_written = rtw_android_setband(net, command, priv_cmd.total_len);
		break;

	case ANDROID_WIFI_CMD_GETBAND:
		bytes_written = rtw_android_getband(net, command, priv_cmd.total_len);
		break;

	case ANDROID_WIFI_CMD_COUNTRY:
		bytes_written = rtw_android_set_country(net, command, priv_cmd.total_len);
		break;
		
#ifdef CONFIG_PNO_SUPPORT
	case ANDROID_WIFI_CMD_PNOSSIDCLR_SET:
		//bytes_written = dhd_dev_pno_reset(net);
		break;
	case ANDROID_WIFI_CMD_PNOSETUP_SET:
		bytes_written = rtw_android_pno_setup(net, command, priv_cmd.total_len);
		break;
	case ANDROID_WIFI_CMD_PNOENABLE_SET:
		cmdlen = strlen(android_wifi_cmd_str[ANDROID_WIFI_CMD_PNOENABLE_SET]);
		pno_enable = *(command + cmdlen + 1) - '0';
		bytes_written = rtw_android_pno_enable(net, pno_enable);
		break;
#endif

	case ANDROID_WIFI_CMD_P2P_DEV_ADDR:
		bytes_written = rtw_android_get_p2p_dev_addr(net, command, priv_cmd.total_len);
		break;
	case ANDROID_WIFI_CMD_P2P_SET_NOA:
		//int skip = strlen(CMD_P2P_SET_NOA) + 1;
		//bytes_written = wl_cfg80211_set_p2p_noa(net, command + skip, priv_cmd.total_len - skip);
		break;
	case ANDROID_WIFI_CMD_P2P_GET_NOA:
		//bytes_written = wl_cfg80211_get_p2p_noa(net, command, priv_cmd.total_len);
		break;
	case ANDROID_WIFI_CMD_P2P_SET_PS:
		//int skip = strlen(CMD_P2P_SET_PS) + 1;
		//bytes_written = wl_cfg80211_set_p2p_ps(net, command + skip, priv_cmd.total_len - skip);
		break;
		
#ifdef CONFIG_IOCTL_CFG80211
	case ANDROID_WIFI_CMD_SET_AP_WPS_P2P_IE:
	{
		int skip = strlen(android_wifi_cmd_str[ANDROID_WIFI_CMD_SET_AP_WPS_P2P_IE]) + 3;
		bytes_written = rtw_cfg80211_set_mgnt_wpsp2pie(net, command + skip, priv_cmd.total_len - skip, *(command + skip - 2) - '0');
		break;
	}
#endif //CONFIG_IOCTL_CFG80211

#ifdef CONFIG_WFD

	case ANDROID_WIFI_CMD_MIRACAST:
		bytes_written = rtw_android_set_miracast_mode(net, command, priv_cmd.total_len);
		break;

	case ANDROID_WIFI_CMD_WFD_ENABLE:
	{
		//	Commented by Albert 2012/07/24
		//	We can enable the WFD function by using the following command:
		//	wpa_cli driver wfd-enable

		if (padapter->wdinfo.driver_interface == DRIVER_CFG80211)
			rtw_wfd_enable(padapter, 1);
		break;
	}

	case ANDROID_WIFI_CMD_WFD_DISABLE:
	{
		//	Commented by Albert 2012/07/24
		//	We can disable the WFD function by using the following command:
		//	wpa_cli driver wfd-disable

		if (padapter->wdinfo.driver_interface == DRIVER_CFG80211)
			rtw_wfd_enable(padapter, 0);
		break;
	}
	case ANDROID_WIFI_CMD_WFD_SET_TCPPORT:
	{
		//	Commented by Albert 2012/07/24
		//	We can set the tcp port number by using the following command:
		//	wpa_cli driver wfd-set-tcpport = 554

		if (padapter->wdinfo.driver_interface == DRIVER_CFG80211)
			rtw_wfd_set_ctrl_port(padapter, (u16)get_int_from_command(priv_cmd.buf));
		break;
	}
	case ANDROID_WIFI_CMD_WFD_SET_MAX_TPUT:
	{
		break;
	}
	case ANDROID_WIFI_CMD_WFD_SET_DEVTYPE:
	{
		//	Commented by Albert 2012/08/28
		//	Specify the WFD device type ( WFD source/primary sink )

		pwfd_info = &padapter->wfd_info;
		if( padapter->wdinfo.driver_interface == DRIVER_CFG80211 )
		{
			pwfd_info->wfd_device_type = ( u8 ) get_int_from_command( priv_cmd.buf );
			pwfd_info->wfd_device_type &= WFD_DEVINFO_DUAL;
		}
		break;
	}
#endif
	case ANDROID_WIFI_CMD_CHANGE_DTIM:
		{
#ifdef CONFIG_LPS
			u8 dtim;
			u8 *ptr =(u8 *) &priv_cmd.buf;
			
			ptr += 9;//string command length of  "SET_DTIM";

			dtim = rtw_atoi(ptr);

			DBG_871X("DTIM=%d\n", dtim);

			rtw_lps_change_dtim_cmd(padapter, dtim);			
#endif			
		}		
		break;
	case ANDROID_WIFI_CMD_HOSTAPD_SET_MACADDR_ACL:
	{
		padapter->stapriv.acl_list.mode = ( u8 ) get_int_from_command(command);
		DBG_871X("%s ANDROID_WIFI_CMD_HOSTAPD_SET_MACADDR_ACL mode:%d\n", __FUNCTION__, padapter->stapriv.acl_list.mode);
		break;
	}
	case ANDROID_WIFI_CMD_HOSTAPD_ACL_ADD_STA:
	{
		u8 addr[ETH_ALEN] = {0x00};
		macstr2num(addr, command+strlen("HOSTAPD_ACL_ADD_STA")+3);	// 3 is space bar + "=" + space bar these 3 chars
		rtw_acl_add_sta(padapter, addr);
		break;
	}
	case ANDROID_WIFI_CMD_HOSTAPD_ACL_REMOVE_STA:
	{
		u8 addr[ETH_ALEN] = {0x00};
		macstr2num(addr, command+strlen("HOSTAPD_ACL_REMOVE_STA")+3);	// 3 is space bar + "=" + space bar these 3 chars
		rtw_acl_remove_sta(padapter, addr);
		break;
	}
#if defined(CONFIG_GTK_OL) && (LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0))
	case ANDROID_WIFI_CMD_GTK_REKEY_OFFLOAD:
		rtw_gtk_offload(net, (u8*)command);
		break;
#endif //CONFIG_GTK_OL		
	case ANDROID_WIFI_CMD_P2P_DISABLE:
	{
#ifdef CONFIG_P2P
		struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;	
		u8 channel, ch_offset;
		u16 bwmode;

		rtw_p2p_enable(padapter, P2P_ROLE_DISABLE);
#endif // CONFIG_P2P
		break;
	}
	case ANDROID_WIFI_CMD_DRIVERVERSION:
	{
		bytes_written = strlen(DRIVERVERSION);
		snprintf(command, bytes_written+1, DRIVERVERSION);
		break;
	}
	default:
		DBG_871X("Unknown PRIVATE command %s - ignored\n", command);
		snprintf(command, 3, "OK");
		bytes_written = strlen("OK");
	}

response:
	if (bytes_written >= 0) {
		if ((bytes_written == 0) && (priv_cmd.total_len > 0))
			command[0] = '\0';
		if (bytes_written >= priv_cmd.total_len) {
			DBG_871X("%s: bytes_written = %d\n", __FUNCTION__, bytes_written);
			bytes_written = priv_cmd.total_len;
		} else {
			bytes_written++;
		}
		priv_cmd.used_len = bytes_written;
		if (copy_to_user((void *)priv_cmd.buf, command, bytes_written)) {
			DBG_871X("%s: failed to copy data to user buffer\n", __FUNCTION__);
			ret = -EFAULT;
		}
	}
	else {
		ret = bytes_written;
	}

exit:
	rtw_unlock_suspend();
	if (command) {
		rtw_mfree(command, priv_cmd.total_len);
	}

	return ret;
}


/**
 * Functions for Android WiFi card detection
 */
#if defined(RTW_ENABLE_WIFI_CONTROL_FUNC)

static int g_wifidev_registered = 0;
static struct semaphore wifi_control_sem;
static struct wifi_platform_data *wifi_control_data = NULL;
static struct resource *wifi_irqres = NULL;

static int wifi_add_dev(void);
static void wifi_del_dev(void);

int rtw_android_wifictrl_func_add(void)
{
	int ret = 0;
	sema_init(&wifi_control_sem, 0);

	ret = wifi_add_dev();
	if (ret) {
		DBG_871X("%s: platform_driver_register failed\n", __FUNCTION__);
		return ret;
	}
	g_wifidev_registered = 1;

	/* Waiting callback after platform_driver_register is done or exit with error */
	if (down_timeout(&wifi_control_sem,  msecs_to_jiffies(1000)) != 0) {
		ret = -EINVAL;
		DBG_871X("%s: platform_driver_register timeout\n", __FUNCTION__);
	}

	return ret;
}

void rtw_android_wifictrl_func_del(void)
{
	if (g_wifidev_registered)
	{
		wifi_del_dev();
		g_wifidev_registered = 0;
	}
}

void *wl_android_prealloc(int section, unsigned long size)
{
	void *alloc_ptr = NULL;
	if (wifi_control_data && wifi_control_data->mem_prealloc) {
		alloc_ptr = wifi_control_data->mem_prealloc(section, size);
		if (alloc_ptr) {
			DBG_871X("success alloc section %d\n", section);
			if (size != 0L)
				memset(alloc_ptr, 0, size);
			return alloc_ptr;
		}
	}

	DBG_871X("can't alloc section %d\n", section);
	return NULL;
}

int wifi_get_irq_number(unsigned long *irq_flags_ptr)
{
	if (wifi_irqres) {
		*irq_flags_ptr = wifi_irqres->flags & IRQF_TRIGGER_MASK;
		return (int)wifi_irqres->start;
	}
#ifdef CUSTOM_OOB_GPIO_NUM
	return CUSTOM_OOB_GPIO_NUM;
#else
	return -1;
#endif
}

int wifi_set_power(int on, unsigned long msec)
{
	DBG_871X("%s = %d\n", __FUNCTION__, on);
	if (wifi_control_data && wifi_control_data->set_power) {
		wifi_control_data->set_power(on);
	}
	if (msec)
		msleep(msec);
	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
int wifi_get_mac_addr(unsigned char *buf)
{
	DBG_871X("%s\n", __FUNCTION__);
	if (!buf)
		return -EINVAL;
	if (wifi_control_data && wifi_control_data->get_mac_addr) {
		return wifi_control_data->get_mac_addr(buf);
	}
	return -EOPNOTSUPP;
}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)) */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)) || defined(COMPAT_KERNEL_RELEASE)
void *wifi_get_country_code(char *ccode)
{
	DBG_871X("%s\n", __FUNCTION__);
	if (!ccode)
		return NULL;
	if (wifi_control_data && wifi_control_data->get_country_code) {
		return wifi_control_data->get_country_code(ccode);
	}
	return NULL;
}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)) */

static int wifi_set_carddetect(int on)
{
	DBG_871X("%s = %d\n", __FUNCTION__, on);
	if (wifi_control_data && wifi_control_data->set_carddetect) {
		wifi_control_data->set_carddetect(on);
	}
	return 0;
}

static int wifi_probe(struct platform_device *pdev)
{
	struct wifi_platform_data *wifi_ctrl =
		(struct wifi_platform_data *)(pdev->dev.platform_data);
	int wifi_wake_gpio = 0;

	DBG_871X("## %s\n", __FUNCTION__);
	wifi_irqres = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "bcmdhd_wlan_irq");

	if (wifi_irqres == NULL)
		wifi_irqres = platform_get_resource_byname(pdev,
			IORESOURCE_IRQ, "bcm4329_wlan_irq");
	else
		wifi_wake_gpio = wifi_irqres->start;

#ifdef CONFIG_GPIO_WAKEUP
	printk("%s: gpio:%d wifi_wake_gpio:%d\n", __func__,
			wifi_irqres->start, wifi_wake_gpio);

	if (wifi_wake_gpio > 0) {
#ifdef CONFIG_PLATFORM_INTEL_BYT
		wifi_configure_gpio();
#else //CONFIG_PLATFORM_INTEL_BYT
		gpio_request(wifi_wake_gpio, "oob_irq");
		gpio_direction_input(wifi_wake_gpio);
		oob_irq = gpio_to_irq(wifi_wake_gpio);
#endif //CONFIG_PLATFORM_INTEL_BYT
		printk("%s oob_irq:%d\n", __func__, oob_irq);
	}
	else if(wifi_irqres)
	{
		oob_irq = wifi_irqres->start;
		printk("%s oob_irq:%d\n", __func__, oob_irq);
	}
#endif
	wifi_control_data = wifi_ctrl;

	wifi_set_power(1, 0);	/* Power On */
	wifi_set_carddetect(1);	/* CardDetect (0->1) */

	up(&wifi_control_sem);
	return 0;
}

#ifdef RTW_SUPPORT_PLATFORM_SHUTDOWN
extern PADAPTER g_test_adapter;

static void shutdown_card(void)
{
	u32 addr;
	u8 tmp8, cnt=0;

	if (NULL == g_test_adapter)
	{
		DBG_871X("%s: padapter==NULL\n", __FUNCTION__);
		return;
	}

#ifdef CONFIG_FWLPS_IN_IPS
	LeaveAllPowerSaveMode(g_test_adapter);
#endif // CONFIG_FWLPS_IN_IPS

	// Leave SDIO HCI Suspend
	addr = 0x10250086;
	rtw_write8(g_test_adapter, addr, 0);
	do {
		tmp8 = rtw_read8(g_test_adapter, addr);
		cnt++;
		DBG_871X(FUNC_ADPT_FMT ": polling SDIO_HSUS_CTRL(0x%x)=0x%x, cnt=%d\n",
			FUNC_ADPT_ARG(g_test_adapter), addr, tmp8, cnt);

		if (tmp8 & BIT(1))
			break;

		if (cnt >= 100)
		{
			DBG_871X(FUNC_ADPT_FMT ": polling 0x%x[1]==1 FAIL!!\n",
				FUNC_ADPT_ARG(g_test_adapter), addr);
			break;
		}

		rtw_mdelay_os(10);
	} while (1);

	// unlock register I/O
	rtw_write8(g_test_adapter, 0x1C, 0);

	// enable power down function
	// 0x04[4] = 1
	// 0x05[7] = 1
	addr = 0x04;
	tmp8 = rtw_read8(g_test_adapter, addr);
	tmp8 |= BIT(4);
	rtw_write8(g_test_adapter, addr, tmp8);
	DBG_871X(FUNC_ADPT_FMT ": read after write 0x%x=0x%x\n",
		FUNC_ADPT_ARG(g_test_adapter), addr, rtw_read8(g_test_adapter, addr));

	addr = 0x05;
	tmp8 = rtw_read8(g_test_adapter, addr);
	tmp8 |= BIT(7);
	rtw_write8(g_test_adapter, addr, tmp8);
	DBG_871X(FUNC_ADPT_FMT ": read after write 0x%x=0x%x\n",
		FUNC_ADPT_ARG(g_test_adapter), addr, rtw_read8(g_test_adapter, addr));

	// lock register page0 0x0~0xB read/write
	rtw_write8(g_test_adapter, 0x1C, 0x0E);

	rtw_set_surprise_removed(g_test_adapter);
	DBG_871X(FUNC_ADPT_FMT ": bSurpriseRemoved=%s\n",
		FUNC_ADPT_ARG(g_test_adapter), rtw_is_surprise_removed(g_test_adapter)?"True":"False");
}
#endif // RTW_SUPPORT_PLATFORM_SHUTDOWN

static int wifi_remove(struct platform_device *pdev)
{
	struct wifi_platform_data *wifi_ctrl =
		(struct wifi_platform_data *)(pdev->dev.platform_data);

	DBG_871X("## %s\n", __FUNCTION__);
	wifi_control_data = wifi_ctrl;

	wifi_set_power(0, 0);	/* Power Off */
	wifi_set_carddetect(0);	/* CardDetect (1->0) */

	up(&wifi_control_sem);
	return 0;
}

#ifdef RTW_SUPPORT_PLATFORM_SHUTDOWN
static void wifi_shutdown(struct platform_device *pdev)
{
	struct wifi_platform_data *wifi_ctrl =
		(struct wifi_platform_data *)(pdev->dev.platform_data);
	

	DBG_871X("## %s\n", __FUNCTION__);

	wifi_control_data = wifi_ctrl;

	shutdown_card();
	wifi_set_power(0, 0);	/* Power Off */
	wifi_set_carddetect(0);	/* CardDetect (1->0) */
}
#endif // RTW_SUPPORT_PLATFORM_SHUTDOWN

static int wifi_suspend(struct platform_device *pdev, pm_message_t state)
{
	DBG_871X("##> %s\n", __FUNCTION__);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 39)) && defined(OOB_INTR_ONLY)
	bcmsdh_oob_intr_set(0);
#endif
	return 0;
}

static int wifi_resume(struct platform_device *pdev)
{
	DBG_871X("##> %s\n", __FUNCTION__);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 39)) && defined(OOB_INTR_ONLY)
	if (dhd_os_check_if_up(bcmsdh_get_drvdata()))
		bcmsdh_oob_intr_set(1);
#endif
	return 0;
}

/* temporarily use these two */
static struct platform_driver wifi_device = {
	.probe          = wifi_probe,
	.remove         = wifi_remove,
	.suspend        = wifi_suspend,
	.resume         = wifi_resume,
#ifdef RTW_SUPPORT_PLATFORM_SHUTDOWN
	.shutdown       = wifi_shutdown,
#endif // RTW_SUPPORT_PLATFORM_SHUTDOWN
	.driver         = {
	.name   = "bcmdhd_wlan",
	}
};

static struct platform_driver wifi_device_legacy = {
	.probe          = wifi_probe,
	.remove         = wifi_remove,
	.suspend        = wifi_suspend,
	.resume         = wifi_resume,
	.driver         = {
	.name   = "bcm4329_wlan",
	}
};

static int wifi_add_dev(void)
{
	DBG_871X("## Calling platform_driver_register\n");
	platform_driver_register(&wifi_device);
	platform_driver_register(&wifi_device_legacy);
	return 0;
}

static void wifi_del_dev(void)
{
	DBG_871X("## Unregister platform_driver_register\n");
	platform_driver_unregister(&wifi_device);
	platform_driver_unregister(&wifi_device_legacy);
}
#endif /* defined(RTW_ENABLE_WIFI_CONTROL_FUNC) */

#ifdef CONFIG_GPIO_WAKEUP
#ifdef CONFIG_PLATFORM_INTEL_BYT
int wifi_configure_gpio(void)
{
	if (gpio_request(oob_gpio, "oob_irq")) {
		DBG_871X("## %s Cannot request GPIO\n", __FUNCTION__);
		return -1;
	}
	gpio_export(oob_gpio, 0);
	if (gpio_direction_input(oob_gpio)) {
		DBG_871X("## %s Cannot set GPIO direction input\n", __FUNCTION__);
		return -1;
	}
	if ((oob_irq = gpio_to_irq(oob_gpio)) < 0) {
		DBG_871X("## %s Cannot convert GPIO to IRQ\n", __FUNCTION__);
		return -1;
	}

	DBG_871X("## %s OOB_IRQ=%d\n", __FUNCTION__, oob_irq);

	return 0;
}
#endif //CONFIG_PLATFORM_INTEL_BYT
void wifi_free_gpio(unsigned int gpio)
{
#ifdef CONFIG_PLATFORM_INTEL_BYT
	if(gpio)
		gpio_free(gpio);
#endif //CONFIG_PLATFORM_INTEL_BYT
}
#endif //CONFIG_GPIO_WAKEUP
