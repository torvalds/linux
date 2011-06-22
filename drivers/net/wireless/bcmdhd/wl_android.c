/*
 * Linux cfg80211 driver - Android private commands
 *
 * Copyright (C) 1999-2011, Broadcom Corporation
 *
 *         Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: wl_android.c,v 1.1.4.1.2.14 2011/02/09 01:40:07 Exp $
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include "wlioctl.h"
#include "wldev_common.h"

/*
 * Android private command strings, PLEASE define new private commands here
 * so they can be updated easily in the future (if needed)
 */

#define CMD_START		"START"
#define CMD_STOP		"STOP"
#define	CMD_SCAN_ACTIVE		"SCAN-ACTIVE"
#define	CMD_SCAN_PASSIVE	"SCAN-PASSIVE"
#define CMD_RSSI		"RSSI"
#define CMD_LINKSPEED		"LINKSPEED"
#define CMD_RXFILTER_START	"RXFILTER-START"
#define CMD_RXFILTER_STOP	"RXFILTER-STOP"
#define CMD_BTCOEXSCAN_START	"BTCOEXSCAN-START"
#define CMD_BTCOEXSCAN_STOP	"BTCOEXSCAN-STOP"
#define CMD_BTCOEXMODE		"BTCOEXMODE"
#define CMD_SETSUSPENDOPT	"SETSUSPENDOPT"

typedef struct android_wifi_priv_cmd {
	char *buf;
	int used_len;
	int total_len;
} android_wifi_priv_cmd;

int net_os_set_suspend_disable(struct net_device *dev, int val);
int net_os_set_suspend(struct net_device *dev, int val);

static int g_wifi_on = 0;
static int wl_android_get_link_speed(struct net_device *net, char *command, int total_len);
static int wl_android_get_rssi(struct net_device *net, char *command, int total_len);
static int wl_android_set_suspendopt(struct net_device *dev, char *command, int total_len);

