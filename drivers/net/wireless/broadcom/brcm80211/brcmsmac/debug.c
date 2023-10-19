/*
 * Copyright (c) 2012 Broadcom Corporation
 * Copyright (c) 2012 Canonical Ltd.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <linux/debugfs.h>
#include <linux/if_ether.h>
#include <linux/if.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/ieee80211.h>
#include <linux/module.h>
#include <net/mac80211.h>

#include <defs.h>
#include <brcmu_wifi.h>
#include <brcmu_utils.h>
#include "types.h"
#include "main.h"
#include "debug.h"
#include "brcms_trace_events.h"
#include "phy/phy_int.h"

static struct dentry *root_folder;

void brcms_debugfs_init(void)
{
	root_folder = debugfs_create_dir(KBUILD_MODNAME, NULL);
}

void brcms_debugfs_exit(void)
{
	debugfs_remove_recursive(root_folder);
	root_folder = NULL;
}

void brcms_debugfs_attach(struct brcms_pub *drvr)
{
	drvr->dbgfs_dir = debugfs_create_dir(
		 dev_name(&drvr->wlc->hw->d11core->dev), root_folder);
}

void brcms_debugfs_detach(struct brcms_pub *drvr)
{
	debugfs_remove_recursive(drvr->dbgfs_dir);
}

struct dentry *brcms_debugfs_get_devdir(struct brcms_pub *drvr)
{
	return drvr->dbgfs_dir;
}

static
int brcms_debugfs_hardware_read(struct seq_file *s, void *data)
{
	struct brcms_pub *drvr = s->private;
	struct brcms_hardware *hw = drvr->wlc->hw;
	struct bcma_device *core = hw->d11core;
	struct bcma_bus *bus = core->bus;
	char boardrev[BRCMU_BOARDREV_LEN];

	seq_printf(s, "chipnum 0x%x\n"
		   "chiprev 0x%x\n"
		   "chippackage 0x%x\n"
		   "corerev 0x%x\n"
		   "boardid 0x%x\n"
		   "boardvendor 0x%x\n"
		   "boardrev %s\n"
		   "boardflags 0x%x\n"
		   "boardflags2 0x%x\n"
		   "ucoderev 0x%x\n"
		   "radiorev 0x%x\n"
		   "phytype 0x%x\n"
		   "phyrev 0x%x\n"
		   "anarev 0x%x\n"
		   "nvramrev %d\n",
		   bus->chipinfo.id, bus->chipinfo.rev, bus->chipinfo.pkg,
		   core->id.rev, bus->boardinfo.type, bus->boardinfo.vendor,
		   brcmu_boardrev_str(hw->boardrev, boardrev),
		   drvr->wlc->hw->boardflags, drvr->wlc->hw->boardflags2,
		   drvr->wlc->ucode_rev, hw->band->radiorev,
		   hw->band->phytype, hw->band->phyrev, hw->band->pi->ana_rev,
		   hw->sromrev);
	return 0;
}

static int brcms_debugfs_macstat_read(struct seq_file *s, void *data)
{
	struct brcms_pub *drvr = s->private;
	struct brcms_info *wl = drvr->ieee_hw->priv;
	struct macstat stats;
	int i;

	spin_lock_bh(&wl->lock);
	stats = *(drvr->wlc->core->macstat_snapshot);
	spin_unlock_bh(&wl->lock);

	seq_printf(s, "txallfrm: %d\n", stats.txallfrm);
	seq_printf(s, "txrtsfrm: %d\n", stats.txrtsfrm);
	seq_printf(s, "txctsfrm: %d\n", stats.txctsfrm);
	seq_printf(s, "txackfrm: %d\n", stats.txackfrm);
	seq_printf(s, "txdnlfrm: %d\n", stats.txdnlfrm);
	seq_printf(s, "txbcnfrm: %d\n", stats.txbcnfrm);
	seq_printf(s, "txfunfl[8]:");
	for (i = 0; i < ARRAY_SIZE(stats.txfunfl); i++)
		seq_printf(s, " %d", stats.txfunfl[i]);
	seq_printf(s, "\ntxtplunfl: %d\n", stats.txtplunfl);
	seq_printf(s, "txphyerr: %d\n", stats.txphyerr);
	seq_printf(s, "pktengrxducast: %d\n", stats.pktengrxducast);
	seq_printf(s, "pktengrxdmcast: %d\n", stats.pktengrxdmcast);
	seq_printf(s, "rxfrmtoolong: %d\n", stats.rxfrmtoolong);
	seq_printf(s, "rxfrmtooshrt: %d\n", stats.rxfrmtooshrt);
	seq_printf(s, "rxinvmachdr: %d\n", stats.rxinvmachdr);
	seq_printf(s, "rxbadfcs: %d\n", stats.rxbadfcs);
	seq_printf(s, "rxbadplcp: %d\n", stats.rxbadplcp);
	seq_printf(s, "rxcrsglitch: %d\n", stats.rxcrsglitch);
	seq_printf(s, "rxstrt: %d\n", stats.rxstrt);
	seq_printf(s, "rxdfrmucastmbss: %d\n", stats.rxdfrmucastmbss);
	seq_printf(s, "rxmfrmucastmbss: %d\n", stats.rxmfrmucastmbss);
	seq_printf(s, "rxcfrmucast: %d\n", stats.rxcfrmucast);
	seq_printf(s, "rxrtsucast: %d\n", stats.rxrtsucast);
	seq_printf(s, "rxctsucast: %d\n", stats.rxctsucast);
	seq_printf(s, "rxackucast: %d\n", stats.rxackucast);
	seq_printf(s, "rxdfrmocast: %d\n", stats.rxdfrmocast);
	seq_printf(s, "rxmfrmocast: %d\n", stats.rxmfrmocast);
	seq_printf(s, "rxcfrmocast: %d\n", stats.rxcfrmocast);
	seq_printf(s, "rxrtsocast: %d\n", stats.rxrtsocast);
	seq_printf(s, "rxctsocast: %d\n", stats.rxctsocast);
	seq_printf(s, "rxdfrmmcast: %d\n", stats.rxdfrmmcast);
	seq_printf(s, "rxmfrmmcast: %d\n", stats.rxmfrmmcast);
	seq_printf(s, "rxcfrmmcast: %d\n", stats.rxcfrmmcast);
	seq_printf(s, "rxbeaconmbss: %d\n", stats.rxbeaconmbss);
	seq_printf(s, "rxdfrmucastobss: %d\n", stats.rxdfrmucastobss);
	seq_printf(s, "rxbeaconobss: %d\n", stats.rxbeaconobss);
	seq_printf(s, "rxrsptmout: %d\n", stats.rxrsptmout);
	seq_printf(s, "bcntxcancl: %d\n", stats.bcntxcancl);
	seq_printf(s, "rxf0ovfl: %d\n", stats.rxf0ovfl);
	seq_printf(s, "rxf1ovfl: %d\n", stats.rxf1ovfl);
	seq_printf(s, "rxf2ovfl: %d\n", stats.rxf2ovfl);
	seq_printf(s, "txsfovfl: %d\n", stats.txsfovfl);
	seq_printf(s, "pmqovfl: %d\n", stats.pmqovfl);
	seq_printf(s, "rxcgprqfrm: %d\n", stats.rxcgprqfrm);
	seq_printf(s, "rxcgprsqovfl: %d\n", stats.rxcgprsqovfl);
	seq_printf(s, "txcgprsfail: %d\n", stats.txcgprsfail);
	seq_printf(s, "txcgprssuc: %d\n", stats.txcgprssuc);
	seq_printf(s, "prs_timeout: %d\n", stats.prs_timeout);
	seq_printf(s, "rxnack: %d\n", stats.rxnack);
	seq_printf(s, "frmscons: %d\n", stats.frmscons);
	seq_printf(s, "txnack: %d\n", stats.txnack);
	seq_printf(s, "txglitch_nack: %d\n", stats.txglitch_nack);
	seq_printf(s, "txburst: %d\n", stats.txburst);
	seq_printf(s, "bphy_rxcrsglitch: %d\n", stats.bphy_rxcrsglitch);
	seq_printf(s, "phywatchdog: %d\n", stats.phywatchdog);
	seq_printf(s, "bphy_badplcp: %d\n", stats.bphy_badplcp);
	return 0;
}

struct brcms_debugfs_entry {
	int (*read)(struct seq_file *seq, void *data);
	struct brcms_pub *drvr;
};

static int brcms_debugfs_entry_open(struct inode *inode, struct file *f)
{
	struct brcms_debugfs_entry *entry = inode->i_private;

	return single_open(f, entry->read, entry->drvr);
}

static const struct file_operations brcms_debugfs_def_ops = {
	.owner = THIS_MODULE,
	.open = brcms_debugfs_entry_open,
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek
};

static void
brcms_debugfs_add_entry(struct brcms_pub *drvr, const char *fn,
			int (*read_fn)(struct seq_file *seq, void *data))
{
	struct device *dev = &drvr->wlc->hw->d11core->dev;
	struct dentry *dentry =  drvr->dbgfs_dir;
	struct brcms_debugfs_entry *entry;

	entry = devm_kzalloc(dev, sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return;

	entry->read = read_fn;
	entry->drvr = drvr;

	debugfs_create_file(fn, 0444, dentry, entry, &brcms_debugfs_def_ops);
}

void brcms_debugfs_create_files(struct brcms_pub *drvr)
{
	brcms_debugfs_add_entry(drvr, "hardware", brcms_debugfs_hardware_read);
	brcms_debugfs_add_entry(drvr, "macstat", brcms_debugfs_macstat_read);
}

#define __brcms_fn(fn)						\
void __brcms_ ##fn(struct device *dev, const char *fmt, ...)	\
{								\
	struct va_format vaf = {				\
		.fmt = fmt,					\
	};							\
	va_list args;						\
								\
	va_start(args, fmt);					\
	vaf.va = &args;						\
	dev_ ##fn(dev, "%pV", &vaf);				\
	trace_brcms_ ##fn(&vaf);				\
	va_end(args);						\
}

__brcms_fn(info)
__brcms_fn(warn)
__brcms_fn(err)
__brcms_fn(crit)

#if defined(CONFIG_BRCMDBG) || defined(CONFIG_BRCM_TRACING)
void __brcms_dbg(struct device *dev, u32 level, const char *func,
		 const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
#ifdef CONFIG_BRCMDBG
	if ((brcm_msg_level & level) && net_ratelimit())
		dev_err(dev, "%s %pV", func, &vaf);
#endif
	trace_brcms_dbg(level, func, &vaf);
	va_end(args);
}
#endif
