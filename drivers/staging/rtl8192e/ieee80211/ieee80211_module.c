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

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.

  Contact Information:
  James P. Ketrenos <ipw2100-admin@linux.intel.com>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#include <linux/compiler.h>
//#include <linux/config.h>
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
#include <linux/version.h>
#include <linux/wireless.h>
#include <linux/etherdevice.h>
#include <asm/uaccess.h>
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

	ieee->networks = kmalloc(
		MAX_NETWORK_COUNT * sizeof(struct ieee80211_network),
		GFP_KERNEL);
	if (!ieee->networks) {
		printk(KERN_WARNING "%s: Out of memory allocating beacons\n",
		       ieee->dev->name);
		return -ENOMEM;
	}

	memset(ieee->networks, 0,
	       MAX_NETWORK_COUNT * sizeof(struct ieee80211_network));

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
	int i,err;

	IEEE80211_DEBUG_INFO("Initializing...\n");

	dev = alloc_etherdev(sizeof(struct ieee80211_device) + sizeof_priv);
	if (!dev) {
		IEEE80211_ERROR("Unable to network device.\n");
		goto failed;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0))
	ieee = netdev_priv(dev);
#else
	ieee = (struct ieee80211_device *)dev->priv;
#endif
#if 0
	dev->hard_start_xmit = ieee80211_rtl_xmit;
#endif

	memset(ieee, 0, sizeof(struct ieee80211_device)+sizeof_priv);
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
	init_timer(&ieee->crypt_deinit_timer);
	ieee->crypt_deinit_timer.data = (unsigned long)ieee;
	ieee->crypt_deinit_timer.function = ieee80211_crypt_deinit_handler;

	spin_lock_init(&ieee->lock);
	spin_lock_init(&ieee->wpax_suitlist_lock);
	spin_lock_init(&ieee->bw_spinlock);
	spin_lock_init(&ieee->reorder_spinlock);
	//added by WB
	atomic_set(&(ieee->atm_chnlop), 0);
	atomic_set(&(ieee->atm_swbw), 0);

	ieee->wpax_type_set = 0;
 	ieee->wpa_enabled = 0;
 	ieee->tkip_countermeasures = 0;
 	ieee->drop_unencrypted = 0;
 	ieee->privacy_invoked = 0;
 	ieee->ieee802_1x = 1;
	ieee->raw_tx = 0;
	//ieee->hwsec_support = 1; //default support hw security. //use module_param instead.
	ieee->hwsec_active = 0; //disable hwsec, switch it on when necessary.

	ieee80211_softmac_init(ieee);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,13))
	ieee->pHTInfo = (RT_HIGH_THROUGHPUT*)kzalloc(sizeof(RT_HIGH_THROUGHPUT), GFP_KERNEL);
#else
	ieee->pHTInfo = (RT_HIGH_THROUGHPUT*)kmalloc(sizeof(RT_HIGH_THROUGHPUT), GFP_KERNEL);
	memset(ieee->pHTInfo,0,sizeof(RT_HIGH_THROUGHPUT));
#endif
	if (ieee->pHTInfo == NULL)
	{
		IEEE80211_DEBUG(IEEE80211_DL_ERR, "can't alloc memory for HTInfo\n");
		return NULL;
	}
	HTUpdateDefaultSetting(ieee);
	HTInitializeHTInfo(ieee); //may move to other place.
	TSInitialize(ieee);
#if 0
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
 	INIT_WORK(&ieee->ht_onAssRsp, (void(*)(void*)) HTOnAssocRsp_wq);
#else
	INIT_WORK(&ieee->ht_onAssRsp, (void(*)(void*)) HTOnAssocRsp_wq, ieee);
#endif
#endif
	for (i = 0; i < IEEE_IBSS_MAC_HASH_SIZE; i++)
		INIT_LIST_HEAD(&ieee->ibss_mac_hash[i]);

	for (i = 0; i < 17; i++) {
	  ieee->last_rxseq_num[i] = -1;
	  ieee->last_rxfrag_num[i] = -1;
	  ieee->last_packet_time[i] = 0;
	}

//These function were added to load crypte module autoly
	ieee80211_tkip_null();
	ieee80211_wep_null();
	ieee80211_ccmp_null();

	return dev;

 failed:
	if (dev)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0))
		free_netdev(dev);
#else
		kfree(dev);
#endif
	return NULL;
}


void free_ieee80211(struct net_device *dev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0))
	struct ieee80211_device *ieee = netdev_priv(dev);
#else
	struct ieee80211_device *ieee = (struct ieee80211_device *)dev->priv;
#endif
	int i;
	//struct list_head *p, *q;
//	del_timer_sync(&ieee->SwBwTimer);
#if 1
	if (ieee->pHTInfo != NULL)
	{
		kfree(ieee->pHTInfo);
		ieee->pHTInfo = NULL;
	}
#endif
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
#if 0
	for (i = 0; i < IEEE_IBSS_MAC_HASH_SIZE; i++) {
		list_for_each_safe(p, q, &ieee->ibss_mac_hash[i]) {
			kfree(list_entry(p, struct ieee_ibss_seq, list));
			list_del(p);
		}
	}

