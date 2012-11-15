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

int brcmf_debugfs_attach(struct brcmf_pub *drvr)
{
	if (!root_folder)
		return -ENODEV;

	drvr->dbgfs_dir = debugfs_create_dir(dev_name(drvr->dev), root_folder);
	return PTR_RET(drvr->dbgfs_dir);
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

static
ssize_t brcmf_debugfs_sdio_counter_read(struct file *f, char __user *data,
					size_t count, loff_t *ppos)
{
	struct brcmf_sdio_count *sdcnt = f->private_data;
	char buf[750];
	int res;

	/* only allow read from start */
	if (*ppos > 0)
		return 0;

	res = scnprintf(buf, sizeof(buf),
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

	return simple_read_from_buffer(data, count, ppos, buf, res);
}

static const struct file_operations brcmf_debugfs_sdio_counter_ops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = brcmf_debugfs_sdio_counter_read
};

void brcmf_debugfs_create_sdio_count(struct brcmf_pub *drvr,
				     struct brcmf_sdio_count *sdcnt)
{
	struct dentry *dentry = drvr->dbgfs_dir;

	if (!IS_ERR_OR_NULL(dentry))
		debugfs_create_file("counters", S_IRUGO, dentry,
				    sdcnt, &brcmf_debugfs_sdio_counter_ops);
}
