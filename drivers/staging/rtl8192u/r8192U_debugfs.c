// SPDX-License-Identifier: GPL-2.0
/****************************************************************************
 *   -----------------------------DEGUGFS STUFF-------------------------
 ****************************************************************************/
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include "r8192U.h"

#define KBUILD_MODNAME "r8192u_usb"

static int rtl8192_usb_stats_ap_show(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct r8192_priv *priv = ieee80211_priv(dev);
	struct ieee80211_device *ieee = priv->ieee80211;
	struct ieee80211_network *target;

	list_for_each_entry(target, &ieee->network_list, list) {
		const char *wpa = "non_WPA";

		if (target->wpa_ie_len > 0 || target->rsn_ie_len > 0)
			wpa = "WPA";

		seq_printf(m, "%s %s\n", target->ssid, wpa);
	}

	return 0;
}

static int rtl8192_usb_registers_show(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	int i, n, max = 0xff;
	u8 byte_rd;

	seq_puts(m, "\n####################page 0##################\n ");

	for (n = 0; n <= max;) {
		seq_printf(m, "\nD:  %2x > ", n);

		for (i = 0; i < 16 && n <= max; i++, n++) {
			read_nic_byte(dev, 0x000 | n, &byte_rd);
			seq_printf(m, "%2x ", byte_rd);
		}
	}

	seq_puts(m, "\n####################page 1##################\n ");
	for (n = 0; n <= max;) {
		seq_printf(m, "\nD:  %2x > ", n);

		for (i = 0; i < 16 && n <= max; i++, n++) {
			read_nic_byte(dev, 0x100 | n, &byte_rd);
			seq_printf(m, "%2x ", byte_rd);
		}
	}

	seq_puts(m, "\n####################page 3##################\n ");
	for (n = 0; n <= max;) {
		seq_printf(m, "\nD:  %2x > ", n);

		for (i = 0; i < 16 && n <= max; i++, n++) {
			read_nic_byte(dev, 0x300 | n, &byte_rd);
			seq_printf(m, "%2x ", byte_rd);
		}
	}

	seq_putc(m, '\n');
	return 0;
}

static int rtl8192_usb_stats_tx_show(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct r8192_priv *priv = ieee80211_priv(dev);

	seq_printf(m,
		   "TX VI priority ok int: %lu\n"
		   "TX VI priority error int: %lu\n"
		   "TX VO priority ok int: %lu\n"
		   "TX VO priority error int: %lu\n"
		   "TX BE priority ok int: %lu\n"
		   "TX BE priority error int: %lu\n"
		   "TX BK priority ok int: %lu\n"
		   "TX BK priority error int: %lu\n"
		   "TX MANAGE priority ok int: %lu\n"
		   "TX MANAGE priority error int: %lu\n"
		   "TX BEACON priority ok int: %lu\n"
		   "TX BEACON priority error int: %lu\n"
		   "TX queue resume: %lu\n"
		   "TX queue stopped?: %d\n"
		   "TX fifo overflow: %lu\n"
		   "TX VI queue: %d\n"
		   "TX VO queue: %d\n"
		   "TX BE queue: %d\n"
		   "TX BK queue: %d\n"
		   "TX VI dropped: %lu\n"
		   "TX VO dropped: %lu\n"
		   "TX BE dropped: %lu\n"
		   "TX BK dropped: %lu\n"
		   "TX total data packets %lu\n",
		   priv->stats.txviokint,
		   priv->stats.txvierr,
		   priv->stats.txvookint,
		   priv->stats.txvoerr,
		   priv->stats.txbeokint,
		   priv->stats.txbeerr,
		   priv->stats.txbkokint,
		   priv->stats.txbkerr,
		   priv->stats.txmanageokint,
		   priv->stats.txmanageerr,
		   priv->stats.txbeaconokint,
		   priv->stats.txbeaconerr,
		   priv->stats.txresumed,
		   netif_queue_stopped(dev),
		   priv->stats.txoverflow,
		   atomic_read(&(priv->tx_pending[VI_PRIORITY])),
		   atomic_read(&(priv->tx_pending[VO_PRIORITY])),
		   atomic_read(&(priv->tx_pending[BE_PRIORITY])),
		   atomic_read(&(priv->tx_pending[BK_PRIORITY])),
		   priv->stats.txvidrop,
		   priv->stats.txvodrop,
		   priv->stats.txbedrop,
		   priv->stats.txbkdrop,
		   priv->stats.txdatapkt
		);

	return 0;
}

static int rtl8192_usb_stats_rx_show(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct r8192_priv *priv = ieee80211_priv(dev);

	seq_printf(m,
		   "RX packets: %lu\n"
		   "RX urb status error: %lu\n"
		   "RX invalid urb error: %lu\n",
		   priv->stats.rxoktotal,
		   priv->stats.rxstaterr,
		   priv->stats.rxurberr);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(rtl8192_usb_stats_rx);
DEFINE_SHOW_ATTRIBUTE(rtl8192_usb_stats_tx);
DEFINE_SHOW_ATTRIBUTE(rtl8192_usb_stats_ap);
DEFINE_SHOW_ATTRIBUTE(rtl8192_usb_registers);

void rtl8192_debugfs_init_one(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	struct dentry *parent_dir = debugfs_lookup(KBUILD_MODNAME, NULL);
	struct dentry *dir = debugfs_create_dir(dev->name, parent_dir);

	debugfs_create_file("stats-rx", 0444, dir, dev, &rtl8192_usb_stats_rx_fops);
	debugfs_create_file("stats-tx", 0444, dir, dev, &rtl8192_usb_stats_tx_fops);
	debugfs_create_file("stats-ap", 0444, dir, dev, &rtl8192_usb_stats_ap_fops);
	debugfs_create_file("registers", 0444, dir, dev, &rtl8192_usb_registers_fops);

	priv->debugfs_dir = dir;
}

void rtl8192_debugfs_exit_one(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	debugfs_remove_recursive(priv->debugfs_dir);
}

void rtl8192_debugfs_rename_one(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	struct dentry *parent_dir = debugfs_lookup(KBUILD_MODNAME, NULL);

	debugfs_rename(parent_dir, priv->debugfs_dir, parent_dir, dev->name);
}

void rtl8192_debugfs_init(void)
{
	debugfs_create_dir(KBUILD_MODNAME, NULL);
}

void rtl8192_debugfs_exit(void)
{
	debugfs_remove_recursive(debugfs_lookup(KBUILD_MODNAME, NULL));
}