#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0))
	free_netdev(dev);
#else
	kfree(dev);
#endif
}

#ifdef CONFIG_IEEE80211_DEBUG

u32 ieee80211_debug_level = 0;
static int debug = \
	//		    IEEE80211_DL_INFO	|
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
	//		    IEEE80211_DL_HT 	|
	//		    IEEE80211_DL_TS	|
//			    IEEE80211_DL_BA 	|
	//		    IEEE80211_DL_REORDER|
//			    IEEE80211_DL_TRACE  |
			    //IEEE80211_DL_DATA	|
			    IEEE80211_DL_ERR	  //awayls open this flags to show error out
			    ;
struct proc_dir_entry *ieee80211_proc = NULL;

static int show_debug_level(char *page, char **start, off_t offset,
			    int count, int *eof, void *data)
{
	return snprintf(page, count, "0x%08X\n", ieee80211_debug_level);
}

static int store_debug_level(struct file *file, const char *buffer,
			     unsigned long count, void *data)
{
	char buf[] = "0x00000000";
	unsigned long len = min(sizeof(buf) - 1, (u32)count);
	char *p = (char *)buf;
	unsigned long val;

	if (copy_from_user(buf, buffer, len))
		return count;
	buf[len] = 0;
	if (p[1] == 'x' || p[1] == 'X' || p[0] == 'x' || p[0] == 'X') {
		p++;
		if (p[0] == 'x' || p[0] == 'X')
			p++;
		val = simple_strtoul(p, &p, 16);
	} else
		val = simple_strtoul(p, &p, 10);
	if (p == buf)
		printk(KERN_INFO DRV_NAME
		       ": %s is not in hex or decimal form.\n", buf);
	else
		ieee80211_debug_level = val;

	return strnlen(buf, count);
}

extern int ieee80211_crypto_init(void);
extern void ieee80211_crypto_deinit(void);
extern int ieee80211_crypto_tkip_init(void);
extern void ieee80211_crypto_tkip_exit(void);
extern int ieee80211_crypto_ccmp_init(void);
extern void ieee80211_crypto_ccmp_exit(void);
extern int ieee80211_crypto_wep_init(void);
extern void ieee80211_crypto_wep_exit(void);

int __init ieee80211_rtl_init(void)
{
	struct proc_dir_entry *e;
	int retval;

	retval = ieee80211_crypto_init();
	if (retval)
		return retval;
	retval = ieee80211_crypto_tkip_init();
	if (retval) {
		ieee80211_crypto_deinit();
		return retval;
	}
	retval = ieee80211_crypto_ccmp_init();
	if (retval) {
		ieee80211_crypto_tkip_exit();
		ieee80211_crypto_deinit();
		return retval;
	}
	retval = ieee80211_crypto_wep_init();
	if (retval) {
		ieee80211_crypto_ccmp_exit();
		ieee80211_crypto_tkip_exit();
		ieee80211_crypto_deinit();
		return retval;
	}

	ieee80211_debug_level = debug;
#if(LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
	ieee80211_proc = create_proc_entry(DRV_NAME, S_IFDIR, proc_net);
#else
	ieee80211_proc = create_proc_entry(DRV_NAME, S_IFDIR, init_net.proc_net);
#endif
	if (ieee80211_proc == NULL) {
		IEEE80211_ERROR("Unable to create " DRV_NAME
				" proc directory\n");
		return -EIO;
	}
	e = create_proc_entry("debug_level", S_IFREG | S_IRUGO | S_IWUSR,
			      ieee80211_proc);
	if (!e) {
#if(LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
		remove_proc_entry(DRV_NAME, proc_net);
#else
		remove_proc_entry(DRV_NAME, init_net.proc_net);
#endif
		ieee80211_proc = NULL;
		return -EIO;
	}
	e->read_proc = show_debug_level;
	e->write_proc = store_debug_level;
	e->data = NULL;

	return 0;
}

void __exit ieee80211_rtl_exit(void)
{
	if (ieee80211_proc) {
		remove_proc_entry("debug_level", ieee80211_proc);
#if(LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
		remove_proc_entry(DRV_NAME, proc_net);
#else
		remove_proc_entry(DRV_NAME, init_net.proc_net);
#endif
		ieee80211_proc = NULL;
	}
	ieee80211_crypto_wep_exit();
	ieee80211_crypto_ccmp_exit();
	ieee80211_crypto_tkip_exit();
	ieee80211_crypto_deinit();
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
#include <linux/moduleparam.h>
module_param(debug, int, 0444);
MODULE_PARM_DESC(debug, "debug output mask");


//module_exit(ieee80211_rtl_exit);
//module_init(ieee80211_rtl_init);
#endif
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
//EXPORT_SYMBOL(alloc_ieee80211);
//EXPORT_SYMBOL(free_ieee80211);
#else
EXPORT_SYMBOL_NOVERS(alloc_ieee80211);
EXPORT_SYMBOL_NOVERS(free_ieee80211);
#endif
