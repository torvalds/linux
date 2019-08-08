// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2005 IBM Corporation
 *
 * Authors:
 *	Seiji Munetoh <munetoh@jp.ibm.com>
 *	Stefan Berger <stefanb@us.ibm.com>
 *	Reiner Sailer <sailer@watson.ibm.com>
 *	Kylene Hall <kjhall@us.ibm.com>
 *	Nayna Jain <nayna@linux.vnet.ibm.com>
 *
 * Maintained by: <tpmdd-devel@lists.sourceforge.net>
 *
 * Access to the event log extended by the TCG BIOS of PC platform
 */

#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/security.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/tpm_eventlog.h>

#include "../tpm.h"
#include "common.h"

struct acpi_tcpa {
	struct acpi_table_header hdr;
	u16 platform_class;
	union {
		struct client_hdr {
			u32 log_max_len __packed;
			u64 log_start_addr __packed;
		} client;
		struct server_hdr {
			u16 reserved;
			u64 log_max_len __packed;
			u64 log_start_addr __packed;
		} server;
	};
};

/* read binary bios log */
int tpm_read_log_acpi(struct tpm_chip *chip)
{
	struct acpi_tcpa *buff;
	acpi_status status;
	void __iomem *virt;
	u64 len, start;
	struct tpm_bios_log *log;

	if (chip->flags & TPM_CHIP_FLAG_TPM2)
		return -ENODEV;

	log = &chip->log;

	/* Unfortuntely ACPI does not associate the event log with a specific
	 * TPM, like PPI. Thus all ACPI TPMs will read the same log.
	 */
	if (!chip->acpi_dev_handle)
		return -ENODEV;

	/* Find TCPA entry in RSDT (ACPI_LOGICAL_ADDRESSING) */
	status = acpi_get_table(ACPI_SIG_TCPA, 1,
				(struct acpi_table_header **)&buff);

	if (ACPI_FAILURE(status))
		return -ENODEV;

	switch(buff->platform_class) {
	case BIOS_SERVER:
		len = buff->server.log_max_len;
		start = buff->server.log_start_addr;
		break;
	case BIOS_CLIENT:
	default:
		len = buff->client.log_max_len;
		start = buff->client.log_start_addr;
		break;
	}
	if (!len) {
		dev_warn(&chip->dev, "%s: TCPA log area empty\n", __func__);
		return -EIO;
	}

	/* malloc EventLog space */
	log->bios_event_log = kmalloc(len, GFP_KERNEL);
	if (!log->bios_event_log)
		return -ENOMEM;

	log->bios_event_log_end = log->bios_event_log + len;

	virt = acpi_os_map_iomem(start, len);
	if (!virt)
		goto err;

	memcpy_fromio(log->bios_event_log, virt, len);

	acpi_os_unmap_iomem(virt, len);
	return EFI_TCG2_EVENT_LOG_FORMAT_TCG_1_2;

err:
	kfree(log->bios_event_log);
	log->bios_event_log = NULL;
	return -EIO;

}
