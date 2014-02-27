/*******************************************************************************

  Copyright(c) 2004-2005 Intel Corporation. All rights reserved.

  Portions of this file are based on the WEP enablement code provided by the
  Host AP project hostap-drivers v0.1.3
  Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
  <j@w1.fi>
  Copyright (c) 2002-2003, Jouni Malinen <j@w1.fi>

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
  Intel Linux Wireless <ilw@linux.intel.com>
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
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/tcp.h>
#include <linux/types.h>
#include <linux/wireless.h>
#include <linux/etherdevice.h>
#include <asm/uaccess.h>
#include <net/net_namespace.h>
#include <net/arp.h>

#include "libipw.h"

#define DRV_DESCRIPTION "802.11 data/management/control stack"
#define DRV_NAME        "libipw"
#define DRV_PROCNAME	"ieee80211"
#define DRV_VERSION	LIBIPW_VERSION
#define DRV_COPYRIGHT   "Copyright (C) 2004-2005 Intel Corporation <jketreno@linux.intel.com>"

MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR(DRV_COPYRIGHT);
MODULE_LICENSE("GPL");

static struct cfg80211_ops libipw_config_ops = { };
static void *libipw_wiphy_privid = &libipw_wiphy_privid;

static int libipw_networks_allocate(struct libipw_device *ieee)
{
	int i, j;

	for (i = 0; i < MAX_NETWORK_COUNT; i++) {
		ieee->networks[i] = kzalloc(sizeof(struct libipw_network),
					    GFP_KERNEL);
		if (!ieee->networks[i]) {
			LIBIPW_ERROR("Out of memory allocating beacons\n");
			for (j = 0; j < i; j++)
				kfree(ieee->networks[j]);
			return -ENOMEM;
		}
	}

	return 0;
}

void libipw_network_reset(struct libipw_network *network)
{
	if (!network)
		return;

	if (network->ibss_dfs) {
		kfree(network->ibss_dfs);
		network->ibss_dfs = NULL;
	}
}

static inline void libipw_networks_free(struct libipw_device *ieee)
{
	int i;

	for (i = 0; i < MAX_NETWORK_COUNT; i++) {
		if (ieee->networks[i]->ibss_dfs)
			kfree(ieee->networks[i]->ibss_dfs);
		kfree(ieee->networks[i]);
	}
}

void libipw_networks_age(struct libipw_device *ieee,
                            unsigned long age_secs)
{
	struct libipw_network *network = NULL;
	unsigned long flags;
	unsigned long age_jiffies = msecs_to_jiffies(age_secs * MSEC_PER_SEC);

	spin_lock_irqsave(&ieee->lock, flags);
	list_for_each_entry(network, &ieee->network_list, list) {
		network->last_scanned -= age_jiffies;
	}
	spin_unlock_irqrestore(&ieee->lock, flags);
}
EXPORT_SYMBOL(libipw_networks_age);

static void libipw_networks_initialize(struct libipw_device *ieee)
{
	int i;

	INIT_LIST_HEAD(&ieee->network_free_list);
	INIT_LIST_HEAD(&ieee->network_list);
	for (i = 0; i < MAX_NETWORK_COUNT; i++)
		list_add_tail(&ieee->networks[i]->list,
			      &ieee->network_free_list);
}

int libipw_change_mtu(struct net_device *dev, int new_mtu)
{
	if ((new_mtu < 68) || (new_mtu > LIBIPW_DATA_LEN))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}
EXPORT_SYMBOL(libipw_change_mtu);

struct net_device *alloc_libipw(int sizeof_priv, int monitor)
{
	struct libipw_device *ieee;
	struct net_device *dev;
	int err;

	LIBIPW_DEBUG_INFO("Initializing...\n");

	dev = alloc_etherdev(sizeof(struct libipw_device) + sizeof_priv);
	if (!dev)
		goto failed;

	ieee = netdev_priv(dev);

	ieee->dev = dev;

	if (!monitor) {
		ieee->wdev.wiphy = wiphy_new(&libipw_config_ops, 0);
		if (!ieee->wdev.wiphy) {
			LIBIPW_ERROR("Unable to allocate wiphy.\n");
			goto failed_free_netdev;
		}

		ieee->dev->ieee80211_ptr = &ieee->wdev;
		ieee->wdev.iftype = NL80211_IFTYPE_STATION;

		/* Fill-out wiphy structure bits we know...  Not enough info
		   here to call set_wiphy_dev or set MAC address or channel info
		   -- have to do that in ->ndo_init... */
		ieee->wdev.wiphy->privid = libipw_wiphy_privid;

		ieee->wdev.wiphy->max_scan_ssids = 1;
		ieee->wdev.wiphy->max_scan_ie_len = 0;
		ieee->wdev.wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION)
						| BIT(NL80211_IFTYPE_ADHOC);
	}

	err = libipw_networks_allocate(ieee);
	if (err) {
		LIBIPW_ERROR("Unable to allocate beacon storage: %d\n", err);
		goto failed_free_wiphy;
	}
	libipw_networks_initialize(ieee);

	/* Default fragmentation threshold is maximum payload size */
	ieee->fts = DEFAULT_FTS;
	ieee->rts = DEFAULT_FTS;
	ieee->scan_age = DEFAULT_MAX_SCAN_AGE;
	ieee->open_wep = 1;

	/* Default to enabling full open WEP with host based encrypt/decrypt */
	ieee->host_encrypt = 1;
	ieee->host_decrypt = 1;
	ieee->host_mc_decrypt = 1;

	/* Host fragmentation in Open mode. Default is enabled.
	 * Note: host fragmentation is always enabled if host encryption
	 * is enabled. For cards can do hardware encryption, they must do
	 * hardware fragmentation as well. So we don't need a variable
	 * like host_enc_frag. */
	ieee->host_open_frag = 1;
	ieee->ieee802_1x = 1;	/* Default to supporting 802.1x */

	spin_lock_init(&ieee->lock);

	lib80211_crypt_info_init(&ieee->crypt_info, dev->name, &ieee->lock);

	ieee->wpa_enabled = 0;
	ieee->drop_unencrypted = 0;
	ieee->privacy_invoked = 0;

	return dev;

