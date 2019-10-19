// SPDX-License-Identifier: GPL-2.0
/*******************************************************************************
 *
 *  Copyright(c) 2004 Intel Corporation. All rights reserved.
 *
 *  Portions of this file are based on the WEP enablement code provided by the
 *  Host AP project hostap-drivers v0.1.3
 *  Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
 *  <jkmaline@cc.hut.fi>
 *  Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of version 2 of the GNU General Public License as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *  The full GNU General Public License is included in this distribution in the
 *  file called LICENSE.
 *
 *  Contact Information:
 *  James P. Ketrenos <ipw2100-admin@linux.intel.com>
 *  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

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

#include "ieee80211.h"

MODULE_DESCRIPTION("802.11 data/management/control stack");
MODULE_AUTHOR("Copyright (C) 2004 Intel Corporation <jketreno@linux.intel.com>");
MODULE_LICENSE("GPL");

#define DRV_NAME "ieee80211"

static inline int ieee80211_networks_allocate(struct ieee80211_device *ieee)
{
	if (ieee->networks)
		return 0;

	ieee->networks = kcalloc(MAX_NETWORK_COUNT,
				 sizeof(struct ieee80211_network),
				 GFP_KERNEL);
	if (!ieee->networks) {
		printk(KERN_WARNING "%s: Out of memory allocating beacons\n",
		       ieee->dev->name);
		return -ENOMEM;
	}

	return 0;
}

static inline void ieee80211_networks_free(struct ieee80211_device *ieee)
{
	if (!ieee->networks)
		return;
	kfree(ieee->networks);
	ieee->networks = NULL;
}

static inline void ieee80211_networks_initialize(struct ieee80211_device *ieee)
{
	int i;

	INIT_LIST_HEAD(&ieee->network_free_list);
	INIT_LIST_HEAD(&ieee->network_list);
	for (i = 0; i < MAX_NETWORK_COUNT; i++)
		list_add_tail(&ieee->networks[i].list, &ieee->network_free_list);
}

struct net_device *alloc_ieee80211(int sizeof_priv)
{
	struct ieee80211_device *ieee;
	struct net_device *dev;
	int i, err;

	IEEE80211_DEBUG_INFO("Initializing...\n");

	dev = alloc_etherdev(sizeof(struct ieee80211_device) + sizeof_priv);
	if (!dev) {
		IEEE80211_ERROR("Unable to network device.\n");
		goto failed;
	}

	ieee = netdev_priv(dev);
	ieee->dev = dev;

	err = ieee80211_networks_allocate(ieee);
	if (err) {
		IEEE80211_ERROR("Unable to allocate beacon storage: %d\n",
				err);
		goto failed;
	}
	ieee80211_networks_initialize(ieee);

	/* Default fragmentation threshold is maximum payload size */
	ieee->fts = DEFAULT_FTS;
	ieee->scan_age = DEFAULT_MAX_SCAN_AGE;
	ieee->open_wep = 1;

	/* Default to enabling full open WEP with host based encrypt/decrypt */
	ieee->host_encrypt = 1;
	ieee->host_decrypt = 1;
	ieee->ieee802_1x = 1; /* Default to supporting 802.1x */

	INIT_LIST_HEAD(&ieee->crypt_deinit_list);
	timer_setup(&ieee->crypt_deinit_timer, ieee80211_crypt_deinit_handler,
		    0);

	spin_lock_init(&ieee->lock);
	spin_lock_init(&ieee->wpax_suitlist_lock);
	spin_lock_init(&ieee->bw_spinlock);
	spin_lock_init(&ieee->reorder_spinlock);
	/* added by WB */
	atomic_set(&ieee->atm_chnlop, 0);
	atomic_set(&ieee->atm_swbw, 0);

	ieee->wpax_type_set = 0;
	ieee->wpa_enabled = 0;
	ieee->tkip_countermeasures = 0;
	ieee->drop_unencrypted = 0;
	ieee->privacy_invoked = 0;
	ieee->ieee802_1x = 1;
	ieee->raw_tx = 0;
	//ieee->hwsec_support = 1; //defalt support hw security. //use module_param instead.
	ieee->hwsec_active = 0; /* disable hwsec, switch it on when necessary. */

	ieee80211_softmac_init(ieee);

	ieee->pHTInfo = kzalloc(sizeof(RT_HIGH_THROUGHPUT), GFP_KERNEL);
	if (!ieee->pHTInfo) {
		IEEE80211_DEBUG(IEEE80211_DL_ERR, "can't alloc memory for HTInfo\n");

		/* By this point in code ieee80211_networks_allocate() has been
		 * successfully called so the memory allocated should be freed
		 */
		ieee80211_networks_free(ieee);
		goto failed;
	}
	HTUpdateDefaultSetting(ieee);
	HTInitializeHTInfo(ieee); /* may move to other place. */
	TSInitialize(ieee);

	for (i = 0; i < IEEE_IBSS_MAC_HASH_SIZE; i++)
		INIT_LIST_HEAD(&ieee->ibss_mac_hash[i]);

	for (i = 0; i < 17; i++) {
		ieee->last_rxseq_num[i] = -1;
		ieee->last_rxfrag_num[i] = -1;
		ieee->last_packet_time[i] = 0;
	}

