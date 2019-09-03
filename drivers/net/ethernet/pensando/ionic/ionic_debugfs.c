// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#include <linux/pci.h>
#include <linux/netdevice.h>

#include "ionic.h"
#include "ionic_bus.h"
#include "ionic_lif.h"
#include "ionic_debugfs.h"

#ifdef CONFIG_DEBUG_FS

static struct dentry *ionic_dir;

void ionic_debugfs_create(void)
{
	ionic_dir = debugfs_create_dir(IONIC_DRV_NAME, NULL);
}

void ionic_debugfs_destroy(void)
{
	debugfs_remove_recursive(ionic_dir);
}

void ionic_debugfs_add_dev(struct ionic *ionic)
{
	ionic->dentry = debugfs_create_dir(ionic_bus_info(ionic), ionic_dir);
}

void ionic_debugfs_del_dev(struct ionic *ionic)
{
	debugfs_remove_recursive(ionic->dentry);
	ionic->dentry = NULL;
}

static int identity_show(struct seq_file *seq, void *v)
{
	struct ionic *ionic = seq->private;
	struct ionic_identity *ident;

	ident = &ionic->ident;

	seq_printf(seq, "nlifs:            %d\n", ident->dev.nlifs);
	seq_printf(seq, "nintrs:           %d\n", ident->dev.nintrs);
	seq_printf(seq, "ndbpgs_per_lif:   %d\n", ident->dev.ndbpgs_per_lif);
	seq_printf(seq, "intr_coal_mult:   %d\n", ident->dev.intr_coal_mult);
	seq_printf(seq, "intr_coal_div:    %d\n", ident->dev.intr_coal_div);

	seq_printf(seq, "max_ucast_filters:  %d\n", ident->lif.eth.max_ucast_filters);
	seq_printf(seq, "max_mcast_filters:  %d\n", ident->lif.eth.max_mcast_filters);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(identity);

void ionic_debugfs_add_ident(struct ionic *ionic)
{
	debugfs_create_file("identity", 0400, ionic->dentry,
			    ionic, &identity_fops) ? 0 : -EOPNOTSUPP;
}

void ionic_debugfs_add_sizes(struct ionic *ionic)
{
	debugfs_create_u32("nlifs", 0400, ionic->dentry,
			   (u32 *)&ionic->ident.dev.nlifs);
	debugfs_create_u32("nintrs", 0400, ionic->dentry, &ionic->nintrs);

	debugfs_create_u32("ntxqs_per_lif", 0400, ionic->dentry,
			   (u32 *)&ionic->ident.lif.eth.config.queue_count[IONIC_QTYPE_TXQ]);
	debugfs_create_u32("nrxqs_per_lif", 0400, ionic->dentry,
			   (u32 *)&ionic->ident.lif.eth.config.queue_count[IONIC_QTYPE_RXQ]);
}

static int netdev_show(struct seq_file *seq, void *v)
{
	struct net_device *netdev = seq->private;

	seq_printf(seq, "%s\n", netdev->name);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(netdev);

void ionic_debugfs_add_lif(struct ionic_lif *lif)
{
	lif->dentry = debugfs_create_dir(lif->name, lif->ionic->dentry);
	debugfs_create_file("netdev", 0400, lif->dentry,
			    lif->netdev, &netdev_fops);
}

void ionic_debugfs_del_lif(struct ionic_lif *lif)
{
	debugfs_remove_recursive(lif->dentry);
	lif->dentry = NULL;
}
#endif
