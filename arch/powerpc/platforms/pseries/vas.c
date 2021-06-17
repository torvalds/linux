// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2020-21 IBM Corp.
 */

#define pr_fmt(fmt) "vas: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <asm/machdep.h>
#include <asm/hvcall.h>
#include <asm/plpar_wrappers.h>
#include <asm/vas.h>
#include "vas.h"

#define VAS_INVALID_WIN_ADDRESS	0xFFFFFFFFFFFFFFFFul
#define VAS_DEFAULT_DOMAIN_ID	0xFFFFFFFFFFFFFFFFul
/* The hypervisor allows one credit per window right now */
#define DEF_WIN_CREDS		1

static struct vas_all_caps caps_all;
static bool copypaste_feat;

static struct vas_caps vascaps[VAS_MAX_FEAT_TYPE];

static long hcall_return_busy_check(long rc)
{
	/* Check if we are stalled for some time */
	if (H_IS_LONG_BUSY(rc)) {
		msleep(get_longbusy_msecs(rc));
		rc = H_BUSY;
	} else if (rc == H_BUSY) {
		cond_resched();
	}

	return rc;
}

/*
 * Allocate VAS window hcall
 */
static int h_allocate_vas_window(struct pseries_vas_window *win, u64 *domain,
				     u8 wintype, u16 credits)
{
	long retbuf[PLPAR_HCALL9_BUFSIZE] = {0};
	long rc;

	do {
		rc = plpar_hcall9(H_ALLOCATE_VAS_WINDOW, retbuf, wintype,
				  credits, domain[0], domain[1], domain[2],
				  domain[3], domain[4], domain[5]);

		rc = hcall_return_busy_check(rc);
	} while (rc == H_BUSY);

	if (rc == H_SUCCESS) {
		if (win->win_addr == VAS_INVALID_WIN_ADDRESS) {
			pr_err("H_ALLOCATE_VAS_WINDOW: COPY/PASTE is not supported\n");
			return -ENOTSUPP;
		}
		win->vas_win.winid = retbuf[0];
		win->win_addr = retbuf[1];
		win->complete_irq = retbuf[2];
		win->fault_irq = retbuf[3];
		return 0;
	}

	pr_err("H_ALLOCATE_VAS_WINDOW error: %ld, wintype: %u, credits: %u\n",
		rc, wintype, credits);

	return -EIO;
}

/*
 * Deallocate VAS window hcall.
 */
static int h_deallocate_vas_window(u64 winid)
{
	long rc;

	do {
		rc = plpar_hcall_norets(H_DEALLOCATE_VAS_WINDOW, winid);

		rc = hcall_return_busy_check(rc);
	} while (rc == H_BUSY);

	if (rc == H_SUCCESS)
		return 0;

	pr_err("H_DEALLOCATE_VAS_WINDOW error: %ld, winid: %llu\n",
		rc, winid);
	return -EIO;
}

/*
 * Modify VAS window.
 * After the window is opened with allocate window hcall, configure it
 * with flags and LPAR PID before using.
 */
static int h_modify_vas_window(struct pseries_vas_window *win)
{
	long rc;
	u32 lpid = mfspr(SPRN_PID);

	/*
	 * AMR value is not supported in Linux VAS implementation.
	 * The hypervisor ignores it if 0 is passed.
	 */
	do {
		rc = plpar_hcall_norets(H_MODIFY_VAS_WINDOW,
					win->vas_win.winid, lpid, 0,
					VAS_MOD_WIN_FLAGS, 0);

		rc = hcall_return_busy_check(rc);
	} while (rc == H_BUSY);

	if (rc == H_SUCCESS)
		return 0;

	pr_err("H_MODIFY_VAS_WINDOW error: %ld, winid %u lpid %u\n",
			rc, win->vas_win.winid, lpid);
	return -EIO;
}

/*
 * This hcall is used to determine the capabilities from the hypervisor.
 * @hcall: H_QUERY_VAS_CAPABILITIES or H_QUERY_NX_CAPABILITIES
 * @query_type: If 0 is passed, the hypervisor returns the overall
 *		capabilities which provides all feature(s) that are
 *		available. Then query the hypervisor to get the
 *		corresponding capabilities for the specific feature.
 *		Example: H_QUERY_VAS_CAPABILITIES provides VAS GZIP QoS
 *			and VAS GZIP Default capabilities.
 *			H_QUERY_NX_CAPABILITIES provides NX GZIP
 *			capabilities.
 * @result: Return buffer to save capabilities.
 */
