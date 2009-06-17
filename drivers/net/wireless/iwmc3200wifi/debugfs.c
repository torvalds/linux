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

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/debugfs.h>

#include "iwm.h"
#include "bus.h"
#include "rx.h"
#include "debug.h"

static struct {
	u8 id;
	char *name;
} iwm_debug_module[__IWM_DM_NR] = {
	 {IWM_DM_BOOT, "boot"},
	 {IWM_DM_FW,   "fw"},
	 {IWM_DM_SDIO, "sdio"},
	 {IWM_DM_NTF,  "ntf"},
	 {IWM_DM_RX,   "rx"},
	 {IWM_DM_TX,   "tx"},
	 {IWM_DM_MLME, "mlme"},
	 {IWM_DM_CMD,  "cmd"},
	 {IWM_DM_WEXT,  "wext"},
};

#define add_dbg_module(dbg, name, id, initlevel) 	\
do {							\
	struct dentry *d;				\
	dbg.dbg_module[id] = (initlevel);		\
	d = debugfs_create_x8(name, 0600, dbg.dbgdir,	\
			     &(dbg.dbg_module[id]));	\
	if (!IS_ERR(d))					\
		dbg.dbg_module_dentries[id] = d;        \
} while (0)

static int iwm_debugfs_u32_read(void *data, u64 *val)
{
	struct iwm_priv *iwm = data;

	*val = iwm->dbg.dbg_level;
	return 0;
}

