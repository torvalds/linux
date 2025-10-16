// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2007,2012
 *
 * Author(s): Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#define KMSG_COMPONENT "sclp_cmd"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/completion.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/chpid.h>
#include <asm/ctlreg.h>
#include <asm/sclp.h>

#include "sclp.h"

/* CPU configuration related functions */
#define SCLP_CMDW_CONFIGURE_CPU			0x00110001
#define SCLP_CMDW_DECONFIGURE_CPU		0x00100001
/* Channel path configuration related functions */
#define SCLP_CMDW_CONFIGURE_CHPATH		0x000f0001
#define SCLP_CMDW_DECONFIGURE_CHPATH		0x000e0001
#define SCLP_CMDW_READ_CHPATH_INFORMATION	0x00030001

struct cpu_configure_sccb {
	struct sccb_header header;
} __packed __aligned(8);

struct chp_cfg_sccb {
	struct sccb_header header;
	u8 ccm;
	u8 reserved[6];
	u8 cssid;
} __packed;

struct chp_info_sccb {
	struct sccb_header header;
	u8 recognized[SCLP_CHP_INFO_MASK_SIZE];
	u8 standby[SCLP_CHP_INFO_MASK_SIZE];
	u8 configured[SCLP_CHP_INFO_MASK_SIZE];
	u8 ccm;
	u8 reserved[6];
	u8 cssid;
} __packed;

static void sclp_sync_callback(struct sclp_req *req, void *data)
{
	struct completion *completion = data;

	complete(completion);
}

int sclp_sync_request(sclp_cmdw_t cmd, void *sccb)
{
	return sclp_sync_request_timeout(cmd, sccb, 0);
}

int sclp_sync_request_timeout(sclp_cmdw_t cmd, void *sccb, int timeout)
{
	struct completion completion;
	struct sclp_req *request;
	int rc;

	request = kzalloc(sizeof(*request), GFP_KERNEL);
	if (!request)
		return -ENOMEM;
	if (timeout)
		request->queue_timeout = timeout;
	request->command = cmd;
	request->sccb = sccb;
	request->status = SCLP_REQ_FILLED;
	request->callback = sclp_sync_callback;
	request->callback_data = &completion;
	init_completion(&completion);

	rc = sclp_add_request(request);
	if (rc)
		goto out;
	wait_for_completion(&completion);

	if (request->status != SCLP_REQ_DONE) {
		pr_warn("sync request failed (cmd=0x%08x, status=0x%02x)\n",
			cmd, request->status);
		rc = -EIO;
	}
out:
	kfree(request);
	return rc;
}

int _sclp_get_core_info(struct sclp_core_info *info)
{
	struct read_cpu_info_sccb *sccb;
	int rc, length;

	if (!SCLP_HAS_CPU_INFO)
		return -EOPNOTSUPP;

	length = test_facility(140) ? EXT_SCCB_READ_CPU : PAGE_SIZE;
	sccb = (void *)__get_free_pages(GFP_KERNEL | GFP_DMA | __GFP_ZERO, get_order(length));
	if (!sccb)
		return -ENOMEM;
	sccb->header.length = length;
	sccb->header.control_mask[2] = 0x80;
	rc = sclp_sync_request_timeout(SCLP_CMDW_READ_CPU_INFO, sccb,
				       SCLP_QUEUE_INTERVAL);
	if (rc)
		goto out;
	if (sccb->header.response_code != 0x0010) {
		pr_warn("readcpuinfo failed (response=0x%04x)\n",
			sccb->header.response_code);
		rc = -EIO;
		goto out;
	}
	sclp_fill_core_info(info, sccb);
out:
	free_pages((unsigned long)sccb, get_order(length));
	return rc;
}

