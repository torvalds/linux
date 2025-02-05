// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2024 Hisilicon Limited.

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/seq_file.h>
#include <linux/string_choices.h>
#include "hbg_common.h"
#include "hbg_debugfs.h"
#include "hbg_hw.h"
#include "hbg_irq.h"
#include "hbg_txrx.h"

static struct dentry *hbg_dbgfs_root;

struct hbg_dbg_info {
	const char *name;
	int (*read)(struct seq_file *seq, void *data);
};

#define state_str_true_false(p, s) str_true_false(test_bit(s, &(p)->state))

static void hbg_dbg_ring(struct hbg_priv *priv, struct hbg_ring *ring,
			 struct seq_file *s)
{
	u32 irq_mask = ring->dir == HBG_DIR_TX ? HBG_INT_MSK_TX_B :
						 HBG_INT_MSK_RX_B;

	seq_printf(s, "ring used num: %u\n",
		   hbg_get_queue_used_num(ring));
	seq_printf(s, "ring max num: %u\n", ring->len);
	seq_printf(s, "ring head: %u, tail: %u\n", ring->head, ring->tail);
	seq_printf(s, "fifo used num: %u\n",
		   hbg_hw_get_fifo_used_num(priv, ring->dir));
	seq_printf(s, "fifo max num: %u\n",
		   hbg_get_spec_fifo_max_num(priv, ring->dir));
	seq_printf(s, "irq enabled: %s\n",
		   str_true_false(hbg_hw_irq_is_enabled(priv, irq_mask)));
}

static int hbg_dbg_tx_ring(struct seq_file *s, void *unused)
{
	struct net_device *netdev = dev_get_drvdata(s->private);
	struct hbg_priv *priv = netdev_priv(netdev);

	hbg_dbg_ring(priv, &priv->tx_ring, s);
	return 0;
}

static int hbg_dbg_rx_ring(struct seq_file *s, void *unused)
{
	struct net_device *netdev = dev_get_drvdata(s->private);
	struct hbg_priv *priv = netdev_priv(netdev);

	hbg_dbg_ring(priv, &priv->rx_ring, s);
	return 0;
}

static int hbg_dbg_irq_info(struct seq_file *s, void *unused)
{
	struct net_device *netdev = dev_get_drvdata(s->private);
	struct hbg_priv *priv = netdev_priv(netdev);
	struct hbg_irq_info *info;
	u32 i;

	for (i = 0; i < priv->vectors.info_array_len; i++) {
		info = &priv->vectors.info_array[i];
		seq_printf(s,
			   "%-20s: enabled: %-5s, logged: %-5s, count: %llu\n",
			   info->name,
			   str_true_false(hbg_hw_irq_is_enabled(priv,
								info->mask)),
			   str_true_false(info->need_print),
			   info->count);
	}

	return 0;
}

static int hbg_dbg_mac_table(struct seq_file *s, void *unused)
{
	struct net_device *netdev = dev_get_drvdata(s->private);
	struct hbg_priv *priv = netdev_priv(netdev);
	struct hbg_mac_filter *filter;
	u32 i;

	filter = &priv->filter;
	seq_printf(s, "mac addr max count: %u\n", filter->table_max_len);
	seq_printf(s, "filter enabled: %s\n", str_true_false(filter->enabled));

	for (i = 0; i < filter->table_max_len; i++) {
		if (is_zero_ether_addr(filter->mac_table[i].addr))
			continue;

		seq_printf(s, "[%u] %pM\n", i, filter->mac_table[i].addr);
	}

	return 0;
}

static const char * const reset_type_str[] = {"None", "FLR", "Function"};

static int hbg_dbg_nic_state(struct seq_file *s, void *unused)
{
	struct net_device *netdev = dev_get_drvdata(s->private);
	struct hbg_priv *priv = netdev_priv(netdev);

	seq_printf(s, "event handling state: %s\n",
		   state_str_true_false(priv, HBG_NIC_STATE_EVENT_HANDLING));
	seq_printf(s, "resetting state: %s\n",
		   state_str_true_false(priv, HBG_NIC_STATE_RESETTING));
	seq_printf(s, "reset fail state: %s\n",
		   state_str_true_false(priv, HBG_NIC_STATE_RESET_FAIL));
	seq_printf(s, "last reset type: %s\n",
		   reset_type_str[priv->reset_type]);

	return 0;
}

static const struct hbg_dbg_info hbg_dbg_infos[] = {
	{ "tx_ring", hbg_dbg_tx_ring },
	{ "rx_ring", hbg_dbg_rx_ring },
	{ "irq_info", hbg_dbg_irq_info },
	{ "mac_table", hbg_dbg_mac_table },
	{ "nic_state", hbg_dbg_nic_state },
};

static void hbg_debugfs_uninit(void *data)
{
	debugfs_remove_recursive((struct dentry *)data);
}

void hbg_debugfs_init(struct hbg_priv *priv)
{
	const char *name = pci_name(priv->pdev);
	struct device *dev = &priv->pdev->dev;
	struct dentry *root;
	u32 i;

	root = debugfs_create_dir(name, hbg_dbgfs_root);

	for (i = 0; i < ARRAY_SIZE(hbg_dbg_infos); i++)
		debugfs_create_devm_seqfile(dev, hbg_dbg_infos[i].name,
					    root, hbg_dbg_infos[i].read);

	/* Ignore the failure because debugfs is not a key feature. */
	devm_add_action_or_reset(dev, hbg_debugfs_uninit, root);
}

void hbg_debugfs_register(void)
{
	hbg_dbgfs_root = debugfs_create_dir("hibmcge", NULL);
}

void hbg_debugfs_unregister(void)
{
	debugfs_remove_recursive(hbg_dbgfs_root);
	hbg_dbgfs_root = NULL;
}
