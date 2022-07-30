// SPDX-License-Identifier: GPL-2.0
/****************************************************************************
 *   -----------------------------PROCFS STUFF-------------------------
 ****************************************************************************/
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include "r8192U.h"

static struct proc_dir_entry *rtl8192_proc;
static int __maybe_unused proc_get_stats_ap(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);
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

static int __maybe_unused proc_get_registers(struct seq_file *m, void *v)
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

static int __maybe_unused proc_get_stats_tx(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);

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

static int __maybe_unused proc_get_stats_rx(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	struct r8192_priv *priv = (struct r8192_priv *)ieee80211_priv(dev);

	seq_printf(m,
		   "RX packets: %lu\n"
		   "RX urb status error: %lu\n"
		   "RX invalid urb error: %lu\n",
		   priv->stats.rxoktotal,
		   priv->stats.rxstaterr,
		   priv->stats.rxurberr);

	return 0;
}

void rtl8192_proc_module_init(void)
{
	RT_TRACE(COMP_INIT, "Initializing proc filesystem");
	rtl8192_proc = proc_mkdir(RTL819XU_MODULE_NAME, init_net.proc_net);
}

void rtl8192_proc_init_one(struct net_device *dev)
{
	struct proc_dir_entry *dir;

	if (!rtl8192_proc)
		return;

	dir = proc_mkdir_data(dev->name, 0, rtl8192_proc, dev);
	if (!dir)
		return;

	proc_create_single("stats-rx", S_IFREG | 0444, dir,
			   proc_get_stats_rx);
	proc_create_single("stats-tx", S_IFREG | 0444, dir,
			   proc_get_stats_tx);
	proc_create_single("stats-ap", S_IFREG | 0444, dir,
			   proc_get_stats_ap);
	proc_create_single("registers", S_IFREG | 0444, dir,
			   proc_get_registers);
}

void rtl8192_proc_remove_one(struct net_device *dev)
{
	remove_proc_subtree(dev->name, rtl8192_proc);
}
