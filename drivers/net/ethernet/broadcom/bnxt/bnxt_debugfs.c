/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2017-2018 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/defs.h>
#include <linux/module.h>
#include <linux/pci.h>
#include "bnxt_hsi.h"
#include <linux/net_dim.h>
#include "bnxt.h"
#include "bnxt_defs.h"

static struct dentry *bnxt_de_mnt;

static ssize_t defs_dim_read(struct file *filep,
				char __user *buffer,
				size_t count, loff_t *ppos)
{
	struct net_dim *dim = filep->private_data;
	int len;
	char *buf;

	if (*ppos)
		return 0;
	if (!dim)
		return -ENODEV;
	buf = kasprintf(GFP_KERNEL,
			"state = %d\n" \
			"profile_ix = %d\n" \
			"mode = %d\n" \
			"tune_state = %d\n" \
			"steps_right = %d\n" \
			"steps_left = %d\n" \
			"tired = %d\n",
			dim->state,
			dim->profile_ix,
			dim->mode,
			dim->tune_state,
			dim->steps_right,
			dim->steps_left,
			dim->tired);
	if (!buf)
		return -ENOMEM;
	if (count < strlen(buf)) {
		kfree(buf);
		return -ENOSPC;
	}
	len = simple_read_from_buffer(buffer, count, ppos, buf, strlen(buf));
	kfree(buf);
	return len;
}

static const struct file_operations defs_dim_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = defs_dim_read,
};

static struct dentry *defs_dim_ring_init(struct net_dim *dim, int ring_idx,
					    struct dentry *dd)
{
	static char qname[16];

	snprintf(qname, 10, "%d", ring_idx);
	return defs_create_file(qname, 0600, dd,
				   dim, &defs_dim_fops);
}

void bnxt_de_dev_init(struct bnxt *bp)
{
	const char *pname = pci_name(bp->pdev);
	struct dentry *pdevf;
	int i;

	bp->defs_pdev = defs_create_dir(pname, bnxt_de_mnt);
	if (bp->defs_pdev) {
		pdevf = defs_create_dir("dim", bp->defs_pdev);
		if (!pdevf) {
			pr_err("failed to create defs entry %s/dim\n",
			       pname);
			return;
		}
		bp->defs_dim = pdevf;
		/* create files for each rx ring */
		for (i = 0; i < bp->cp_nr_rings; i++) {
			struct bnxt_cp_ring_info *cpr = &bp->bnapi[i]->cp_ring;

			if (cpr && bp->bnapi[i]->rx_ring) {
				pdevf = defs_dim_ring_init(&cpr->dim, i,
							      bp->defs_dim);
				if (!pdevf)
					pr_err("failed to create defs entry %s/dim/%d\n",
					       pname, i);
			}
		}
	} else {
		pr_err("failed to create defs entry %s\n", pname);
	}
}

void bnxt_de_dev_exit(struct bnxt *bp)
{
	if (bp) {
		defs_remove_recursive(bp->defs_pdev);
		bp->defs_pdev = NULL;
	}
}

void bnxt_de_init(void)
{
	bnxt_de_mnt = defs_create_dir("bnxt_en", NULL);
	if (!bnxt_de_mnt)
		pr_err("failed to init bnxt_en defs\n");
}

void bnxt_de_exit(void)
{
	defs_remove_recursive(bnxt_de_mnt);
}