static int iwm_debugfs_dbg_level_write(void *data, u64 val)
{
	struct iwm_priv *iwm = data;
	int i;

	iwm->dbg.dbg_level = val;

	for (i = 0; i < __IWM_DM_NR; i++)
		iwm->dbg.dbg_module[i] = val;

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(fops_iwm_dbg_level,
			iwm_debugfs_u32_read, iwm_debugfs_dbg_level_write,
			"%llu\n");

static int iwm_debugfs_dbg_modules_write(void *data, u64 val)
{
	struct iwm_priv *iwm = data;
	int i, bit;

	iwm->dbg.dbg_modules = val;

	for (i = 0; i < __IWM_DM_NR; i++)
		iwm->dbg.dbg_module[i] = 0;

	for_each_bit(bit, &iwm->dbg.dbg_modules, __IWM_DM_NR)
		iwm->dbg.dbg_module[bit] = iwm->dbg.dbg_level;

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(fops_iwm_dbg_modules,
			iwm_debugfs_u32_read, iwm_debugfs_dbg_modules_write,
			"%llu\n");

static int iwm_txrx_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}


static ssize_t iwm_debugfs_txq_read(struct file *filp, char __user *buffer,
				   size_t count, loff_t *ppos)
{
	struct iwm_priv *iwm = filp->private_data;
	char *buf;
	int i, buf_len = 4096;
	size_t len = 0;
	ssize_t ret;

	if (*ppos != 0)
		return 0;
	if (count < sizeof(buf))
		return -ENOSPC;

	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (i = 0; i < IWM_TX_QUEUES; i++) {
		struct iwm_tx_queue *txq = &iwm->txq[i];
		struct sk_buff *skb;
		int j;
		unsigned long flags;

		spin_lock_irqsave(&txq->queue.lock, flags);

		skb = (struct sk_buff *)&txq->queue;

		len += snprintf(buf + len, buf_len - len, "TXQ #%d\n", i);
		len += snprintf(buf + len, buf_len - len, "\tStopped:     %d\n",
				__netif_subqueue_stopped(iwm_to_ndev(iwm),
							 txq->id));
		len += snprintf(buf + len, buf_len - len, "\tConcat count:%d\n",
				txq->concat_count);
		len += snprintf(buf + len, buf_len - len, "\tQueue len:   %d\n",
				skb_queue_len(&txq->queue));
		for (j = 0; j < skb_queue_len(&txq->queue); j++) {
			struct iwm_tx_info *tx_info;

			skb = skb->next;
			tx_info = skb_to_tx_info(skb);

			len += snprintf(buf + len, buf_len - len,
					"\tSKB #%d\n", j);
			len += snprintf(buf + len, buf_len - len,
					"\t\tsta:   %d\n", tx_info->sta);
			len += snprintf(buf + len, buf_len - len,
					"\t\tcolor: %d\n", tx_info->color);
			len += snprintf(buf + len, buf_len - len,
					"\t\ttid:   %d\n", tx_info->tid);
		}

		spin_unlock_irqrestore(&txq->queue.lock, flags);
	}

	ret = simple_read_from_buffer(buffer, len, ppos, buf, buf_len);
	kfree(buf);

	return ret;
}

static ssize_t iwm_debugfs_tx_credit_read(struct file *filp,
					  char __user *buffer,
					  size_t count, loff_t *ppos)
{
	struct iwm_priv *iwm = filp->private_data;
	struct iwm_tx_credit *credit = &iwm->tx_credit;
	char *buf;
	int i, buf_len = 4096;
	size_t len = 0;
	ssize_t ret;

	if (*ppos != 0)
		return 0;
	if (count < sizeof(buf))
		return -ENOSPC;

	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += snprintf(buf + len, buf_len - len,
			"NR pools:  %d\n", credit->pool_nr);
	len += snprintf(buf + len, buf_len - len,
			"pools map: 0x%lx\n", credit->full_pools_map);

	len += snprintf(buf + len, buf_len - len, "\n### POOLS ###\n");
	for (i = 0; i < IWM_MACS_OUT_GROUPS; i++) {
		len += snprintf(buf + len, buf_len - len,
				"pools entry #%d\n", i);
		len += snprintf(buf + len, buf_len - len,
				"\tid:          %d\n",
				credit->pools[i].id);
		len += snprintf(buf + len, buf_len - len,
				"\tsid:         %d\n",
				credit->pools[i].sid);
		len += snprintf(buf + len, buf_len - len,
				"\tmin_pages:   %d\n",
				credit->pools[i].min_pages);
		len += snprintf(buf + len, buf_len - len,
				"\tmax_pages:   %d\n",
				credit->pools[i].max_pages);
		len += snprintf(buf + len, buf_len - len,
				"\talloc_pages: %d\n",
				credit->pools[i].alloc_pages);
		len += snprintf(buf + len, buf_len - len,
				"\tfreed_pages: %d\n",
				credit->pools[i].total_freed_pages);
	}

	len += snprintf(buf + len, buf_len - len, "\n### SPOOLS ###\n");
	for (i = 0; i < IWM_MACS_OUT_SGROUPS; i++) {
		len += snprintf(buf + len, buf_len - len,
				"spools entry #%d\n", i);
		len += snprintf(buf + len, buf_len - len,
				"\tid:          %d\n",
				credit->spools[i].id);
		len += snprintf(buf + len, buf_len - len,
				"\tmax_pages:   %d\n",
				credit->spools[i].max_pages);
		len += snprintf(buf + len, buf_len - len,
				"\talloc_pages: %d\n",
				credit->spools[i].alloc_pages);

	}

	ret = simple_read_from_buffer(buffer, len, ppos, buf, buf_len);
	kfree(buf);

	return ret;
}

static ssize_t iwm_debugfs_rx_ticket_read(struct file *filp,
					  char __user *buffer,
					  size_t count, loff_t *ppos)
{
	struct iwm_priv *iwm = filp->private_data;
	struct iwm_rx_ticket_node *ticket, *next;
	char *buf;
	int buf_len = 4096, i;
	size_t len = 0;
	ssize_t ret;

	if (*ppos != 0)
		return 0;
	if (count < sizeof(buf))
		return -ENOSPC;

	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	list_for_each_entry_safe(ticket, next, &iwm->rx_tickets, node) {
		len += snprintf(buf + len, buf_len - len, "Ticket #%d\n",
				ticket->ticket->id);
		len += snprintf(buf + len, buf_len - len, "\taction: 0x%x\n",
				ticket->ticket->action);
		len += snprintf(buf + len, buf_len - len, "\tflags:  0x%x\n",
				ticket->ticket->flags);
	}

	for (i = 0; i < IWM_RX_ID_HASH; i++) {
		struct iwm_rx_packet *packet, *nxt;
		struct list_head *pkt_list = &iwm->rx_packets[i];
		if (!list_empty(pkt_list)) {
			len += snprintf(buf + len, buf_len - len,
					"Packet hash #%d\n", i);
			list_for_each_entry_safe(packet, nxt, pkt_list, node) {
				len += snprintf(buf + len, buf_len - len,
						"\tPacket id:     %d\n",
						packet->id);
				len += snprintf(buf + len, buf_len - len,
						"\tPacket length: %lu\n",
						packet->pkt_size);
			}
		}
	}

	ret = simple_read_from_buffer(buffer, len, ppos, buf, buf_len);
	kfree(buf);

	return ret;
}


static const struct file_operations iwm_debugfs_txq_fops = {
	.owner =	THIS_MODULE,
	.open =		iwm_txrx_open,
	.read =		iwm_debugfs_txq_read,
};

static const struct file_operations iwm_debugfs_tx_credit_fops = {
	.owner =	THIS_MODULE,
	.open =		iwm_txrx_open,
	.read =		iwm_debugfs_tx_credit_read,
};

static const struct file_operations iwm_debugfs_rx_ticket_fops = {
	.owner =	THIS_MODULE,
	.open =		iwm_txrx_open,
	.read =		iwm_debugfs_rx_ticket_read,
};

int iwm_debugfs_init(struct iwm_priv *iwm)
{
	int i, result;
	char devdir[16];

	iwm->dbg.rootdir = debugfs_create_dir(KBUILD_MODNAME, NULL);
	result = PTR_ERR(iwm->dbg.rootdir);
	if (!result || IS_ERR(iwm->dbg.rootdir)) {
		if (result == -ENODEV) {
			IWM_ERR(iwm, "DebugFS (CONFIG_DEBUG_FS) not "
				"enabled in kernel config\n");
			result = 0;	/* No debugfs support */
		}
		IWM_ERR(iwm, "Couldn't create rootdir: %d\n", result);
		goto error;
	}

	snprintf(devdir, sizeof(devdir), "%s", wiphy_name(iwm_to_wiphy(iwm)));

	iwm->dbg.devdir = debugfs_create_dir(devdir, iwm->dbg.rootdir);
	result = PTR_ERR(iwm->dbg.devdir);
	if (IS_ERR(iwm->dbg.devdir) && (result != -ENODEV)) {
		IWM_ERR(iwm, "Couldn't create devdir: %d\n", result);
		goto error;
	}

	iwm->dbg.dbgdir = debugfs_create_dir("debug", iwm->dbg.devdir);
	result = PTR_ERR(iwm->dbg.dbgdir);
	if (IS_ERR(iwm->dbg.dbgdir) && (result != -ENODEV)) {
		IWM_ERR(iwm, "Couldn't create dbgdir: %d\n", result);
		goto error;
	}

	iwm->dbg.rxdir = debugfs_create_dir("rx", iwm->dbg.devdir);
	result = PTR_ERR(iwm->dbg.rxdir);
	if (IS_ERR(iwm->dbg.rxdir) && (result != -ENODEV)) {
		IWM_ERR(iwm, "Couldn't create rx dir: %d\n", result);
		goto error;
	}

	iwm->dbg.txdir = debugfs_create_dir("tx", iwm->dbg.devdir);
	result = PTR_ERR(iwm->dbg.txdir);
	if (IS_ERR(iwm->dbg.txdir) && (result != -ENODEV)) {
		IWM_ERR(iwm, "Couldn't create tx dir: %d\n", result);
		goto error;
	}

	iwm->dbg.busdir = debugfs_create_dir("bus", iwm->dbg.devdir);
	result = PTR_ERR(iwm->dbg.busdir);
	if (IS_ERR(iwm->dbg.busdir) && (result != -ENODEV)) {
		IWM_ERR(iwm, "Couldn't create bus dir: %d\n", result);
		goto error;
	}

	if (iwm->bus_ops->debugfs_init) {
		result = iwm->bus_ops->debugfs_init(iwm, iwm->dbg.busdir);
		if (result < 0) {
			IWM_ERR(iwm, "Couldn't create bus entry: %d\n", result);
			goto error;
		}
	}


	iwm->dbg.dbg_level = IWM_DL_NONE;
	iwm->dbg.dbg_level_dentry =
		debugfs_create_file("level", 0200, iwm->dbg.dbgdir, iwm,
				    &fops_iwm_dbg_level);
	result = PTR_ERR(iwm->dbg.dbg_level_dentry);
	if (IS_ERR(iwm->dbg.dbg_level_dentry) && (result != -ENODEV)) {
		IWM_ERR(iwm, "Couldn't create dbg_level: %d\n", result);
		goto error;
	}


	iwm->dbg.dbg_modules = IWM_DM_DEFAULT;
	iwm->dbg.dbg_modules_dentry =
		debugfs_create_file("modules", 0200, iwm->dbg.dbgdir, iwm,
				    &fops_iwm_dbg_modules);
	result = PTR_ERR(iwm->dbg.dbg_modules_dentry);
	if (IS_ERR(iwm->dbg.dbg_modules_dentry) && (result != -ENODEV)) {
		IWM_ERR(iwm, "Couldn't create dbg_modules: %d\n", result);
		goto error;
	}

	for (i = 0; i < __IWM_DM_NR; i++)
		add_dbg_module(iwm->dbg, iwm_debug_module[i].name,
			       iwm_debug_module[i].id, IWM_DL_DEFAULT);

	iwm->dbg.txq_dentry = debugfs_create_file("queues", 0200,
						  iwm->dbg.txdir, iwm,
						  &iwm_debugfs_txq_fops);
	result = PTR_ERR(iwm->dbg.txq_dentry);
	if (IS_ERR(iwm->dbg.txq_dentry) && (result != -ENODEV)) {
		IWM_ERR(iwm, "Couldn't create tx queue: %d\n", result);
		goto error;
	}

	iwm->dbg.tx_credit_dentry = debugfs_create_file("credits", 0200,
						   iwm->dbg.txdir, iwm,
						   &iwm_debugfs_tx_credit_fops);
	result = PTR_ERR(iwm->dbg.tx_credit_dentry);
	if (IS_ERR(iwm->dbg.tx_credit_dentry) && (result != -ENODEV)) {
		IWM_ERR(iwm, "Couldn't create tx credit: %d\n", result);
		goto error;
	}

	iwm->dbg.rx_ticket_dentry = debugfs_create_file("tickets", 0200,
						  iwm->dbg.rxdir, iwm,
						  &iwm_debugfs_rx_ticket_fops);
	result = PTR_ERR(iwm->dbg.rx_ticket_dentry);
	if (IS_ERR(iwm->dbg.rx_ticket_dentry) && (result != -ENODEV)) {
		IWM_ERR(iwm, "Couldn't create rx ticket: %d\n", result);
		goto error;
	}

	return 0;

 error:
	return result;
}

void iwm_debugfs_exit(struct iwm_priv *iwm)
{
	int i;

	for (i = 0; i < __IWM_DM_NR; i++)
		debugfs_remove(iwm->dbg.dbg_module_dentries[i]);

	debugfs_remove(iwm->dbg.dbg_modules_dentry);
	debugfs_remove(iwm->dbg.dbg_level_dentry);
	debugfs_remove(iwm->dbg.txq_dentry);
	debugfs_remove(iwm->dbg.tx_credit_dentry);
	debugfs_remove(iwm->dbg.rx_ticket_dentry);
	if (iwm->bus_ops->debugfs_exit)
		iwm->bus_ops->debugfs_exit(iwm);

	debugfs_remove(iwm->dbg.busdir);
	debugfs_remove(iwm->dbg.dbgdir);
	debugfs_remove(iwm->dbg.txdir);
	debugfs_remove(iwm->dbg.rxdir);
	debugfs_remove(iwm->dbg.devdir);
	debugfs_remove(iwm->dbg.rootdir);
}
