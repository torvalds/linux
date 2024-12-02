// SPDX-License-Identifier: GPL-2.0
/*
 * s390 crypto adapter related sclp functions.
 *
 * Copyright IBM Corp. 2020
 */
#define KMSG_COMPONENT "sclp_cmd"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/export.h>
#include <linux/slab.h>
#include <asm/sclp.h>
#include "sclp.h"

#define SCLP_CMDW_CONFIGURE_AP			0x001f0001
#define SCLP_CMDW_DECONFIGURE_AP		0x001e0001

struct ap_cfg_sccb {
	struct sccb_header header;
} __packed;

static int do_ap_configure(sclp_cmdw_t cmd, u32 apid)
{
	struct ap_cfg_sccb *sccb;
	int rc;

	if (!SCLP_HAS_AP_RECONFIG)
		return -EOPNOTSUPP;

	sccb = (struct ap_cfg_sccb *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!sccb)
		return -ENOMEM;

	sccb->header.length = PAGE_SIZE;
	cmd |= (apid & 0xFF) << 8;
	rc = sclp_sync_request(cmd, sccb);
	if (rc)
		goto out;
	switch (sccb->header.response_code) {
	case 0x0020: case 0x0120: case 0x0440: case 0x0450:
		break;
	default:
		pr_warn("configure AP adapter %u failed: cmd=0x%08x response=0x%04x\n",
			apid, cmd, sccb->header.response_code);
		rc = -EIO;
		break;
	}
out:
	free_page((unsigned long) sccb);
	return rc;
}

int sclp_ap_configure(u32 apid)
{
	return do_ap_configure(SCLP_CMDW_CONFIGURE_AP, apid);
}
EXPORT_SYMBOL(sclp_ap_configure);

int sclp_ap_deconfigure(u32 apid)
{
	return do_ap_configure(SCLP_CMDW_DECONFIGURE_AP, apid);
}
EXPORT_SYMBOL(sclp_ap_deconfigure);