/* These function were added to load crypte module autoly */
	ieee80211_tkip_null();

	return dev;

 failed:
	if (dev)
		free_netdev(dev);

	return NULL;
}

void free_ieee80211(struct net_device *dev)
{
	struct ieee80211_device *ieee = netdev_priv(dev);
	int i;
	/* struct list_head *p, *q; */
//	del_timer_sync(&ieee->SwBwTimer);
	kfree(ieee->pHTInfo);
	ieee->pHTInfo = NULL;
	RemoveAllTS(ieee);
	ieee80211_softmac_free(ieee);
	del_timer_sync(&ieee->crypt_deinit_timer);
	ieee80211_crypt_deinit_entries(ieee, 1);

	for (i = 0; i < WEP_KEYS; i++) {
		struct ieee80211_crypt_data *crypt = ieee->crypt[i];

		if (crypt) {
			if (crypt->ops)
				crypt->ops->deinit(crypt->priv);
			kfree(crypt);
			ieee->crypt[i] = NULL;
		}
	}

	ieee80211_networks_free(ieee);
	free_netdev(dev);
}

#ifdef CONFIG_IEEE80211_DEBUG

u32 ieee80211_debug_level;
static int debug = //	    IEEE80211_DL_INFO	|
	//		    IEEE80211_DL_WX	|
	//		    IEEE80211_DL_SCAN	|
	//		    IEEE80211_DL_STATE	|
	//		    IEEE80211_DL_MGMT	|
	//		    IEEE80211_DL_FRAG	|
	//		    IEEE80211_DL_EAP	|
	//		    IEEE80211_DL_DROP	|
	//		    IEEE80211_DL_TX	|
	//		    IEEE80211_DL_RX	|
			    //IEEE80211_DL_QOS    |
	//		    IEEE80211_DL_HT	|
	//		    IEEE80211_DL_TS	|
//			    IEEE80211_DL_BA	|
	//		    IEEE80211_DL_REORDER|
//			    IEEE80211_DL_TRACE  |
			    //IEEE80211_DL_DATA	|
			    IEEE80211_DL_ERR	  /* awayls open this flags to show error out */
			    ;
static struct proc_dir_entry *ieee80211_proc;

static int show_debug_level(struct seq_file *m, void *v)
{
	seq_printf(m, "0x%08X\n", ieee80211_debug_level);

	return 0;
}

static ssize_t write_debug_level(struct file *file, const char __user *buffer,
				 size_t count, loff_t *ppos)
{
	unsigned long val;
	int err = kstrtoul_from_user(buffer, count, 0, &val);

	if (err)
		return err;
	ieee80211_debug_level = val;
	return count;
}

static int open_debug_level(struct inode *inode, struct file *file)
{
	return single_open(file, show_debug_level, NULL);
}

static const struct file_operations fops = {
	.open = open_debug_level,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = write_debug_level,
	.release = single_release,
};

int __init ieee80211_debug_init(void)
{
	struct proc_dir_entry *e;

	ieee80211_debug_level = debug;

	ieee80211_proc = proc_mkdir(DRV_NAME, init_net.proc_net);
	if (!ieee80211_proc) {
		IEEE80211_ERROR("Unable to create " DRV_NAME
				" proc directory\n");
		return -EIO;
	}
	e = proc_create("debug_level", 0644, ieee80211_proc, &fops);
	if (!e) {
		remove_proc_entry(DRV_NAME, init_net.proc_net);
		ieee80211_proc = NULL;
		return -EIO;
	}
	return 0;
}

void __exit ieee80211_debug_exit(void)
{
	if (ieee80211_proc) {
		remove_proc_entry("debug_level", ieee80211_proc);
		remove_proc_entry(DRV_NAME, init_net.proc_net);
		ieee80211_proc = NULL;
	}
}

module_param(debug, int, 0444);
MODULE_PARM_DESC(debug, "debug output mask");
#endif
