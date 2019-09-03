// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#include <linux/netdevice.h>

#include "ionic.h"
#include "ionic_bus.h"
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

#endif
