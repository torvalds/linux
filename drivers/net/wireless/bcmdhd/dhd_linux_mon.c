/*
 * Broadcom Dongle Host Driver (DHD), Linux monitor network interface
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
 * $Id: dhd_linux_mon.c,v 1.131.2.55 2011-02-09 05:31:56 Exp $
 */

#include <linux/string.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/ieee80211.h>
#include <linux/rtnetlink.h>
#include <net/ieee80211_radiotap.h>

#include <wlioctl.h>
#include <bcmutils.h>
#include <linux_osl.h>
#include <dhd_dbg.h>
#include <dngl_stats.h>
#include <dhd.h>

typedef enum monitor_states
{
	MONITOR_STATE_DEINIT = 0x0,
	MONITOR_STATE_INIT = 0x1,
	MONITOR_STATE_INTERFACE_ADDED = 0x2,
	MONITOR_STATE_INTERFACE_DELETED = 0x4
} monitor_states_t;
extern int dhd_start_xmit(struct sk_buff *skb, struct net_device *net);

/**
 * Local declarations and defintions (not exposed)
 */
#define MON_PRINT(format, ...) printk("DHD-MON: %s " format, __func__, ##__VA_ARGS__)
#define MON_TRACE MON_PRINT

typedef struct monitor_interface {
	int radiotap_enabled;
	struct net_device* real_ndev;	/* The real interface that the monitor is on */
	struct net_device* mon_ndev;
} monitor_interface;

typedef struct dhd_linux_monitor {
	void *dhd_pub;
	monitor_states_t monitor_state;
	monitor_interface mon_if[DHD_MAX_IFS];
	struct mutex lock;		/* lock to protect mon_if */
} dhd_linux_monitor_t;

static dhd_linux_monitor_t g_monitor;

static struct net_device* lookup_real_netdev(char *name);
static monitor_interface* ndev_to_monif(struct net_device *ndev);
static int dhd_mon_if_open(struct net_device *ndev);
static int dhd_mon_if_stop(struct net_device *ndev);
static int dhd_mon_if_subif_start_xmit(struct sk_buff *skb, struct net_device *ndev);
static void dhd_mon_if_set_multicast_list(struct net_device *ndev);
static int dhd_mon_if_change_mac(struct net_device *ndev, void *addr);

static const struct net_device_ops dhd_mon_if_ops = {
	.ndo_open		= dhd_mon_if_open,
	.ndo_stop		= dhd_mon_if_stop,
	.ndo_start_xmit		= dhd_mon_if_subif_start_xmit,
	.ndo_set_multicast_list = dhd_mon_if_set_multicast_list,
	.ndo_set_mac_address 	= dhd_mon_if_change_mac,
};

/**
 * Local static function defintions
 */

/* Look up dhd's net device table to find a match (e.g. interface "eth0" is a match for "mon.eth0"
 * "p2p-eth0-0" is a match for "mon.p2p-eth0-0")
 */
static struct net_device* lookup_real_netdev(char *name)
{
	int i;
	int last_name_len = 0;
	struct net_device *ndev;
	struct net_device *ndev_found = NULL;

	/* We want to find interface "p2p-eth0-0" for monitor interface "mon.p2p-eth0-0", so
	 * we skip "eth0" even if "mon.p2p-eth0-0" contains "eth0"
	 */
	for (i = 0; i < DHD_MAX_IFS; i++) {
		ndev = dhd_idx2net(g_monitor.dhd_pub, i);
		if (ndev && strstr(name, ndev->name)) {
			if (strlen(ndev->name) > last_name_len) {
				ndev_found = ndev;
				last_name_len = strlen(ndev->name);
			}
		}
	}

	return ndev_found;
}

static monitor_interface* ndev_to_monif(struct net_device *ndev)
{
	int i;

	for (i = 0; i < DHD_MAX_IFS; i++) {
		if (g_monitor.mon_if[i].mon_ndev == ndev)
			return &g_monitor.mon_if[i];
	}

	return NULL;
}

static int dhd_mon_if_open(struct net_device *ndev)
{
	int ret = 0;

	MON_PRINT("enter\n");
	return ret;
}

static int dhd_mon_if_stop(struct net_device *ndev)
{
	int ret = 0;

	MON_PRINT("enter\n");
	return ret;
}

