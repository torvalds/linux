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
