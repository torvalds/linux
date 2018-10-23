// SPDX-License-Identifier: GPL-2.0
/*
 * PCI I/O adapter configuration related functions.
 *
 * Copyright IBM Corp. 2016
 */
#define KMSG_COMPONENT "sclp_cmd"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/completion.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/err.h>

#include <asm/sclp.h>

#include "sclp.h"

#define SCLP_CMDW_CONFIGURE_PCI			0x001a0001
#define SCLP_CMDW_DECONFIGURE_PCI		0x001b0001

#define SCLP_ATYPE_PCI				2

#define SCLP_ERRNOTIFY_AQ_RESET			0
#define SCLP_ERRNOTIFY_AQ_REPAIR		1
#define SCLP_ERRNOTIFY_AQ_INFO_LOG		2

static DEFINE_MUTEX(sclp_pci_mutex);
static struct sclp_register sclp_pci_event = {
	.send_mask = EVTYP_ERRNOTIFY_MASK,
};

struct err_notify_evbuf {
	struct evbuf_header header;
	u8 action;
	u8 atype;
	u32 fh;
	u32 fid;
	u8 data[0];
} __packed;

struct err_notify_sccb {
	struct sccb_header header;
	struct err_notify_evbuf evbuf;
} __packed;

struct pci_cfg_sccb {
	struct sccb_header header;
	u8 atype;		/* adapter type */
	u8 reserved1;
	u16 reserved2;
	u32 aid;		/* adapter identifier */
} __packed;

static int do_pci_configure(sclp_cmdw_t cmd, u32 fid)
{
	struct pci_cfg_sccb *sccb;
	int rc;

	if (!SCLP_HAS_PCI_RECONFIG)
		return -EOPNOTSUPP;

	sccb = (struct pci_cfg_sccb *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!sccb)
		return -ENOMEM;

	sccb->header.length = PAGE_SIZE;
	sccb->atype = SCLP_ATYPE_PCI;
	sccb->aid = fid;
	rc = sclp_sync_request(cmd, sccb);
	if (rc)
		goto out;
	switch (sccb->header.response_code) {
	case 0x0020:
	case 0x0120:
		break;
	default:
		pr_warn("configure PCI I/O adapter failed: cmd=0x%08x  response=0x%04x\n",
			cmd, sccb->header.response_code);
		rc = -EIO;
		break;
	}
out:
	free_page((unsigned long) sccb);
	return rc;
}

int sclp_pci_configure(u32 fid)
{
	return do_pci_configure(SCLP_CMDW_CONFIGURE_PCI, fid);
}
EXPORT_SYMBOL(sclp_pci_configure);

int sclp_pci_deconfigure(u32 fid)
{
	return do_pci_configure(SCLP_CMDW_DECONFIGURE_PCI, fid);
}
EXPORT_SYMBOL(sclp_pci_deconfigure);

static void sclp_pci_callback(struct sclp_req *req, void *data)
{
	struct completion *completion = data;

	complete(completion);
}

static int sclp_pci_check_report(struct zpci_report_error_header *report)
{
	if (report->version != 1)
		return -EINVAL;

	switch (report->action) {
	case SCLP_ERRNOTIFY_AQ_RESET:
	case SCLP_ERRNOTIFY_AQ_REPAIR:
	case SCLP_ERRNOTIFY_AQ_INFO_LOG:
		break;
	default:
		return -EINVAL;
	}

	if (report->length > (PAGE_SIZE - sizeof(struct err_notify_sccb)))
		return -EINVAL;

	return 0;
}

int sclp_pci_report(struct zpci_report_error_header *report, u32 fh, u32 fid)
{
	DECLARE_COMPLETION_ONSTACK(completion);
	struct err_notify_sccb *sccb;
	struct sclp_req req;
	int ret;

	ret = sclp_pci_check_report(report);
	if (ret)
		return ret;

	mutex_lock(&sclp_pci_mutex);
	ret = sclp_register(&sclp_pci_event);
	if (ret)
		goto out_unlock;

	if (!(sclp_pci_event.sclp_receive_mask & EVTYP_ERRNOTIFY_MASK)) {
		ret = -EOPNOTSUPP;
		goto out_unregister;
	}

	sccb = (void *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!sccb) {
		ret = -ENOMEM;
		goto out_unregister;
	}

	memset(&req, 0, sizeof(req));
	req.callback_data = &completion;
	req.callback = sclp_pci_callback;
	req.command = SCLP_CMDW_WRITE_EVENT_DATA;
	req.status = SCLP_REQ_FILLED;
	req.sccb = sccb;

	sccb->evbuf.header.length = sizeof(sccb->evbuf) + report->length;
	sccb->evbuf.header.type = EVTYP_ERRNOTIFY;
	sccb->header.length = sizeof(sccb->header) + sccb->evbuf.header.length;

	sccb->evbuf.action = report->action;
	sccb->evbuf.atype = SCLP_ATYPE_PCI;
	sccb->evbuf.fh = fh;
	sccb->evbuf.fid = fid;

	memcpy(sccb->evbuf.data, report->data, report->length);

	ret = sclp_add_request(&req);
	if (ret)
		goto out_free_req;

	wait_for_completion(&completion);
	if (req.status != SCLP_REQ_DONE) {
		pr_warn("request failed (status=0x%02x)\n",
			req.status);
		ret = -EIO;
		goto out_free_req;
	}

	if (sccb->header.response_code != 0x0020) {
		pr_warn("request failed with response code 0x%x\n",
			sccb->header.response_code);
		ret = -EIO;
	}

out_free_req:
	free_page((unsigned long) sccb);
out_unregister:
	sclp_unregister(&sclp_pci_event);
out_unlock:
	mutex_unlock(&sclp_pci_mutex);
	return ret;
}