int wl_android_priv_cmd(struct net_device *net, struct ifreq *ifr, int cmd)
{
	int ret = 0;
	char *command = NULL;
	int bytes_written = 0;
	android_wifi_priv_cmd *priv_cmd;

	/* net_os_wake_lock(dev); */

	priv_cmd = (android_wifi_priv_cmd*)ifr->ifr_data;
	if (!priv_cmd)
	{
		ret = -EINVAL;
		goto exit;
	}
	command = kmalloc(priv_cmd->total_len, GFP_KERNEL);
	if (!command)
	{
		printk("%s: failed to allocate memory\n", __FUNCTION__);
		ret = -ENOMEM;
		goto exit;
	}
	if (copy_from_user(command, priv_cmd->buf, priv_cmd->total_len)) {
		ret = -EFAULT;
		goto exit;
	}

	printk("%s: Android private command \"%s\" on %s\n", __FUNCTION__, command, ifr->ifr_name);

	if (strnicmp(command, CMD_START, strlen(CMD_START)) == 0) {
		/* TBD: START */
		printk("%s, Received regular START command\n", __FUNCTION__);
		g_wifi_on = 1;
	}
	if (!g_wifi_on) {
		/*
		printk("%s START command has to be called first\n", __FUNCTION__);
		ret = -EFAULT;
		goto exit;
		*/
	}
	if (strnicmp(command, CMD_STOP, strlen(CMD_STOP)) == 0) {
		/* TBD: STOP */
	}
	else if (strnicmp(command, CMD_SCAN_ACTIVE, strlen(CMD_SCAN_ACTIVE)) == 0) {
		/* TBD: SCAN-ACTIVE */
	}
	else if (strnicmp(command, CMD_SCAN_PASSIVE, strlen(CMD_SCAN_PASSIVE)) == 0) {
		/* TBD: SCAN-PASSIVE */
	}
	else if (strnicmp(command, CMD_RSSI, strlen(CMD_RSSI)) == 0) {
		bytes_written = wl_android_get_rssi(net, command, priv_cmd->total_len);
	}
	else if (strnicmp(command, CMD_LINKSPEED, strlen(CMD_LINKSPEED)) == 0) {
		bytes_written = wl_android_get_link_speed(net, command, priv_cmd->total_len);
	}
	else if (strnicmp(command, CMD_RXFILTER_START, strlen(CMD_RXFILTER_START)) == 0) {
		/* TBD: RXFILTER-START */
	}
	else if (strnicmp(command, CMD_RXFILTER_STOP, strlen(CMD_RXFILTER_STOP)) == 0) {
		/* TBD: RXFILTER-STOP */
	}
	else if (strnicmp(command, CMD_BTCOEXSCAN_START, strlen(CMD_BTCOEXSCAN_START)) == 0) {
		/* TBD: BTCOEXSCAN-START */
	}
	else if (strnicmp(command, CMD_BTCOEXSCAN_STOP, strlen(CMD_BTCOEXSCAN_STOP)) == 0) {
		/* TBD: BTCOEXSCAN-STOP */
	}
	else if (strnicmp(command, CMD_BTCOEXMODE, strlen(CMD_BTCOEXMODE)) == 0) {
		/* TBD: BTCOEXMODE */
	}
	else if (strnicmp(command, CMD_SETSUSPENDOPT, strlen(CMD_SETSUSPENDOPT)) == 0) {
		bytes_written = wl_android_set_suspendopt(net, command, priv_cmd->total_len);
	}
	else {
		printk("Unknown PRIVATE command %s - ignored\n", command);
		snprintf(command, 3, "OK");
		bytes_written = strlen("OK") + 1;
	}

	if (bytes_written > 0) {
		priv_cmd->used_len = bytes_written;
		if (copy_to_user(priv_cmd->buf, command, bytes_written)) {
			printk("%s: failed to copy data to user buffer\n", __FUNCTION__);
		}
	} else {
		ret = bytes_written;
	}

exit:
	/* net_os_wake_unlock(dev); */
	if (command) {
		kfree(command);
	}

	return ret;
}

static int wl_android_get_link_speed(struct net_device *net, char *command, int total_len)
{
	int link_speed;
	int bytes_written;
	int error;

	error = wldev_get_link_speed(net, &link_speed);
	if (error)
		return -1;

	/* Convert Kbps to Android Mbps */
	link_speed = link_speed / 1000;
	bytes_written = snprintf(command, total_len, "LinkSpeed %d", link_speed);
	printk("%s: command result is %s \n", __FUNCTION__, command);
	return bytes_written;
}

static int wl_android_get_rssi(struct net_device *net, char *command, int total_len)
{
	wlc_ssid_t ssid;
	int rssi;
	int bytes_written;
	int error;

	error = wldev_get_rssi(net, &rssi);
	if (error)
		return -1;

	error = wldev_get_ssid(net, &ssid);
	if (error)
		return -1;
	memcpy(command, ssid.SSID, ssid.SSID_len);
	bytes_written = ssid.SSID_len;
	bytes_written += snprintf(&command[bytes_written], total_len, " rssi %d", rssi);
	printk("%s: command result is %s \n", __FUNCTION__, command);
	return bytes_written;
}

static int wl_android_set_suspendopt(struct net_device *dev, char *command, int total_len)
{
	int suspend_flag;
	int ret_now;
	int ret = 0;

	suspend_flag = *(command + strlen(CMD_SETSUSPENDOPT) + 1) - '0';

	if (suspend_flag != 0)
		suspend_flag = 1;
	ret_now = net_os_set_suspend_disable(dev, suspend_flag);

	if (ret_now != suspend_flag) {
		if (!(ret = net_os_set_suspend(dev, ret_now)))
			printk("%s: Suspend Flag %d -> %d\n",
				__FUNCTION__, ret_now, suspend_flag);
		else
			printk("%s: failed %d\n", __FUNCTION__, ret);
	}
	return ret;
}