static int do_core_configure(sclp_cmdw_t cmd)
{
	struct cpu_configure_sccb *sccb;
	int rc;

	if (!SCLP_HAS_CPU_RECONFIG)
		return -EOPNOTSUPP;
	/*
	 * Use kmalloc to have a minimum alignment of 8 bytes and ensure sccb
	 * is not going to cross a page boundary.
	 */
	sccb = kzalloc(sizeof(*sccb), GFP_KERNEL | GFP_DMA);
	if (!sccb)
		return -ENOMEM;
	sccb->header.length = sizeof(*sccb);
	rc = sclp_sync_request_timeout(cmd, sccb, SCLP_QUEUE_INTERVAL);
	if (rc)
		goto out;
	switch (sccb->header.response_code) {
	case 0x0020:
	case 0x0120:
		break;
	default:
		pr_warn("configure cpu failed (cmd=0x%08x, response=0x%04x)\n",
			cmd, sccb->header.response_code);
		rc = -EIO;
		break;
	}
out:
	kfree(sccb);
	return rc;
}

int sclp_core_configure(u8 core)
{
	return do_core_configure(SCLP_CMDW_CONFIGURE_CPU | core << 8);
}

int sclp_core_deconfigure(u8 core)
{
	return do_core_configure(SCLP_CMDW_DECONFIGURE_CPU | core << 8);
}

static int do_chp_configure(sclp_cmdw_t cmd)
{
	struct chp_cfg_sccb *sccb;
	int rc;

	if (!SCLP_HAS_CHP_RECONFIG)
		return -EOPNOTSUPP;
	sccb = (struct chp_cfg_sccb *)get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!sccb)
		return -ENOMEM;
	sccb->header.length = sizeof(*sccb);
	rc = sclp_sync_request(cmd, sccb);
	if (rc)
		goto out;
	switch (sccb->header.response_code) {
	case 0x0020:
	case 0x0120:
	case 0x0440:
	case 0x0450:
		break;
	default:
		pr_warn("configure channel-path failed (cmd=0x%08x, response=0x%04x)\n",
			cmd, sccb->header.response_code);
		rc = -EIO;
		break;
	}
out:
	free_page((unsigned long)sccb);
	return rc;
}

/**
 * sclp_chp_configure - perform configure channel-path sclp command
 * @chpid: channel-path ID
 *
 * Perform configure channel-path command sclp command for specified chpid.
 * Return 0 after command successfully finished, non-zero otherwise.
 */
int sclp_chp_configure(struct chp_id chpid)
{
	return do_chp_configure(SCLP_CMDW_CONFIGURE_CHPATH | chpid.id << 8);
}

/**
 * sclp_chp_deconfigure - perform deconfigure channel-path sclp command
 * @chpid: channel-path ID
 *
 * Perform deconfigure channel-path command sclp command for specified chpid
 * and wait for completion. On success return 0. Return non-zero otherwise.
 */
int sclp_chp_deconfigure(struct chp_id chpid)
{
	return do_chp_configure(SCLP_CMDW_DECONFIGURE_CHPATH | chpid.id << 8);
}

/**
 * sclp_chp_read_info - perform read channel-path information sclp command
 * @info: resulting channel-path information data
 *
 * Perform read channel-path information sclp command and wait for completion.
 * On success, store channel-path information in @info and return 0. Return
 * non-zero otherwise.
 */
int sclp_chp_read_info(struct sclp_chp_info *info)
{
	struct chp_info_sccb *sccb;
	int rc;

	if (!SCLP_HAS_CHP_INFO)
		return -EOPNOTSUPP;
	sccb = (struct chp_info_sccb *)get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!sccb)
		return -ENOMEM;
	sccb->header.length = sizeof(*sccb);
	rc = sclp_sync_request(SCLP_CMDW_READ_CHPATH_INFORMATION, sccb);
	if (rc)
		goto out;
	if (sccb->header.response_code != 0x0010) {
		pr_warn("read channel-path info failed (response=0x%04x)\n",
			sccb->header.response_code);
		rc = -EIO;
		goto out;
	}
	memcpy(info->recognized, sccb->recognized, SCLP_CHP_INFO_MASK_SIZE);
	memcpy(info->standby, sccb->standby, SCLP_CHP_INFO_MASK_SIZE);
	memcpy(info->configured, sccb->configured, SCLP_CHP_INFO_MASK_SIZE);
out:
	free_page((unsigned long)sccb);
	return rc;
}
