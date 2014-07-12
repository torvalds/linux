/*
 * Copyright (c) 2012 Broadcom Corporation
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
#include <linux/netdevice.h>
#include <linux/module.h>

#include <brcmu_wifi.h>
#include <brcmu_utils.h>
#include "dhd.h"
#include "dhd_bus.h"
#include "dhd_dbg.h"

static struct dentry *root_folder;

void brcmf_debugfs_init(void)
{
	root_folder = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (IS_ERR(root_folder))
		root_folder = NULL;
}

void brcmf_debugfs_exit(void)
{
	if (!root_folder)
		return;

	debugfs_remove_recursive(root_folder);
	root_folder = NULL;
}

static int brcmf_debugfs_chipinfo_read(struct seq_file *seq, void *data)
{
	struct brcmf_pub *drvr = seq->private;
	struct brcmf_bus *bus = drvr->bus_if;

	seq_printf(seq, "chip: %x(%u) rev %u\n",
		   bus->chip, bus->chip, bus->chiprev);
	return 0;
}

static int brcmf_debugfs_chipinfo_open(struct inode *inode, struct file *f)
{
	return single_open(f, brcmf_debugfs_chipinfo_read, inode->i_private);
}

static const struct file_operations brcmf_debugfs_chipinfo_ops = {
	.owner = THIS_MODULE,
	.open = brcmf_debugfs_chipinfo_open,
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek
};

static int brcmf_debugfs_create_chipinfo(struct brcmf_pub *drvr)
{
	struct dentry *dentry =  drvr->dbgfs_dir;

	if (!IS_ERR_OR_NULL(dentry))
		debugfs_create_file("chipinfo", S_IRUGO, dentry, drvr,
				    &brcmf_debugfs_chipinfo_ops);
	return 0;
}

int brcmf_debugfs_attach(struct brcmf_pub *drvr)
{
	struct device *dev = drvr->bus_if->dev;

	if (!root_folder)
		return -ENODEV;

	drvr->dbgfs_dir = debugfs_create_dir(dev_name(dev), root_folder);
	brcmf_debugfs_create_chipinfo(drvr);
	return PTR_ERR_OR_ZERO(drvr->dbgfs_dir);
}

void brcmf_debugfs_detach(struct brcmf_pub *drvr)
{
	if (!IS_ERR_OR_NULL(drvr->dbgfs_dir))
		debugfs_remove_recursive(drvr->dbgfs_dir);
}

struct dentry *brcmf_debugfs_get_devdir(struct brcmf_pub *drvr)
{
	return drvr->dbgfs_dir;
}

static int brcmf_debugfs_sdio_count_read(struct seq_file *seq, void *data)
{
	struct brcmf_sdio_count *sdcnt = seq->private;

	seq_printf(seq,
		   "intrcount:    %u\nlastintrs:    %u\n"
		   "pollcnt:      %u\nregfails:     %u\n"
		   "tx_sderrs:    %u\nfcqueued:     %u\n"
		   "rxrtx:        %u\nrx_toolong:   %u\n"
		   "rxc_errors:   %u\nrx_hdrfail:   %u\n"
		   "rx_badhdr:    %u\nrx_badseq:    %u\n"
		   "fc_rcvd:      %u\nfc_xoff:      %u\n"
		   "fc_xon:       %u\nrxglomfail:   %u\n"
		   "rxglomframes: %u\nrxglompkts:   %u\n"
		   "f2rxhdrs:     %u\nf2rxdata:     %u\n"
		   "f2txdata:     %u\nf1regdata:    %u\n"
		   "tickcnt:      %u\ntx_ctlerrs:   %lu\n"
		   "tx_ctlpkts:   %lu\nrx_ctlerrs:   %lu\n"
		   "rx_ctlpkts:   %lu\nrx_readahead: %lu\n",
		   sdcnt->intrcount, sdcnt->lastintrs,
		   sdcnt->pollcnt, sdcnt->regfails,
		   sdcnt->tx_sderrs, sdcnt->fcqueued,
		   sdcnt->rxrtx, sdcnt->rx_toolong,
		   sdcnt->rxc_errors, sdcnt->rx_hdrfail,
		   sdcnt->rx_badhdr, sdcnt->rx_badseq,
		   sdcnt->fc_rcvd, sdcnt->fc_xoff,
		   sdcnt->fc_xon, sdcnt->rxglomfail,
		   sdcnt->rxglomframes, sdcnt->rxglompkts,
		   sdcnt->f2rxhdrs, sdcnt->f2rxdata,
		   sdcnt->f2txdata, sdcnt->f1regdata,
		   sdcnt->tickcnt, sdcnt->tx_ctlerrs,
		   sdcnt->tx_ctlpkts, sdcnt->rx_ctlerrs,
		   sdcnt->rx_ctlpkts, sdcnt->rx_readahead_cnt);

	return 0;
}

static int brcmf_debugfs_sdio_count_open(struct inode *inode, struct file *f)
{
	return single_open(f, brcmf_debugfs_sdio_count_read, inode->i_private);
}

static const struct file_operations brcmf_debugfs_sdio_counter_ops = {
	.owner = THIS_MODULE,
	.open = brcmf_debugfs_sdio_count_open,
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek
};

void brcmf_debugfs_create_sdio_count(struct brcmf_pub *drvr,
				     struct brcmf_sdio_count *sdcnt)
{
	struct dentry *dentry = drvr->dbgfs_dir;

	if (!IS_ERR_OR_NULL(dentry))
		debugfs_create_file("counters", S_IRUGO, dentry,
				    sdcnt, &brcmf_debugfs_sdio_counter_ops);
}

static int brcmf_debugfs_fws_stats_read(struct seq_file *seq, void *data)
{
	struct brcmf_fws_stats *fwstats = seq->private;

	seq_printf(seq,
		   "header_pulls:      %u\n"
		   "header_only_pkt:   %u\n"
		   "tlv_parse_failed:  %u\n"
		   "tlv_invalid_type:  %u\n"
		   "mac_update_fails:  %u\n"
		   "ps_update_fails:   %u\n"
		   "if_update_fails:   %u\n"
		   "pkt2bus:           %u\n"
		   "generic_error:     %u\n"
		   "rollback_success:  %u\n"
		   "rollback_failed:   %u\n"
		   "delayq_full:       %u\n"
		   "supprq_full:       %u\n"
		   "txs_indicate:      %u\n"
		   "txs_discard:       %u\n"
		   "txs_suppr_core:    %u\n"
		   "txs_suppr_ps:      %u\n"
		   "txs_tossed:        %u\n"
		   "txs_host_tossed:   %u\n"
		   "bus_flow_block:    %u\n"
		   "fws_flow_block:    %u\n"
		   "send_pkts:         BK:%u BE:%u VO:%u VI:%u BCMC:%u\n"
		   "requested_sent:    BK:%u BE:%u VO:%u VI:%u BCMC:%u\n",
		   fwstats->header_pulls,
		   fwstats->header_only_pkt,
		   fwstats->tlv_parse_failed,
		   fwstats->tlv_invalid_type,
		   fwstats->mac_update_failed,
		   fwstats->mac_ps_update_failed,
		   fwstats->if_update_failed,
		   fwstats->pkt2bus,
		   fwstats->generic_error,
		   fwstats->rollback_success,
		   fwstats->rollback_failed,
		   fwstats->delayq_full_error,
		   fwstats->supprq_full_error,
		   fwstats->txs_indicate,
		   fwstats->txs_discard,
		   fwstats->txs_supp_core,
		   fwstats->txs_supp_ps,
		   fwstats->txs_tossed,
		   fwstats->txs_host_tossed,
		   fwstats->bus_flow_block,
		   fwstats->fws_flow_block,
		   fwstats->send_pkts[0], fwstats->send_pkts[1],
		   fwstats->send_pkts[2], fwstats->send_pkts[3],
		   fwstats->send_pkts[4],
		   fwstats->requested_sent[0],
		   fwstats->requested_sent[1],
		   fwstats->requested_sent[2],
		   fwstats->requested_sent[3],
		   fwstats->requested_sent[4]);

	return 0;
}

static int brcmf_debugfs_fws_stats_open(struct inode *inode, struct file *f)
{
	return single_open(f, brcmf_debugfs_fws_stats_read, inode->i_private);
}

static const struct file_operations brcmf_debugfs_fws_stats_ops = {
	.owner = THIS_MODULE,
	.open = brcmf_debugfs_fws_stats_open,
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek
};

void brcmf_debugfs_create_fws_stats(struct brcmf_pub *drvr,
				    struct brcmf_fws_stats *stats)
{
	struct dentry *dentry =  drvr->dbgfs_dir;

	if (!IS_ERR_OR_NULL(dentry))
		debugfs_create_file("fws_stats", S_IRUGO, dentry,
				    stats, &brcmf_debugfs_fws_stats_ops);
}
