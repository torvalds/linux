/*
 * Intel Wireless Multicomm 3200 WiFi driver
 *
 * Copyright (C) 2009 Intel Corporation <ilw@linux.intel.com>
 * Samuel Ortiz <samuel.ortiz@intel.com>
 * Zhu Yi <yi.zhu@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

/*
 * This is the netdev related hooks for iwm.
 *
 * Some interesting code paths:
 *
 * iwm_open() (Called at netdev interface bringup time)
 *  -> iwm_up() (main.c)
 *      -> iwm_bus_enable()
 *          -> if_sdio_enable() (In case of an SDIO bus)
 *              -> sdio_enable_func()
 *      -> iwm_notif_wait(BARKER_REBOOT) (wait for reboot barker)
 *      -> iwm_notif_wait(ACK_BARKER) (wait for ACK barker)
 *      -> iwm_load_fw() (fw.c)
 *          -> iwm_load_umac()
 *          -> iwm_load_lmac() (Calibration LMAC)
 *          -> iwm_load_lmac() (Operational LMAC)
 *      -> iwm_send_umac_config()
 *
 * iwm_stop() (Called at netdev interface bringdown time)
 *  -> iwm_down()
 *      -> iwm_bus_disable()
 *          -> if_sdio_disable() (In case of an SDIO bus)
 *              -> sdio_disable_func()
 */
#include <linux/netdevice.h>

#include "iwm.h"
#include "commands.h"
#include "cfg80211.h"
#include "debug.h"

static int iwm_open(struct net_device *ndev)
{
	struct iwm_priv *iwm = ndev_to_iwm(ndev);

	return iwm_up(iwm);
}

static int iwm_stop(struct net_device *ndev)
{
	struct iwm_priv *iwm = ndev_to_iwm(ndev);

	return iwm_down(iwm);
}

/*
 * iwm AC to queue mapping
 *
 * AC_VO -> queue 3
 * AC_VI -> queue 2
 * AC_BE -> queue 1
 * AC_BK -> queue 0
 */
static const u16 iwm_1d_to_queue[8] = { 1, 0, 0, 1, 2, 2, 3, 3 };

int iwm_tid_to_queue(u16 tid)
{
	if (tid > IWM_UMAC_TID_NR - 2)
		return -EINVAL;

	return iwm_1d_to_queue[tid];
}

static u16 iwm_select_queue(struct net_device *dev, struct sk_buff *skb)
{
	skb->priority = cfg80211_classify8021d(skb);

	return iwm_1d_to_queue[skb->priority];
}

static const struct net_device_ops iwm_netdev_ops = {
	.ndo_open		= iwm_open,
	.ndo_stop		= iwm_stop,
	.ndo_start_xmit		= iwm_xmit_frame,
	.ndo_select_queue	= iwm_select_queue,
};

void *iwm_if_alloc(int sizeof_bus, struct device *dev,
		   struct iwm_if_ops *if_ops)
{
	struct net_device *ndev;
	struct wireless_dev *wdev;
	struct iwm_priv *iwm;
	int ret = 0;

	wdev = iwm_wdev_alloc(sizeof_bus, dev);
	if (IS_ERR(wdev))
		return wdev;

	iwm = wdev_to_iwm(wdev);
	iwm->bus_ops = if_ops;
	iwm->wdev = wdev;

	ret = iwm_priv_init(iwm);
	if (ret) {
		dev_err(dev, "failed to init iwm_priv\n");
		goto out_wdev;
	}

	wdev->iftype = iwm_mode_to_nl80211_iftype(iwm->conf.mode);

	ndev = alloc_netdev_mq(0, "wlan%d", ether_setup, IWM_TX_QUEUES);
	if (!ndev) {
		dev_err(dev, "no memory for network device instance\n");
		goto out_priv;
	}

	ndev->netdev_ops = &iwm_netdev_ops;
	ndev->ieee80211_ptr = wdev;
	SET_NETDEV_DEV(ndev, wiphy_dev(wdev->wiphy));
	wdev->netdev = ndev;

	iwm->umac_profile = kmalloc(sizeof(struct iwm_umac_profile),
				    GFP_KERNEL);
	if (!iwm->umac_profile) {
		dev_err(dev, "Couldn't alloc memory for profile\n");
		goto out_profile;
	}

	iwm_init_default_profile(iwm, iwm->umac_profile);

	return iwm;

 out_profile:
	free_netdev(ndev);

 out_priv:
	iwm_priv_deinit(iwm);

 out_wdev:
	iwm_wdev_free(iwm);
	return ERR_PTR(ret);
}

void iwm_if_free(struct iwm_priv *iwm)
{
	if (!iwm_to_ndev(iwm))
		return;

	cancel_delayed_work_sync(&iwm->ct_kill_delay);
	free_netdev(iwm_to_ndev(iwm));
	iwm_priv_deinit(iwm);
	kfree(iwm->umac_profile);
	iwm->umac_profile = NULL;
	iwm_wdev_free(iwm);
}

int iwm_if_add(struct iwm_priv *iwm)
{
	struct net_device *ndev = iwm_to_ndev(iwm);
	int ret;

	ret = register_netdev(ndev);
	if (ret < 0) {
		dev_err(&ndev->dev, "Failed to register netdev: %d\n", ret);
		return ret;
	}

	return 0;
}

void iwm_if_remove(struct iwm_priv *iwm)
{
	unregister_netdev(iwm_to_ndev(iwm));
}