static int dhd_mon_if_subif_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	int ret = 0;
	int rtap_len;
	int qos_len = 0;
	int dot11_hdr_len = 24;
	int snap_len = 6;
	unsigned char *pdata;
	unsigned short frame_ctl;
	unsigned char src_mac_addr[6];
	unsigned char dst_mac_addr[6];
	struct ieee80211_hdr *dot11_hdr;
	struct ieee80211_radiotap_header *rtap_hdr;
	monitor_interface* mon_if;

	MON_PRINT("enter\n");

	mon_if = ndev_to_monif(ndev);
	if (mon_if == NULL || mon_if->real_ndev == NULL) {
		MON_PRINT(" cannot find matched net dev, skip the packet\n");
		goto fail;
	}

	if (unlikely(skb->len < sizeof(struct ieee80211_radiotap_header)))
		goto fail;

	rtap_hdr = (struct ieee80211_radiotap_header *)skb->data;
	if (unlikely(rtap_hdr->it_version))
		goto fail;

	rtap_len = ieee80211_get_radiotap_len(skb->data);
	if (unlikely(skb->len < rtap_len))
		goto fail;

	MON_PRINT("radiotap len (should be 14): %d\n", rtap_len);

	/* Skip the ratio tap header */
	skb_pull(skb, rtap_len);

	dot11_hdr = (struct ieee80211_hdr *)skb->data;
	frame_ctl = le16_to_cpu(dot11_hdr->frame_control);
	/* Check if the QoS bit is set */
	if ((frame_ctl & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA) {
		/* Check if this ia a Wireless Distribution System (WDS) frame
		 * which has 4 MAC addresses
		 */
		if (dot11_hdr->frame_control & 0x0080)
			qos_len = 2;
		if ((dot11_hdr->frame_control & 0x0300) == 0x0300)
			dot11_hdr_len += 6;

		memcpy(dst_mac_addr, dot11_hdr->addr1, sizeof(dst_mac_addr));
		memcpy(src_mac_addr, dot11_hdr->addr2, sizeof(src_mac_addr));

		/* Skip the 802.11 header, QoS (if any) and SNAP, but leave spaces for
		 * for two MAC addresses
		 */
		skb_pull(skb, dot11_hdr_len + qos_len + snap_len - sizeof(src_mac_addr) * 2);
		pdata = (unsigned char*)skb->data;
		memcpy(pdata, dst_mac_addr, sizeof(dst_mac_addr));
		memcpy(pdata + sizeof(dst_mac_addr), src_mac_addr, sizeof(src_mac_addr));

		MON_PRINT("if name: %s, matched if name %s\n", ndev->name, mon_if->real_ndev->name);

		/* Use the real net device to transmit the packet */
		ret = dhd_start_xmit(skb, mon_if->real_ndev);

		return ret;
	}
fail:
	dev_kfree_skb(skb);
	return 0;
}

static void dhd_mon_if_set_multicast_list(struct net_device *ndev)
{
	monitor_interface* mon_if;

	mon_if = ndev_to_monif(ndev);
	if (mon_if == NULL || mon_if->real_ndev == NULL) {
		MON_PRINT(" cannot find matched net dev, skip the packet\n");
	} else {
		MON_PRINT("enter, if name: %s, matched if name %s\n",
		ndev->name, mon_if->real_ndev->name);
	}
}

static int dhd_mon_if_change_mac(struct net_device *ndev, void *addr)
{
	int ret = 0;
	monitor_interface* mon_if;

	mon_if = ndev_to_monif(ndev);
	if (mon_if == NULL || mon_if->real_ndev == NULL) {
		MON_PRINT(" cannot find matched net dev, skip the packet\n");
	} else {
		MON_PRINT("enter, if name: %s, matched if name %s\n",
		ndev->name, mon_if->real_ndev->name);
	}
	return ret;
}

/**
 * Global function definitions (declared in dhd_linux_mon.h)
 */

