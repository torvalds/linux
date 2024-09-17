// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2019 NXP */

#include <linux/debugfs.h>
#include "compat.h"
#include "debugfs.h"
#include "regs.h"
#include "intern.h"

static int caam_debugfs_u64_get(void *data, u64 *val)
{
	*val = caam64_to_cpu(*(u64 *)data);
	return 0;
}

static int caam_debugfs_u32_get(void *data, u64 *val)
{
	*val = caam32_to_cpu(*(u32 *)data);
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(caam_fops_u32_ro, caam_debugfs_u32_get, NULL, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(caam_fops_u64_ro, caam_debugfs_u64_get, NULL, "%llu\n");

#ifdef CONFIG_CAAM_QI
/*
 * This is a counter for the number of times the congestion group (where all
 * the request and response queueus are) reached congestion. Incremented
 * each time the congestion callback is called with congested == true.
 */
static u64 times_congested;

void caam_debugfs_qi_congested(void)
{
	times_congested++;
}

void caam_debugfs_qi_init(struct caam_drv_private *ctrlpriv)
{
	debugfs_create_file("qi_congested", 0444, ctrlpriv->ctl,
			    &times_congested, &caam_fops_u64_ro);
}
#endif

void caam_debugfs_init(struct caam_drv_private *ctrlpriv, struct dentry *root)
{
	struct caam_perfmon *perfmon;

	/*
	 * FIXME: needs better naming distinction, as some amalgamation of
	 * "caam" and nprop->full_name. The OF name isn't distinctive,
	 * but does separate instances
	 */
	perfmon = (struct caam_perfmon __force *)&ctrlpriv->ctrl->perfmon;

	ctrlpriv->ctl = debugfs_create_dir("ctl", root);

	debugfs_create_file("rq_dequeued", 0444, ctrlpriv->ctl,
			    &perfmon->req_dequeued, &caam_fops_u64_ro);
	debugfs_create_file("ob_rq_encrypted", 0444, ctrlpriv->ctl,
			    &perfmon->ob_enc_req, &caam_fops_u64_ro);
	debugfs_create_file("ib_rq_decrypted", 0444, ctrlpriv->ctl,
			    &perfmon->ib_dec_req, &caam_fops_u64_ro);
	debugfs_create_file("ob_bytes_encrypted", 0444, ctrlpriv->ctl,
			    &perfmon->ob_enc_bytes, &caam_fops_u64_ro);
	debugfs_create_file("ob_bytes_protected", 0444, ctrlpriv->ctl,
			    &perfmon->ob_prot_bytes, &caam_fops_u64_ro);
	debugfs_create_file("ib_bytes_decrypted", 0444, ctrlpriv->ctl,
			    &perfmon->ib_dec_bytes, &caam_fops_u64_ro);
	debugfs_create_file("ib_bytes_validated", 0444, ctrlpriv->ctl,
			    &perfmon->ib_valid_bytes, &caam_fops_u64_ro);

	/* Controller level - global status values */
	debugfs_create_file("fault_addr", 0444, ctrlpriv->ctl,
			    &perfmon->faultaddr, &caam_fops_u32_ro);
	debugfs_create_file("fault_detail", 0444, ctrlpriv->ctl,
			    &perfmon->faultdetail, &caam_fops_u32_ro);
	debugfs_create_file("fault_status", 0444, ctrlpriv->ctl,
			    &perfmon->status, &caam_fops_u32_ro);

	/* Internal covering keys (useful in non-secure mode only) */
	ctrlpriv->ctl_kek_wrap.data = (__force void *)&ctrlpriv->ctrl->kek[0];
	ctrlpriv->ctl_kek_wrap.size = KEK_KEY_SIZE * sizeof(u32);
	debugfs_create_blob("kek", 0444, ctrlpriv->ctl,
			    &ctrlpriv->ctl_kek_wrap);

	ctrlpriv->ctl_tkek_wrap.data = (__force void *)&ctrlpriv->ctrl->tkek[0];
	ctrlpriv->ctl_tkek_wrap.size = KEK_KEY_SIZE * sizeof(u32);
	debugfs_create_blob("tkek", 0444, ctrlpriv->ctl,
			    &ctrlpriv->ctl_tkek_wrap);

	ctrlpriv->ctl_tdsk_wrap.data = (__force void *)&ctrlpriv->ctrl->tdsk[0];
	ctrlpriv->ctl_tdsk_wrap.size = KEK_KEY_SIZE * sizeof(u32);
	debugfs_create_blob("tdsk", 0444, ctrlpriv->ctl,
			    &ctrlpriv->ctl_tdsk_wrap);
}
