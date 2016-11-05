/*******************************************************************************

  Copyright(c) 2004 Intel Corporation. All rights reserved.

  Portions of this file are based on the WEP enablement code provided by the
  Host AP project hostap-drivers v0.1.3
  Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
  <jkmaline@cc.hut.fi>
  Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>

  This program is free software; you can redistribute it and/or modify it
  under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.

  Contact Information:
  James P. Ketrenos <ipw2100-admin@linux.intel.com>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/in6.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/tcp.h>
#include <linux/types.h>
#include <linux/wireless.h>
#include <linux/etherdevice.h>
#include <linux/uaccess.h>
#include <net/arp.h>
#include "rtllib.h"

u32 rt_global_debug_component = COMP_ERR;
EXPORT_SYMBOL(rt_global_debug_component);

static inline int rtllib_networks_allocate(struct rtllib_device *ieee)
{
	if (ieee->networks)
		return 0;

	ieee->networks = kcalloc(MAX_NETWORK_COUNT,
				 sizeof(struct rtllib_network), GFP_KERNEL);
	if (!ieee->networks)
		return -ENOMEM;

	return 0;
}

static inline void rtllib_networks_free(struct rtllib_device *ieee)
{
	if (!ieee->networks)
		return;
	kfree(ieee->networks);
	ieee->networks = NULL;
}

static inline void rtllib_networks_initialize(struct rtllib_device *ieee)
{
	int i;

	INIT_LIST_HEAD(&ieee->network_free_list);
	INIT_LIST_HEAD(&ieee->network_list);
	for (i = 0; i < MAX_NETWORK_COUNT; i++)
		list_add_tail(&ieee->networks[i].list,
			      &ieee->network_free_list);
}

struct net_device *alloc_rtllib(int sizeof_priv)
{
	struct rtllib_device *ieee = NULL;
	struct net_device *dev;
	int i, err;

	pr_debug("rtllib: Initializing...\n");

	dev = alloc_etherdev(sizeof(struct rtllib_device) + sizeof_priv);
	if (!dev) {
		pr_err("Unable to allocate net_device.\n");
		return NULL;
	}
	ieee = (struct rtllib_device *)netdev_priv_rsl(dev);
	memset(ieee, 0, sizeof(struct rtllib_device)+sizeof_priv);
	ieee->dev = dev;

	err = rtllib_networks_allocate(ieee);
	if (err) {
		pr_err("Unable to allocate beacon storage: %d\n", err);
		goto failed;
	}
	rtllib_networks_initialize(ieee);

	/* Default fragmentation threshold is maximum payload size */
	ieee->fts = DEFAULT_FTS;
	ieee->scan_age = DEFAULT_MAX_SCAN_AGE;
	ieee->open_wep = 1;

	/* Default to enabling full open WEP with host based encrypt/decrypt */
	ieee->host_encrypt = 1;
	ieee->host_decrypt = 1;
	ieee->ieee802_1x = 1; /* Default to supporting 802.1x */

	ieee->rtllib_ap_sec_type = rtllib_ap_sec_type;

	spin_lock_init(&ieee->lock);
	spin_lock_init(&ieee->wpax_suitlist_lock);
	spin_lock_init(&ieee->reorder_spinlock);
	atomic_set(&(ieee->atm_swbw), 0);

	/* SAM FIXME */
	lib80211_crypt_info_init(&ieee->crypt_info, "RTLLIB", &ieee->lock);

	ieee->wpa_enabled = 0;
	ieee->tkip_countermeasures = 0;
	ieee->drop_unencrypted = 0;
	ieee->privacy_invoked = 0;
	ieee->ieee802_1x = 1;
	ieee->raw_tx = 0;
	ieee->hwsec_active = 0;

	memset(ieee->swcamtable, 0, sizeof(struct sw_cam_table) * 32);
	rtllib_softmac_init(ieee);

	ieee->pHTInfo = kzalloc(sizeof(struct rt_hi_throughput), GFP_KERNEL);
	if (!ieee->pHTInfo)
		return NULL;

	HTUpdateDefaultSetting(ieee);
	HTInitializeHTInfo(ieee);
	TSInitialize(ieee);
	for (i = 0; i < IEEE_IBSS_MAC_HASH_SIZE; i++)
		INIT_LIST_HEAD(&ieee->ibss_mac_hash[i]);

	for (i = 0; i < 17; i++) {
		ieee->last_rxseq_num[i] = -1;
		ieee->last_rxfrag_num[i] = -1;
		ieee->last_packet_time[i] = 0;
	}

	return dev;

 failed:
	free_netdev(dev);
	return NULL;
}
EXPORT_SYMBOL(alloc_rtllib);

void free_rtllib(struct net_device *dev)
{
	struct rtllib_device *ieee = (struct rtllib_device *)
				      netdev_priv_rsl(dev);

	kfree(ieee->pHTInfo);
	ieee->pHTInfo = NULL;
	rtllib_softmac_free(ieee);

	lib80211_crypt_info_free(&ieee->crypt_info);

	rtllib_networks_free(ieee);
	free_netdev(dev);
}
EXPORT_SYMBOL(free_rtllib);

static int __init rtllib_init(void)
{
	return 0;
}

static void __exit rtllib_exit(void)
{
}

module_init(rtllib_init);
module_exit(rtllib_exit);

MODULE_LICENSE("GPL");