int dhd_add_monitor(char *name, struct net_device **new_ndev)
{
	int i;
	int idx = -1;
	int ret = 0;
	struct net_device* ndev = NULL;
	dhd_linux_monitor_t **dhd_mon;

	mutex_lock(&g_monitor.lock);

	MON_TRACE("enter, if name: %s\n", name);
	if (!name || !new_ndev) {
		MON_PRINT("invalid parameters\n");
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Find a vacancy
	 */
	for (i = 0; i < DHD_MAX_IFS; i++)
		if (g_monitor.mon_if[i].mon_ndev == NULL) {
			idx = i;
			break;
		}
	if (idx == -1) {
		MON_PRINT("exceeds maximum interfaces\n");
		ret = -EFAULT;
		goto out;
	}

	ndev = alloc_etherdev(sizeof(dhd_linux_monitor_t*));
	if (!ndev) {
		MON_PRINT("failed to allocate memory\n");
		ret = -ENOMEM;
		goto out;
	}

	ndev->type = ARPHRD_IEEE80211_RADIOTAP;
	strncpy(ndev->name, name, IFNAMSIZ);
	ndev->name[IFNAMSIZ - 1] = 0;
	ndev->netdev_ops = &dhd_mon_if_ops;

	ret = register_netdevice(ndev);
	if (ret) {
		MON_PRINT(" register_netdevice failed (%d)\n", ret);
		goto out;
	}

	*new_ndev = ndev;
	g_monitor.mon_if[idx].radiotap_enabled = TRUE;
	g_monitor.mon_if[idx].mon_ndev = ndev;
	g_monitor.mon_if[idx].real_ndev = lookup_real_netdev(name);
	dhd_mon = (dhd_linux_monitor_t **)netdev_priv(ndev);
	*dhd_mon = &g_monitor;
	g_monitor.monitor_state = MONITOR_STATE_INTERFACE_ADDED;
	MON_PRINT("net device returned: 0x%p\n", ndev);
	MON_PRINT("found a matched net device, name %s\n", g_monitor.mon_if[idx].real_ndev->name);

out:
	if (ret && ndev)
		free_netdev(ndev);

	mutex_unlock(&g_monitor.lock);
	return ret;

}

int dhd_del_monitor(struct net_device *ndev)
{
	int i;
	bool rollback_lock = false;
	if (!ndev)
		return -EINVAL;
	mutex_lock(&g_monitor.lock);
	for (i = 0; i < DHD_MAX_IFS; i++) {
		if (g_monitor.mon_if[i].mon_ndev == ndev ||
			g_monitor.mon_if[i].real_ndev == ndev) {
			g_monitor.mon_if[i].real_ndev = NULL;
			if (rtnl_is_locked()) {
				rtnl_unlock();
				rollback_lock = true;
			}
			unregister_netdev(g_monitor.mon_if[i].mon_ndev);
			free_netdev(g_monitor.mon_if[i].mon_ndev);
			g_monitor.mon_if[i].mon_ndev = NULL;
			g_monitor.monitor_state = MONITOR_STATE_INTERFACE_DELETED;
			break;
		}
	}
	if (rollback_lock) {
		rtnl_lock();
		rollback_lock = false;
	}

	if (g_monitor.monitor_state !=
	MONITOR_STATE_INTERFACE_DELETED)
		MON_PRINT("interface not found in monitor IF array, is this a monitor IF? 0x%p\n",
			ndev);
	mutex_unlock(&g_monitor.lock);

	return 0;
}

int dhd_monitor_init(void *dhd_pub)
{
	if (g_monitor.monitor_state == MONITOR_STATE_DEINIT) {
		g_monitor.dhd_pub = dhd_pub;
		mutex_init(&g_monitor.lock);
		g_monitor.monitor_state = MONITOR_STATE_INIT;
	}
	return 0;
}

int dhd_monitor_uninit(void)
{
	int i;
	struct net_device *ndev;
	bool rollback_lock = false;
	mutex_lock(&g_monitor.lock);
	if (g_monitor.monitor_state != MONITOR_STATE_DEINIT) {
		for (i = 0; i < DHD_MAX_IFS; i++) {
			ndev = g_monitor.mon_if[i].mon_ndev;
			if (ndev) {
				if (rtnl_is_locked()) {
					rtnl_unlock();
					rollback_lock = true;
				}
				unregister_netdev(ndev);
				free_netdev(ndev);
				g_monitor.mon_if[i].real_ndev = NULL;
				g_monitor.mon_if[i].mon_ndev = NULL;
				if (rollback_lock) {
					rtnl_lock();
					rollback_lock = false;
				}
			}
		}
		g_monitor.monitor_state = MONITOR_STATE_DEINIT;
	}
	mutex_unlock(&g_monitor.lock);
	return 0;
}
