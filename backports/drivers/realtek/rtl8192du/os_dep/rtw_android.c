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
 *
 ******************************************************************************/

#include <linux/module.h>
#include <linux/netdevice.h>

#include <rtw_android.h>
#include <osdep_service.h>
#include <rtw_debug.h>
#include <ioctl_cfg80211.h>
#include <rtw_ioctl_set.h>

#if defined(RTW_ENABLE_WIFI_CONTROL_FUNC)
#include <linux/platform_device.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
#include <linux/wlan_plat.h>
#else
#include <linux/wifi_tiwlan.h>
#endif
#endif /* defined(RTW_ENABLE_WIFI_CONTROL_FUNC) */

static const char *android_wifi_cmd_str[ANDROID_WIFI_CMD_MAX] = {
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
#ifdef PNO_SUPPORT
	"PNOSSIDCLR",
	"PNOSETUP ",
	"PNOFORCE",
	"PNODEBUG",
#endif

	"MACADDR",

	"BLOCK",
	"WFD-ENABLE",
	"WFD-DISABLE",
	"WFD-SET-TCPPORT",
	"WFD-SET-MAXTPUT",
	"WFD-SET-DEVTYPE",
};

#ifdef PNO_SUPPORT
#define PNO_TLV_PREFIX			'S'
#define PNO_TLV_VERSION			'1'
#define PNO_TLV_SUBVERSION		'2'
#define PNO_TLV_RESERVED		'0'
#define PNO_TLV_TYPE_SSID_IE		'S'
#define PNO_TLV_TYPE_TIME		'T'
#define PNO_TLV_FREQ_REPEAT		'R'
#define PNO_TLV_FREQ_EXPO_MAX		'M'

struct cmd_tlv {
	char prefix;
	char version;
	char subver;
	char reserved;
};
#endif /* PNO_SUPPORT */

struct android_wifi_priv_cmd {
	const char __user *buf;
	int used_len;
	int total_len;
};

/**
 * Local (static) functions and variables
 */

/* Initialize g_wifi_on to 1 so dhd_bus_start will be called for the first
 * time (only) in dhd_open, subsequential wifi on will be handled by
 * wl_android_wifi_on
 */
static int g_wifi_on = true;

#ifdef PNO_SUPPORT
static int wl_android_set_pno_setup(struct net_device *dev, char *command, int total_len)
{
	wlc_ssid_t ssids_local[MAX_PFN_LIST_COUNT];
	int res = -1;
	int nssid = 0;
	struct cmd_tlv *struct cmd_tlvemp;
	char *str_ptr;
	int tlv_size_left;
	int pno_time = 0;
	int pno_repeat = 0;
	int pno_freq_expo_max = 0;

#ifdef PNO_SET_DEBUG
	int i;
	char pno_in_example[] = {
		'P', 'N', 'O', 'S', 'E', 'T', 'U', 'P', ' ',
		'S', '1', '2', '0',
		'S',
		0x05,
		'd', 'l', 'i', 'n', 'k',
		'S',
		0x04,
		'G', 'O', 'O', 'G',
		'T',
		'0', 'B',
		'R',
		'2',
		'M',
		'2',
		0x00
		};
#endif /* PNO_SET_DEBUG */

	DHD_INFO(("%s: command=%s, len=%d\n", __func__, command, total_len));

	if (total_len < (strlen(CMD_PNOSETUP_SET) + sizeof(struct cmd_tlv))) {
		DBG_8192D("%s argument=%d less min size\n", __func__, total_len);
		goto exit_proc;
	}

#ifdef PNO_SET_DEBUG
	memcpy(command, pno_in_example, sizeof(pno_in_example));
	for (i = 0; i < sizeof(pno_in_example); i++)
		printf("%02X ", command[i]);
	printf("\n");
	total_len = sizeof(pno_in_example);
#endif

	str_ptr = command + strlen(CMD_PNOSETUP_SET);
	tlv_size_left = total_len - strlen(CMD_PNOSETUP_SET);

	struct cmd_tlvemp = (struct cmd_tlv *)str_ptr;
	memset(ssids_local, 0, sizeof(ssids_local));

	if ((struct cmd_tlvemp->prefix == PNO_TLV_PREFIX) &&
		(struct cmd_tlvemp->version == PNO_TLV_VERSION) &&
		(struct cmd_tlvemp->subver == PNO_TLV_SUBVERSION)) {

		str_ptr += sizeof(struct cmd_tlv);
		tlv_size_left -= sizeof(struct cmd_tlv);

		if ((nssid = wl_iw_parse_ssid_list_tlv(&str_ptr, ssids_local,
			MAX_PFN_LIST_COUNT, &tlv_size_left)) <= 0) {
			DBG_8192D("SSID is not presented or corrupted ret=%d\n", nssid);
			goto exit_proc;
		} else {
			if ((str_ptr[0] != PNO_TLV_TYPE_TIME) || (tlv_size_left <= 1)) {
				DBG_8192D("%s scan duration corrupted field size %d\n",
					__func__, tlv_size_left);
				goto exit_proc;
			}
			str_ptr++;
			pno_time = simple_strtoul(str_ptr, &str_ptr, 16);
			DHD_INFO(("%s: pno_time=%d\n", __func__, pno_time));

			if (str_ptr[0] != 0) {
				if ((str_ptr[0] != PNO_TLV_FREQ_REPEAT)) {
					DBG_8192D("%s pno repeat : corrupted field\n",
						__func__);
					goto exit_proc;
				}
				str_ptr++;
				pno_repeat = simple_strtoul(str_ptr, &str_ptr, 16);
				DHD_INFO(("%s :got pno_repeat=%d\n", __func__, pno_repeat));
				if (str_ptr[0] != PNO_TLV_FREQ_EXPO_MAX) {
					DBG_8192D("%s FREQ_EXPO_MAX corrupted field size\n",
						__func__);
					goto exit_proc;
				}
				str_ptr++;
				pno_freq_expo_max = simple_strtoul(str_ptr, &str_ptr, 16);
				DHD_INFO(("%s: pno_freq_expo_max=%d\n",
					__func__, pno_freq_expo_max));
			}
		}
	} else {
		DBG_8192D("%s get wrong TLV command\n", __func__);
		goto exit_proc;
	}

	res = dhd_dev_pno_set(dev, ssids_local, nssid, pno_time, pno_repeat, pno_freq_expo_max);

exit_proc:
	return res;
}
#endif /* PNO_SUPPORT */

