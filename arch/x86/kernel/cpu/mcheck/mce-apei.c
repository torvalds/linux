/*
 * Bridge between MCE and APEI
 *
 * On some machine, corrected memory errors are reported via APEI
 * generic hardware error source (GHES) instead of corrected Machine
 * Check. These corrected memory errors can be reported to user space
 * through /dev/mcelog via faking a corrected Machine Check, so that
 * the error memory page can be offlined by /sbin/mcelog if the error
 * count for one page is beyond the threshold.
 *
 * For fatal MCE, save MCE record into persistent storage via ERST, so
 * that the MCE record can be logged after reboot via ERST.
 *
 * Copyright 2010 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/cper.h>
#include <acpi/apei.h>
#include <asm/mce.h>

#include "mce-internal.h"

void apei_mce_report_mem_error(int corrected, struct cper_sec_mem_err *mem_err)
{
	struct mce m;

	/* Only corrected MC is reported */
	if (!corrected || !(mem_err->validation_bits & CPER_MEM_VALID_PA))
		return;

	mce_setup(&m);
	m.bank = 1;
	/* Fake a memory read corrected error with unknown channel */
	m.status = MCI_STATUS_VAL | MCI_STATUS_EN | MCI_STATUS_ADDRV | 0x9f;
	m.addr = mem_err->physical_addr;
	mce_log(&m);
	mce_notify_irq();
}
EXPORT_SYMBOL_GPL(apei_mce_report_mem_error);

#define CPER_CREATOR_MCE						\
	UUID_LE(0x75a574e3, 0x5052, 0x4b29, 0x8a, 0x8e, 0xbe, 0x2c,	\
		0x64, 0x90, 0xb8, 0x9d)
#define CPER_SECTION_TYPE_MCE						\
	UUID_LE(0xfe08ffbe, 0x95e4, 0x4be7, 0xbc, 0x73, 0x40, 0x96,	\
		0x04, 0x4a, 0x38, 0xfc)

/*
 * CPER specification (in UEFI specification 2.3 appendix N) requires
 * byte-packed.
 */
struct cper_mce_record {
	struct cper_record_header hdr;
	struct cper_section_descriptor sec_hdr;
	struct mce mce;
} __packed;

int apei_write_mce(struct mce *m)
{
	struct cper_mce_record rcd;

	memset(&rcd, 0, sizeof(rcd));
	memcpy(rcd.hdr.signature, CPER_SIG_RECORD, CPER_SIG_SIZE);
	rcd.hdr.revision = CPER_RECORD_REV;
	rcd.hdr.signature_end = CPER_SIG_END;
	rcd.hdr.section_count = 1;
	rcd.hdr.error_severity = CPER_SEV_FATAL;
	/* timestamp, platform_id, partition_id are all invalid */
	rcd.hdr.validation_bits = 0;
	rcd.hdr.record_length = sizeof(rcd);
	rcd.hdr.creator_id = CPER_CREATOR_MCE;
	rcd.hdr.notification_type = CPER_NOTIFY_MCE;
	rcd.hdr.record_id = cper_next_record_id();
	rcd.hdr.flags = CPER_HW_ERROR_FLAGS_PREVERR;

	rcd.sec_hdr.section_offset = (void *)&rcd.mce - (void *)&rcd;
	rcd.sec_hdr.section_length = sizeof(rcd.mce);
	rcd.sec_hdr.revision = CPER_SEC_REV;
	/* fru_id and fru_text is invalid */
	rcd.sec_hdr.validation_bits = 0;
	rcd.sec_hdr.flags = CPER_SEC_PRIMARY;
	rcd.sec_hdr.section_type = CPER_SECTION_TYPE_MCE;
	rcd.sec_hdr.section_severity = CPER_SEV_FATAL;

	memcpy(&rcd.mce, m, sizeof(*m));

	return erst_write(&rcd.hdr);
}

ssize_t apei_read_mce(struct mce *m, u64 *record_id)
{
	struct cper_mce_record rcd;
	int rc, pos;

	rc = erst_get_record_id_begin(&pos);
	if (rc)
		return rc;
retry:
	rc = erst_get_record_id_next(&pos, record_id);
	if (rc)
		goto out;
	/* no more record */
	if (*record_id == APEI_ERST_INVALID_RECORD_ID)
		goto out;
	rc = erst_read(*record_id, &rcd.hdr, sizeof(rcd));
	/* someone else has cleared the record, try next one */
	if (rc == -ENOENT)
		goto retry;
	else if (rc < 0)
		goto out;
	/* try to skip other type records in storage */
	else if (rc != sizeof(rcd) ||
		 uuid_le_cmp(rcd.hdr.creator_id, CPER_CREATOR_MCE))
		goto retry;
	memcpy(m, &rcd.mce, sizeof(*m));
	rc = sizeof(*m);
out:
	erst_get_record_id_end();

	return rc;
}

/* Check whether there is record in ERST */
int apei_check_mce(void)
{
	return erst_get_record_count();
}

int apei_clear_mce(u64 record_id)
{
	return erst_clear(record_id);
}
