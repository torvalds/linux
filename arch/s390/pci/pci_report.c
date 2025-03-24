// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2024
 *
 * Author(s):
 *   Niklas Schnelle <schnelle@linux.ibm.com>
 *
 */

#define KMSG_COMPONENT "zpci"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/sprintf.h>
#include <linux/pci.h>

#include <asm/sclp.h>
#include <asm/debug.h>
#include <asm/pci_debug.h>

#include "pci_report.h"

#define ZPCI_ERR_LOG_ID_KERNEL_REPORT 0x4714

struct zpci_report_error_data {
	u64 timestamp;
	u64 err_log_id;
	char log_data[];
} __packed;

#define ZPCI_REPORT_SIZE	(PAGE_SIZE - sizeof(struct err_notify_sccb))
#define ZPCI_REPORT_DATA_SIZE	(ZPCI_REPORT_SIZE - sizeof(struct zpci_report_error_data))

struct zpci_report_error {
	struct zpci_report_error_header header;
	struct zpci_report_error_data data;
} __packed;

static const char *zpci_state_str(pci_channel_state_t state)
{
	switch (state) {
	case pci_channel_io_normal:
		return "normal";
	case pci_channel_io_frozen:
		return "frozen";
	case pci_channel_io_perm_failure:
		return "permanent-failure";
	default:
		return "invalid";
	};
}

static int debug_log_header_fn(debug_info_t *id, struct debug_view *view,
			       int area, debug_entry_t *entry, char *out_buf,
			       size_t out_buf_size)
{
	unsigned long sec, usec;
	unsigned int level;
	char *except_str;
	int rc = 0;

	level = entry->level;
	sec = entry->clock;
	usec = do_div(sec, USEC_PER_SEC);

	if (entry->exception)
		except_str = "*";
	else
		except_str = "-";
	rc += scnprintf(out_buf, out_buf_size, "%011ld:%06lu %1u %1s %04u  ",
			sec, usec, level, except_str,
			entry->cpu);
	return rc;
}

static int debug_prolog_header(debug_info_t *id, struct debug_view *view,
			       char *out_buf, size_t out_buf_size)
{
	return scnprintf(out_buf, out_buf_size, "sec:usec level except cpu  msg\n");
}

static struct debug_view debug_log_view = {
	"pci_msg_log",
	&debug_prolog_header,
	&debug_log_header_fn,
	&debug_sprintf_format_fn,
	NULL,
	NULL
};

/**
 * zpci_report_status - Report the status of operations on a PCI device
 * @zdev:	The PCI device for which to report status
 * @operation:	A string representing the operation reported
 * @status:	A string representing the status of the operation
 *
 * This function creates a human readable report about an operation such as
 * PCI device recovery and forwards this to the platform using the SCLP Write
 * Event Data mechanism. Besides the operation and status strings the report
 * also contains additional information about the device deemed useful for
 * debug such as the currently bound device driver, if any, and error state.
 * Additionally a string representation of pci_debug_msg_id, or as much as fits,
 * is also included.
 *
 * Return: 0 on success an error code < 0 otherwise.
 */
int zpci_report_status(struct zpci_dev *zdev, const char *operation, const char *status)
{
	struct zpci_report_error *report;
	struct pci_driver *driver = NULL;
	struct pci_dev *pdev = NULL;
	char *buf, *end;
	int ret;

	if (!zdev || !zdev->zbus)
		return -ENODEV;

	/* Protected virtualization hosts get nothing from us */
	if (prot_virt_guest)
		return -ENODATA;

	report = (void *)get_zeroed_page(GFP_KERNEL);
	if (!report)
		return -ENOMEM;
	if (zdev->zbus->bus)
		pdev = pci_get_slot(zdev->zbus->bus, zdev->devfn);
	if (pdev)
		driver = to_pci_driver(pdev->dev.driver);

	buf = report->data.log_data;
	end = report->data.log_data + ZPCI_REPORT_DATA_SIZE;
	buf += scnprintf(buf, end - buf, "report: %s\n", operation);
	buf += scnprintf(buf, end - buf, "status: %s\n", status);
	buf += scnprintf(buf, end - buf, "state: %s\n",
			 (pdev) ? zpci_state_str(pdev->error_state) : "n/a");
	buf += scnprintf(buf, end - buf, "driver: %s\n", (driver) ? driver->name : "n/a");
	ret = debug_dump(pci_debug_msg_id, &debug_log_view, buf, end - buf, true);
	if (ret < 0)
		pr_err("Reading PCI debug messages failed with code %d\n", ret);
	else
		buf += ret;

	report->header.version = 1;
	report->header.action = SCLP_ERRNOTIFY_AQ_INFO_LOG;
	report->header.length = buf - (char *)&report->data;
	report->data.timestamp = ktime_get_clocktai_seconds();
	report->data.err_log_id = ZPCI_ERR_LOG_ID_KERNEL_REPORT;

	ret = sclp_pci_report(&report->header, zdev->fh, zdev->fid);
	if (ret)
		pr_err("Reporting PCI status failed with code %d\n", ret);
	else
		pr_info("Reported PCI device status\n");

	free_page((unsigned long)report);

	return ret;
}
