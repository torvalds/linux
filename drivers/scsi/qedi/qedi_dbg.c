/*
 * QLogic iSCSI Offload Driver
 * Copyright (c) 2016 Cavium Inc.
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#include "qedi_dbg.h"
#include <linux/vmalloc.h>

void
qedi_dbg_err(struct qedi_dbg_ctx *qedi, const char *func, u32 line,
	     const char *fmt, ...)
{
	va_list va;
	struct va_format vaf;
	char nfunc[32];

	memset(nfunc, 0, sizeof(nfunc));
	memcpy(nfunc, func, sizeof(nfunc) - 1);

	va_start(va, fmt);

	vaf.fmt = fmt;
	vaf.va = &va;

	if (likely(qedi) && likely(qedi->pdev))
		pr_err("[%s]:[%s:%d]:%d: %pV", dev_name(&qedi->pdev->dev),
		       nfunc, line, qedi->host_no, &vaf);
	else
		pr_err("[0000:00:00.0]:[%s:%d]: %pV", nfunc, line, &vaf);

	va_end(va);
}

void
qedi_dbg_warn(struct qedi_dbg_ctx *qedi, const char *func, u32 line,
	      const char *fmt, ...)
{
	va_list va;
	struct va_format vaf;
	char nfunc[32];

	memset(nfunc, 0, sizeof(nfunc));
	memcpy(nfunc, func, sizeof(nfunc) - 1);

	va_start(va, fmt);

	vaf.fmt = fmt;
	vaf.va = &va;

	if (!(qedi_dbg_log & QEDI_LOG_WARN))
		goto ret;

	if (likely(qedi) && likely(qedi->pdev))
		pr_warn("[%s]:[%s:%d]:%d: %pV", dev_name(&qedi->pdev->dev),
			nfunc, line, qedi->host_no, &vaf);
	else
		pr_warn("[0000:00:00.0]:[%s:%d]: %pV", nfunc, line, &vaf);

ret:
	va_end(va);
}

void
qedi_dbg_notice(struct qedi_dbg_ctx *qedi, const char *func, u32 line,
		const char *fmt, ...)
{
	va_list va;
	struct va_format vaf;
	char nfunc[32];

	memset(nfunc, 0, sizeof(nfunc));
	memcpy(nfunc, func, sizeof(nfunc) - 1);

	va_start(va, fmt);

	vaf.fmt = fmt;
	vaf.va = &va;

	if (!(qedi_dbg_log & QEDI_LOG_NOTICE))
		goto ret;

	if (likely(qedi) && likely(qedi->pdev))
		pr_notice("[%s]:[%s:%d]:%d: %pV",
			  dev_name(&qedi->pdev->dev), nfunc, line,
			  qedi->host_no, &vaf);
	else
		pr_notice("[0000:00:00.0]:[%s:%d]: %pV", nfunc, line, &vaf);

ret:
	va_end(va);
}

void
qedi_dbg_info(struct qedi_dbg_ctx *qedi, const char *func, u32 line,
	      u32 level, const char *fmt, ...)
{
	va_list va;
	struct va_format vaf;
	char nfunc[32];

	memset(nfunc, 0, sizeof(nfunc));
	memcpy(nfunc, func, sizeof(nfunc) - 1);

	va_start(va, fmt);

	vaf.fmt = fmt;
	vaf.va = &va;

	if (!(qedi_dbg_log & level))
		goto ret;

	if (likely(qedi) && likely(qedi->pdev))
		pr_info("[%s]:[%s:%d]:%d: %pV", dev_name(&qedi->pdev->dev),
			nfunc, line, qedi->host_no, &vaf);
	else
		pr_info("[0000:00:00.0]:[%s:%d]: %pV", nfunc, line, &vaf);

ret:
	va_end(va);
}

int
qedi_create_sysfs_attr(struct Scsi_Host *shost, struct sysfs_bin_attrs *iter)
{
	int ret = 0;

	for (; iter->name; iter++) {
		ret = sysfs_create_bin_file(&shost->shost_gendev.kobj,
					    iter->attr);
		if (ret)
			pr_err("Unable to create sysfs %s attr, err(%d).\n",
			       iter->name, ret);
	}
	return ret;
}

void
qedi_remove_sysfs_attr(struct Scsi_Host *shost, struct sysfs_bin_attrs *iter)
{
	for (; iter->name; iter++)
		sysfs_remove_bin_file(&shost->shost_gendev.kobj, iter->attr);
}