failed_free_wiphy:
	if (!monitor)
		wiphy_free(ieee->wdev.wiphy);
failed_free_netdev:
	free_netdev(dev);
failed:
	return NULL;
}
EXPORT_SYMBOL(alloc_libipw);

void free_libipw(struct net_device *dev, int monitor)
{
	struct libipw_device *ieee = netdev_priv(dev);

	lib80211_crypt_info_free(&ieee->crypt_info);

	libipw_networks_free(ieee);

	/* free cfg80211 resources */
	if (!monitor)
		wiphy_free(ieee->wdev.wiphy);

	free_netdev(dev);
}
EXPORT_SYMBOL(free_libipw);

#ifdef CONFIG_LIBIPW_DEBUG

static int debug = 0;
u32 libipw_debug_level = 0;
EXPORT_SYMBOL_GPL(libipw_debug_level);
static struct proc_dir_entry *libipw_proc = NULL;

static int debug_level_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "0x%08X\n", libipw_debug_level);
	return 0;
}

static int debug_level_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_level_proc_show, NULL);
}

static ssize_t debug_level_proc_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *pos)
{
	char buf[] = "0x00000000\n";
	size_t len = min(sizeof(buf) - 1, count);
	unsigned long val;

	if (copy_from_user(buf, buffer, len))
		return count;
	buf[len] = 0;
	if (sscanf(buf, "%li", &val) != 1)
		printk(KERN_INFO DRV_NAME
		       ": %s is not in hex or decimal form.\n", buf);
	else
		libipw_debug_level = val;

	return strnlen(buf, len);
}

static const struct file_operations debug_level_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= debug_level_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= debug_level_proc_write,
};
#endif				/* CONFIG_LIBIPW_DEBUG */

static int __init libipw_init(void)
{
#ifdef CONFIG_LIBIPW_DEBUG
	struct proc_dir_entry *e;

	libipw_debug_level = debug;
	libipw_proc = proc_mkdir(DRV_PROCNAME, init_net.proc_net);
	if (libipw_proc == NULL) {
		LIBIPW_ERROR("Unable to create " DRV_PROCNAME
				" proc directory\n");
		return -EIO;
	}
	e = proc_create("debug_level", S_IRUGO | S_IWUSR, libipw_proc,
			&debug_level_proc_fops);
	if (!e) {
		remove_proc_entry(DRV_PROCNAME, init_net.proc_net);
		libipw_proc = NULL;
		return -EIO;
	}
#endif				/* CONFIG_LIBIPW_DEBUG */

	printk(KERN_INFO DRV_NAME ": " DRV_DESCRIPTION ", " DRV_VERSION "\n");
	printk(KERN_INFO DRV_NAME ": " DRV_COPYRIGHT "\n");

	return 0;
}

static void __exit libipw_exit(void)
{
#ifdef CONFIG_LIBIPW_DEBUG
	if (libipw_proc) {
		remove_proc_entry("debug_level", libipw_proc);
		remove_proc_entry(DRV_PROCNAME, init_net.proc_net);
		libipw_proc = NULL;
	}
#endif				/* CONFIG_LIBIPW_DEBUG */
}

#ifdef CONFIG_LIBIPW_DEBUG
#include <linux/moduleparam.h>
module_param(debug, int, 0444);
MODULE_PARM_DESC(debug, "debug output mask");
#endif				/* CONFIG_LIBIPW_DEBUG */

module_exit(libipw_exit);
module_init(libipw_init);
