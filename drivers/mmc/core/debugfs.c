/*
 * Debugfs support for hosts and cards
 *
 * Copyright (C) 2008 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>

#include "core.h"
#include "mmc_ops.h"

/* The debugfs functions are optimized away when CONFIG_DEBUG_FS isn't set. */
static int mmc_ios_show(struct seq_file *s, void *data)
{
	static const char *vdd_str[] = {
		[8]	= "2.0",
		[9]	= "2.1",
		[10]	= "2.2",
		[11]	= "2.3",
		[12]	= "2.4",
		[13]	= "2.5",
		[14]	= "2.6",
		[15]	= "2.7",
		[16]	= "2.8",
		[17]	= "2.9",
		[18]	= "3.0",
		[19]	= "3.1",
		[20]	= "3.2",
		[21]	= "3.3",
		[22]	= "3.4",
		[23]	= "3.5",
		[24]	= "3.6",
	};
	struct mmc_host	*host = s->private;
	struct mmc_ios	*ios = &host->ios;
	const char *str;

	seq_printf(s, "clock:\t\t%u Hz\n", ios->clock);
	seq_printf(s, "vdd:\t\t%u ", ios->vdd);
	if ((1 << ios->vdd) & MMC_VDD_165_195)
		seq_printf(s, "(1.65 - 1.95 V)\n");
	else if (ios->vdd < (ARRAY_SIZE(vdd_str) - 1)
			&& vdd_str[ios->vdd] && vdd_str[ios->vdd + 1])
		seq_printf(s, "(%s ~ %s V)\n", vdd_str[ios->vdd],
				vdd_str[ios->vdd + 1]);
	else
		seq_printf(s, "(invalid)\n");

	switch (ios->bus_mode) {
	case MMC_BUSMODE_OPENDRAIN:
		str = "open drain";
		break;
	case MMC_BUSMODE_PUSHPULL:
		str = "push-pull";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "bus mode:\t%u (%s)\n", ios->bus_mode, str);

	switch (ios->chip_select) {
	case MMC_CS_DONTCARE:
		str = "don't care";
		break;
	case MMC_CS_HIGH:
		str = "active high";
		break;
	case MMC_CS_LOW:
		str = "active low";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "chip select:\t%u (%s)\n", ios->chip_select, str);

	switch (ios->power_mode) {
	case MMC_POWER_OFF:
		str = "off";
		break;
	case MMC_POWER_UP:
		str = "up";
		break;
	case MMC_POWER_ON:
		str = "on";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "power mode:\t%u (%s)\n", ios->power_mode, str);
	seq_printf(s, "bus width:\t%u (%u bits)\n",
			ios->bus_width, 1 << ios->bus_width);

	switch (ios->timing) {
	case MMC_TIMING_LEGACY:
		str = "legacy";
		break;
	case MMC_TIMING_MMC_HS:
		str = "mmc high-speed";
		break;
	case MMC_TIMING_SD_HS:
		str = "sd high-speed";
		break;
	default:
		str = "invalid";
		break;
	}
	seq_printf(s, "timing spec:\t%u (%s)\n", ios->timing, str);

	return 0;
}

static int mmc_ios_open(struct inode *inode, struct file *file)
{
	return single_open(file, mmc_ios_show, inode->i_private);
}

static const struct file_operations mmc_ios_fops = {
	.open		= mmc_ios_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void mmc_add_host_debugfs(struct mmc_host *host)
{
	struct dentry *root;

	root = debugfs_create_dir(mmc_hostname(host), NULL);
	if (IS_ERR(root))
		/* Don't complain -- debugfs just isn't enabled */
		return;
	if (!root)
		/* Complain -- debugfs is enabled, but it failed to
		 * create the directory. */
		goto err_root;

	host->debugfs_root = root;

	if (!debugfs_create_file("ios", S_IRUSR, root, host, &mmc_ios_fops))
		goto err_ios;

	return;

err_ios:
	debugfs_remove_recursive(root);
	host->debugfs_root = NULL;
err_root:
	dev_err(&host->class_dev, "failed to initialize debugfs\n");
}

void mmc_remove_host_debugfs(struct mmc_host *host)
{
	debugfs_remove_recursive(host->debugfs_root);
}

static int mmc_dbg_card_status_get(void *data, u64 *val)
{
	struct mmc_card	*card = data;
	u32		status;
	int		ret;

	mmc_claim_host(card->host);

	ret = mmc_send_status(data, &status);
	if (!ret)
		*val = status;

	mmc_release_host(card->host);

	return ret;
}
DEFINE_SIMPLE_ATTRIBUTE(mmc_dbg_card_status_fops, mmc_dbg_card_status_get,
		NULL, "%08llx\n");

void mmc_add_card_debugfs(struct mmc_card *card)
{
	struct mmc_host	*host = card->host;
	struct dentry	*root;

	if (!host->debugfs_root)
		return;

	root = debugfs_create_dir(mmc_card_id(card), host->debugfs_root);
	if (IS_ERR(root))
		/* Don't complain -- debugfs just isn't enabled */
		return;
	if (!root)
		/* Complain -- debugfs is enabled, but it failed to
		 * create the directory. */
		goto err;

	card->debugfs_root = root;

	if (!debugfs_create_x32("state", S_IRUSR, root, &card->state))
		goto err;

	if (mmc_card_mmc(card) || mmc_card_sd(card))
		if (!debugfs_create_file("status", S_IRUSR, root, card,
					&mmc_dbg_card_status_fops))
			goto err;

	return;

err:
	debugfs_remove_recursive(root);
	card->debugfs_root = NULL;
	dev_err(&card->dev, "failed to initialize debugfs\n");
}

void mmc_remove_card_debugfs(struct mmc_card *card)
{
	debugfs_remove_recursive(card->debugfs_root);
}