int rtw_android_cmdstr_to_num(char *cmdstr)
{
	int cmd_num;
	for (cmd_num=0 ; cmd_num<ANDROID_WIFI_CMD_MAX; cmd_num++)
		if (0 == strnicmp(cmdstr , android_wifi_cmd_str[cmd_num], strlen(android_wifi_cmd_str[cmd_num])))
			break;

	return cmd_num;
}

static int rtw_android_get_rssi(struct net_device *net, char *command, int total_len)
{
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(net);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct	wlan_network	*pcur_network = &pmlmepriv->cur_network;
	int bytes_written = 0;

	if (check_fwstate(pmlmepriv, _FW_LINKED) == true) {
		bytes_written += snprintf(&command[bytes_written], total_len, "%s rssi %d",
			pcur_network->network.Ssid.Ssid, padapter->recvpriv.rssi);
	}

	return bytes_written;
}

static int rtw_android_get_link_speed(struct net_device *net, char *command, int total_len)
{
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(net);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct	wlan_network	*pcur_network = &pmlmepriv->cur_network;
	int bytes_written = 0;
	u16 link_speed = 0;

	link_speed = rtw_get_cur_max_rate(padapter)/10;
	bytes_written = snprintf(command, total_len, "LinkSpeed %d", link_speed);

	return bytes_written;
}

static int rtw_android_get_macaddr(struct net_device *net, char *command, int total_len)
{
	struct rtw_adapter *adapter = (struct rtw_adapter *)rtw_netdev_priv(net);
	int bytes_written = 0;

	bytes_written = snprintf(command, total_len, "Macaddr = %pM", net->dev_addr);
	return bytes_written;
}

static int rtw_android_set_country(struct net_device *net, char *command, int total_len)
{
	struct rtw_adapter *adapter = (struct rtw_adapter *)rtw_netdev_priv(net);
	char *country_code = command + strlen(android_wifi_cmd_str[ANDROID_WIFI_CMD_COUNTRY]) + 1;
	int ret;

	ret = rtw_set_country(adapter, country_code);

	return (ret==_SUCCESS)?0:-1;
}

static int rtw_android_get_p2p_dev_addr(struct net_device *net, char *command, int total_len)
{
	int ret;
	int bytes_written = 0;

	/* We use the same address as our HW MAC address */
	memcpy(command, net->dev_addr, ETH_ALEN);

	bytes_written = ETH_ALEN;
	return bytes_written;
}

static int rtw_android_set_block(struct net_device *net, char *command, int total_len)
{
	int ret;
	struct rtw_adapter *adapter = (struct rtw_adapter *)rtw_netdev_priv(net);
	char *block_value = command + strlen(android_wifi_cmd_str[ANDROID_WIFI_CMD_BLOCK]) + 1;

	wdev_to_priv(adapter->rtw_wdev)->block = (*block_value=='0')?false:true;

	return 0;
}

