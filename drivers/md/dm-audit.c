// SPDX-License-Identifier: GPL-2.0-only
/*
 * Creating audit records for mapped devices.
 *
 * Copyright (C) 2021 Fraunhofer AISEC. All rights reserved.
 *
 * Authors: Michael Weiß <michael.weiss@aisec.fraunhofer.de>
 */

#include <linux/audit.h>
#include <linux/module.h>
#include <linux/device-mapper.h>
#include <linux/bio.h>
#include <linux/blkdev.h>

#include "dm-audit.h"
#include "dm-core.h"

static struct audit_buffer *dm_audit_log_start(int audit_type,
					       const char *dm_msg_prefix,
					       const char *op)
{
	struct audit_buffer *ab;

	if (audit_enabled == AUDIT_OFF)
		return NULL;

	ab = audit_log_start(audit_context(), GFP_KERNEL, audit_type);
	if (unlikely(!ab))
		return NULL;

	audit_log_format(ab, "module=%s op=%s", dm_msg_prefix, op);
	return ab;
}

void dm_audit_log_ti(int audit_type, const char *dm_msg_prefix, const char *op,
		     struct dm_target *ti, int result)
{
	struct audit_buffer *ab = NULL;
	struct mapped_device *md = dm_table_get_md(ti->table);
	int dev_major = dm_disk(md)->major;
	int dev_minor = dm_disk(md)->first_minor;

	switch (audit_type) {
	case AUDIT_DM_CTRL:
		ab = dm_audit_log_start(audit_type, dm_msg_prefix, op);
		if (unlikely(!ab))
			return;
		audit_log_task_info(ab);
		audit_log_format(ab, " dev=%d:%d error_msg='%s'", dev_major,
				 dev_minor, !result ? ti->error : "success");
		break;
	case AUDIT_DM_EVENT:
		ab = dm_audit_log_start(audit_type, dm_msg_prefix, op);
		if (unlikely(!ab))
			return;
		audit_log_format(ab, " dev=%d:%d sector=?", dev_major,
				 dev_minor);
		break;
	default: /* unintended use */
		return;
	}

	audit_log_format(ab, " res=%d", result);
	audit_log_end(ab);
}
EXPORT_SYMBOL_GPL(dm_audit_log_ti);

void dm_audit_log_bio(const char *dm_msg_prefix, const char *op,
		      struct bio *bio, sector_t sector, int result)
{
	struct audit_buffer *ab;
	int dev_major = MAJOR(bio->bi_bdev->bd_dev);
	int dev_minor = MINOR(bio->bi_bdev->bd_dev);

	ab = dm_audit_log_start(AUDIT_DM_EVENT, dm_msg_prefix, op);
	if (unlikely(!ab))
		return;

	audit_log_format(ab, " dev=%d:%d sector=%llu res=%d",
			 dev_major, dev_minor, sector, result);
	audit_log_end(ab);
}
EXPORT_SYMBOL_GPL(dm_audit_log_bio);