int h_query_vas_capabilities(const u64 hcall, u8 query_type, u64 result)
{
	long rc;

	rc = plpar_hcall_norets(hcall, query_type, result);

	if (rc == H_SUCCESS)
		return 0;

	pr_err("HCALL(%llx) error %ld, query_type %u, result buffer 0x%llx\n",
			hcall, rc, query_type, result);
	return -EIO;
}

/*
 * Get the specific capabilities based on the feature type.
 * Right now supports GZIP default and GZIP QoS capabilities.
 */
static int get_vas_capabilities(u8 feat, enum vas_cop_feat_type type,
				struct hv_vas_cop_feat_caps *hv_caps)
{
	struct vas_cop_feat_caps *caps;
	struct vas_caps *vcaps;
	int rc = 0;

	vcaps = &vascaps[type];
	memset(vcaps, 0, sizeof(*vcaps));
	INIT_LIST_HEAD(&vcaps->list);

	caps = &vcaps->caps;

	rc = h_query_vas_capabilities(H_QUERY_VAS_CAPABILITIES, feat,
					  (u64)virt_to_phys(hv_caps));
	if (rc)
		return rc;

	caps->user_mode = hv_caps->user_mode;
	if (!(caps->user_mode & VAS_COPY_PASTE_USER_MODE)) {
		pr_err("User space COPY/PASTE is not supported\n");
		return -ENOTSUPP;
	}

	caps->descriptor = be64_to_cpu(hv_caps->descriptor);
	caps->win_type = hv_caps->win_type;
	if (caps->win_type >= VAS_MAX_FEAT_TYPE) {
		pr_err("Unsupported window type %u\n", caps->win_type);
		return -EINVAL;
	}
	caps->max_lpar_creds = be16_to_cpu(hv_caps->max_lpar_creds);
	caps->max_win_creds = be16_to_cpu(hv_caps->max_win_creds);
	atomic_set(&caps->target_lpar_creds,
		   be16_to_cpu(hv_caps->target_lpar_creds));
	if (feat == VAS_GZIP_DEF_FEAT) {
		caps->def_lpar_creds = be16_to_cpu(hv_caps->def_lpar_creds);

		if (caps->max_win_creds < DEF_WIN_CREDS) {
			pr_err("Window creds(%u) > max allowed window creds(%u)\n",
			       DEF_WIN_CREDS, caps->max_win_creds);
			return -EINVAL;
		}
	}

	copypaste_feat = true;

	return 0;
}

static int __init pseries_vas_init(void)
{
	struct hv_vas_cop_feat_caps *hv_cop_caps;
	struct hv_vas_all_caps *hv_caps;
	int rc;

	/*
	 * Linux supports user space COPY/PASTE only with Radix
	 */
	if (!radix_enabled()) {
		pr_err("API is supported only with radix page tables\n");
		return -ENOTSUPP;
	}

	hv_caps = kmalloc(sizeof(*hv_caps), GFP_KERNEL);
	if (!hv_caps)
		return -ENOMEM;
	/*
	 * Get VAS overall capabilities by passing 0 to feature type.
	 */
	rc = h_query_vas_capabilities(H_QUERY_VAS_CAPABILITIES, 0,
					  (u64)virt_to_phys(hv_caps));
	if (rc)
		goto out;

	caps_all.descriptor = be64_to_cpu(hv_caps->descriptor);
	caps_all.feat_type = be64_to_cpu(hv_caps->feat_type);

	hv_cop_caps = kmalloc(sizeof(*hv_cop_caps), GFP_KERNEL);
	if (!hv_cop_caps) {
		rc = -ENOMEM;
		goto out;
	}
	/*
	 * QOS capabilities available
	 */
	if (caps_all.feat_type & VAS_GZIP_QOS_FEAT_BIT) {
		rc = get_vas_capabilities(VAS_GZIP_QOS_FEAT,
					  VAS_GZIP_QOS_FEAT_TYPE, hv_cop_caps);

		if (rc)
			goto out_cop;
	}
	/*
	 * Default capabilities available
	 */
	if (caps_all.feat_type & VAS_GZIP_DEF_FEAT_BIT) {
		rc = get_vas_capabilities(VAS_GZIP_DEF_FEAT,
					  VAS_GZIP_DEF_FEAT_TYPE, hv_cop_caps);
		if (rc)
			goto out_cop;
	}

	pr_info("GZIP feature is available\n");

out_cop:
	kfree(hv_cop_caps);
out:
	kfree(hv_caps);
	return rc;
}
machine_device_initcall(pseries, pseries_vas_init);