static int get_int_from_command(char* pcmd)
{
	int i = 0;

	for (i = 0; i < strlen(pcmd); i++)
	{
		if (pcmd[i] == '=')
		{
			/*	Skip the '=' and space characters. */
			i += 2;
			break;
		}
	}
	return (rtw_atoi(pcmd + i));
}

int rtw_android_priv_cmd(struct net_device *net, struct ifreq *ifr, int cmd)
{
	int ret = 0;
	char *command = NULL;
	int cmd_num;
	int bytes_written = 0;
	struct android_wifi_priv_cmd priv_cmd;

	rtw_lock_suspend();

	if (!ifr->ifr_data) {
		ret = -EINVAL;
		goto exit;
	}
	if (copy_from_user(&priv_cmd, ifr->ifr_data, sizeof(struct android_wifi_priv_cmd))) {
		ret = -EFAULT;
		goto exit;
	}

	command = kzalloc(priv_cmd.total_len, GFP_KERNEL);
	if (!command)
	{
		DBG_8192D("%s: failed to allocate memory\n", __func__);
		ret = -ENOMEM;
		goto exit;
	}

	if (!access_ok(VERIFY_READ, priv_cmd.buf, priv_cmd.total_len)) {
		DBG_8192D("%s: failed to access memory\n", __func__);
		ret = -EFAULT;
		goto exit;
	 }
	if (copy_from_user(command, (char __user *)priv_cmd.buf, priv_cmd.total_len)) {
		ret = -EFAULT;
		goto exit;
	}

	DBG_8192D("%s: Android private cmd \"%s\" on %s\n"
		, __func__, command, ifr->ifr_name);

	cmd_num = rtw_android_cmdstr_to_num(command);

	switch (cmd_num) {
	case ANDROID_WIFI_CMD_START:
		goto response;
	case ANDROID_WIFI_CMD_SETFWPATH:
		goto response;
	}

	if (!g_wifi_on) {
		DBG_8192D("%s: Ignore private cmd \"%s\" - iface %s is down\n"
			,__func__, command, ifr->ifr_name);
		ret = 0;
		goto exit;
	}

	switch (cmd_num) {
	case ANDROID_WIFI_CMD_STOP:
		break;
	case ANDROID_WIFI_CMD_SCAN_ACTIVE:
		break;
	case ANDROID_WIFI_CMD_SCAN_PASSIVE:
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
	case ANDROID_WIFI_CMD_BLOCK:
		bytes_written = rtw_android_set_block(net, command, priv_cmd.total_len);
		break;
	case ANDROID_WIFI_CMD_RXFILTER_START:
		break;
	case ANDROID_WIFI_CMD_RXFILTER_STOP:
		break;
	case ANDROID_WIFI_CMD_RXFILTER_ADD:
		break;
	case ANDROID_WIFI_CMD_RXFILTER_REMOVE:
		break;
	case ANDROID_WIFI_CMD_BTCOEXSCAN_START:
		break;
	case ANDROID_WIFI_CMD_BTCOEXSCAN_STOP:
		break;
	case ANDROID_WIFI_CMD_BTCOEXMODE:
		break;
	case ANDROID_WIFI_CMD_SETSUSPENDOPT:
		break;
	case ANDROID_WIFI_CMD_SETBAND:
	{
		uint band = *(command + strlen("SETBAND") + 1) - '0';
		struct rtw_adapter*	padapter = (struct rtw_adapter *) rtw_netdev_priv(net);

		if (padapter->chip_type == RTL8192D)
			padapter->setband = band;

		break;
	}
	case ANDROID_WIFI_CMD_GETBAND:
		break;
	case ANDROID_WIFI_CMD_COUNTRY:
		bytes_written = rtw_android_set_country(net, command, priv_cmd.total_len);
		break;
#ifdef PNO_SUPPORT
	case ANDROID_WIFI_CMD_PNOSSIDCLR_SET:
		break;
	case ANDROID_WIFI_CMD_PNOSETUP_SET:
		break;
	case ANDROID_WIFI_CMD_PNOENABLE_SET:
		break;
#endif
	case ANDROID_WIFI_CMD_P2P_DEV_ADDR:
		bytes_written = rtw_android_get_p2p_dev_addr(net, command, priv_cmd.total_len);
		break;
	case ANDROID_WIFI_CMD_P2P_SET_NOA:
		break;
	case ANDROID_WIFI_CMD_P2P_GET_NOA:
		break;
	case ANDROID_WIFI_CMD_P2P_SET_PS:
		break;
	case ANDROID_WIFI_CMD_SET_AP_WPS_P2P_IE:
	{
		int skip = strlen(android_wifi_cmd_str[ANDROID_WIFI_CMD_SET_AP_WPS_P2P_IE]) + 3;
		bytes_written = rtw_cfg80211_set_mgnt_wpsp2pie(net, command + skip, priv_cmd.total_len - skip, *(command + skip - 2) - '0');
		break;
	}

	default:
		DBG_8192D("Unknown PRIVATE command %s - ignored\n", command);
		snprintf(command, 3, "OK");
		bytes_written = strlen("OK");
	}

response:
	if (bytes_written >= 0) {
		if ((bytes_written == 0) && (priv_cmd.total_len > 0))
			command[0] = '\0';
		if (bytes_written >= priv_cmd.total_len) {
			DBG_8192D("%s: bytes_written = %d\n", __func__, bytes_written);
			bytes_written = priv_cmd.total_len;
		} else {
			bytes_written++;
		}
		priv_cmd.used_len = bytes_written;
		if (copy_to_user((char __user *)priv_cmd.buf, command, bytes_written)) {
			DBG_8192D("%s: failed to copy data to user buffer\n", __func__);
			ret = -EFAULT;
		}
	}
	else {
		ret = bytes_written;
	}

exit:
	rtw_unlock_suspend();
	if (command) {
		kfree(command);
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
		DBG_8192D("%s: platform_driver_register failed\n", __func__);
		return ret;
	}
	g_wifidev_registered = 1;

	/* Waiting callback after platform_driver_register is done or exit with error */
	if (down_timeout(&wifi_control_sem,  msecs_to_jiffies(1000)) != 0) {
		ret = -EINVAL;
		DBG_8192D("%s: platform_driver_register timeout\n", __func__);
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
			DBG_8192D("success alloc section %d\n", section);
			if (size != 0L)
				memset(alloc_ptr, 0, size);
			return alloc_ptr;
		}
	}

	DBG_8192D("can't alloc section %d\n", section);
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
	DBG_8192D("%s = %d\n", __func__, on);
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
	DBG_8192D("%s\n", __func__);
	if (!buf)
		return -EINVAL;
	if (wifi_control_data && wifi_control_data->get_mac_addr) {
		return wifi_control_data->get_mac_addr(buf);
	}
	return -EOPNOTSUPP;
}
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)) */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39))
void *wifi_get_country_code(char *ccode)
{
	DBG_8192D("%s\n", __func__);
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
	DBG_8192D("%s = %d\n", __func__, on);
	if (wifi_control_data && wifi_control_data->set_carddetect) {
		wifi_control_data->set_carddetect(on);
	}
	return 0;
}

