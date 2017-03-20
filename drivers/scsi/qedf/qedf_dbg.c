/*
 *  QLogic FCoE Offload Driver
 *  Copyright (c) 2016 Cavium Inc.
 *
 *  This software is available under the terms of the GNU General Public License
 *  (GPL) Version 2, available from the file COPYING in the main directory of
 *  this source tree.
 */
#include "qedf_dbg.h"
#include <linux/vmalloc.h>

void
qedf_dbg_err(struct qedf_dbg_ctx *qedf, const char *func, u32 line,
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

	if (likely(qedf) && likely(qedf->pdev))
		pr_err("[%s]:[%s:%d]:%d: %pV", dev_name(&(qedf->pdev->dev)),
			nfunc, line, qedf->host_no, &vaf);
	else
		pr_err("[0000:00:00.0]:[%s:%d]: %pV", nfunc, line, &vaf);

	va_end(va);
}

void
qedf_dbg_warn(struct qedf_dbg_ctx *qedf, const char *func, u32 line,
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

	if (!(qedf_debug & QEDF_LOG_WARN))
		goto ret;

	if (likely(qedf) && likely(qedf->pdev))
		pr_warn("[%s]:[%s:%d]:%d: %pV", dev_name(&(qedf->pdev->dev)),
			nfunc, line, qedf->host_no, &vaf);
	else
		pr_warn("[0000:00:00.0]:[%s:%d]: %pV", nfunc, line, &vaf);

ret:
	va_end(va);
}

void
qedf_dbg_notice(struct qedf_dbg_ctx *qedf, const char *func, u32 line,
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

	if (!(qedf_debug & QEDF_LOG_NOTICE))
		goto ret;

	if (likely(qedf) && likely(qedf->pdev))
		pr_notice("[%s]:[%s:%d]:%d: %pV",
			  dev_name(&(qedf->pdev->dev)), nfunc, line,
			  qedf->host_no, &vaf);
	else
		pr_notice("[0000:00:00.0]:[%s:%d]: %pV", nfunc, line, &vaf);

ret:
	va_end(va);
}

void
qedf_dbg_info(struct qedf_dbg_ctx *qedf, const char *func, u32 line,
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

	if (!(qedf_debug & level))
		goto ret;

	if (likely(qedf) && likely(qedf->pdev))
		pr_info("[%s]:[%s:%d]:%d: %pV", dev_name(&(qedf->pdev->dev)),
			nfunc, line, qedf->host_no, &vaf);
	else
		pr_info("[0000:00:00.0]:[%s:%d]: %pV", nfunc, line, &vaf);

ret:
	va_end(va);
}

int
qedf_alloc_grc_dump_buf(u8 **buf, uint32_t len)
{
		*buf = vmalloc(len);
		if (!(*buf))
			return -ENOMEM;

		memset(*buf, 0, len);
		return 0;
}

void
qedf_free_grc_dump_buf(uint8_t **buf)
{
		vfree(*buf);
		*buf = NULL;
}

int
qedf_get_grc_dump(struct qed_dev *cdev, const struct qed_common_ops *common,
		   u8 **buf, uint32_t *grcsize)
{
	if (!*buf)
		return -EINVAL;

	return common->dbg_grc(cdev, *buf, grcsize);
}

void
qedf_uevent_emit(struct Scsi_Host *shost, u32 code, char *msg)
{
	char event_string[40];
	char *envp[] = {event_string, NULL};

	memset(event_string, 0, sizeof(event_string));
	switch (code) {
	case QEDF_UEVENT_CODE_GRCDUMP:
		if (msg)
			strncpy(event_string, msg, strlen(msg));
		else
			sprintf(event_string, "GRCDUMP=%u", shost->host_no);
		break;
	default:
		/* do nothing */
		break;
	}

	kobject_uevent_env(&shost->shost_gendev.kobj, KOBJ_CHANGE, envp);
}

int
qedf_create_sysfs_attr(struct Scsi_Host *shost, struct sysfs_bin_attrs *iter)
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
qedf_remove_sysfs_attr(struct Scsi_Host *shost, struct sysfs_bin_attrs *iter)
{
	for (; iter->name; iter++)
		sysfs_remove_bin_file(&shost->shost_gendev.kobj, iter->attr);
}