static int wifi_probe(struct platform_device *pdev)
{
	struct wifi_platform_data *wifi_ctrl =
		(struct wifi_platform_data *)(pdev->dev.platform_data);

	DBG_8192D("## %s\n", __func__);
	wifi_irqres = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "bcmdhd_wlan_irq");
	if (wifi_irqres == NULL)
		wifi_irqres = platform_get_resource_byname(pdev,
			IORESOURCE_IRQ, "bcm4329_wlan_irq");
	wifi_control_data = wifi_ctrl;

	wifi_set_power(1, 0);	/* Power On */
	wifi_set_carddetect(1);	/* CardDetect (0->1) */

	up(&wifi_control_sem);
	return 0;
}

static int wifi_remove(struct platform_device *pdev)
{
	struct wifi_platform_data *wifi_ctrl =
		(struct wifi_platform_data *)(pdev->dev.platform_data);

	DBG_8192D("## %s\n", __func__);
	wifi_control_data = wifi_ctrl;

	wifi_set_power(0, 0);	/* Power Off */
	wifi_set_carddetect(0);	/* CardDetect (1->0) */

	up(&wifi_control_sem);
	return 0;
}

static int wifi_suspend(struct platform_device *pdev, pm_message_t state)
{
	DBG_8192D("##> %s\n", __func__);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 39)) && defined(OOB_INTR_ONLY)
	bcmsdh_oob_intr_set(0);
#endif
	return 0;
}

static int wifi_resume(struct platform_device *pdev)
{
	DBG_8192D("##> %s\n", __func__);
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
	DBG_8192D("## Calling platform_driver_register\n");
	platform_driver_register(&wifi_device);
	platform_driver_register(&wifi_device_legacy);
	return 0;
}

static void wifi_del_dev(void)
{
	DBG_8192D("## Unregister platform_driver_register\n");
	platform_driver_unregister(&wifi_device);
	platform_driver_unregister(&wifi_device_legacy);
}
#endif /* defined(RTW_ENABLE_WIFI_CONTROL_FUNC) */
