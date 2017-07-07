/*
 * QLogic iSCSI Offload Driver
 * Copyright (c) 2016 Cavium Inc.
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef _QEDI_DBG_H_
#define _QEDI_DBG_H_

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_iscsi.h>
#include <linux/fs.h>

#define __PREVENT_QED_HSI__
#include <linux/qed/common_hsi.h>
#include <linux/qed/qed_if.h>

extern uint qedi_dbg_log;

/* Debug print level definitions */
#define QEDI_LOG_DEFAULT	0x1		/* Set default logging mask */
#define QEDI_LOG_INFO		0x2		/* Informational logs,
						 * MAC address, WWPN, WWNN
						 */
#define QEDI_LOG_DISC		0x4		/* Init, discovery, rport */
#define QEDI_LOG_LL2		0x8		/* LL2, VLAN logs */
#define QEDI_LOG_CONN		0x10		/* Connection setup, cleanup */
#define QEDI_LOG_EVT		0x20		/* Events, link, mtu */
#define QEDI_LOG_TIMER		0x40		/* Timer events */
#define QEDI_LOG_MP_REQ		0x80		/* Middle Path (MP) logs */
#define QEDI_LOG_SCSI_TM	0x100		/* SCSI Aborts, Task Mgmt */
#define QEDI_LOG_UNSOL		0x200		/* unsolicited event logs */
#define QEDI_LOG_IO		0x400		/* scsi cmd, completion */
#define QEDI_LOG_MQ		0x800		/* Multi Queue logs */
#define QEDI_LOG_BSG		0x1000		/* BSG logs */
#define QEDI_LOG_DEBUGFS	0x2000		/* debugFS logs */
#define QEDI_LOG_LPORT		0x4000		/* lport logs */
#define QEDI_LOG_ELS		0x8000		/* ELS logs */
#define QEDI_LOG_NPIV		0x10000		/* NPIV logs */
#define QEDI_LOG_SESS		0x20000		/* Conection setup, cleanup */
#define QEDI_LOG_UIO		0x40000		/* iSCSI UIO logs */
#define QEDI_LOG_TID		0x80000         /* FW TID context acquire,
						 * free
						 */
#define QEDI_TRACK_TID		0x100000        /* Track TID state. To be
						 * enabled only at module load
						 * and not run-time.
						 */
#define QEDI_TRACK_CMD_LIST    0x300000        /* Track active cmd list nodes,
						* done with reference to TID,
						* hence TRACK_TID also enabled.
						*/
#define QEDI_LOG_NOTICE		0x40000000	/* Notice logs */
#define QEDI_LOG_WARN		0x80000000	/* Warning logs */

/* Debug context structure */
struct qedi_dbg_ctx {
	unsigned int host_no;
	struct pci_dev *pdev;
#ifdef CONFIG_DEBUG_FS
	struct dentry *bdf_dentry;
#endif
};

#define QEDI_ERR(pdev, fmt, ...)	\
		qedi_dbg_err(pdev, __func__, __LINE__, fmt, ## __VA_ARGS__)
#define QEDI_WARN(pdev, fmt, ...)	\
		qedi_dbg_warn(pdev, __func__, __LINE__, fmt, ## __VA_ARGS__)
#define QEDI_NOTICE(pdev, fmt, ...)	\
		qedi_dbg_notice(pdev, __func__, __LINE__, fmt, ## __VA_ARGS__)
#define QEDI_INFO(pdev, level, fmt, ...)	\
		qedi_dbg_info(pdev, __func__, __LINE__, level, fmt,	\
			      ## __VA_ARGS__)

void qedi_dbg_err(struct qedi_dbg_ctx *qedi, const char *func, u32 line,
		  const char *fmt, ...);
void qedi_dbg_warn(struct qedi_dbg_ctx *qedi, const char *func, u32 line,
		   const char *fmt, ...);
void qedi_dbg_notice(struct qedi_dbg_ctx *qedi, const char *func, u32 line,
		     const char *fmt, ...);
void qedi_dbg_info(struct qedi_dbg_ctx *qedi, const char *func, u32 line,
		   u32 info, const char *fmt, ...);

struct Scsi_Host;

struct sysfs_bin_attrs {
	char *name;
	struct bin_attribute *attr;
};

int qedi_create_sysfs_attr(struct Scsi_Host *shost,
			   struct sysfs_bin_attrs *iter);
void qedi_remove_sysfs_attr(struct Scsi_Host *shost,
			    struct sysfs_bin_attrs *iter);

#ifdef CONFIG_DEBUG_FS
/* DebugFS related code */
struct qedi_list_of_funcs {
	char *oper_str;
	ssize_t (*oper_func)(struct qedi_dbg_ctx *qedi);
};

struct qedi_debugfs_ops {
	char *name;
	struct qedi_list_of_funcs *qedi_funcs;
};

#define qedi_dbg_fileops(drv, ops) \
{ \
	.owner  = THIS_MODULE, \
	.open   = simple_open, \
	.read   = drv##_dbg_##ops##_cmd_read, \
	.write  = drv##_dbg_##ops##_cmd_write \
}

/* Used for debugfs sequential files */
#define qedi_dbg_fileops_seq(drv, ops) \
{ \
	.owner = THIS_MODULE, \
	.open = drv##_dbg_##ops##_open, \
	.read = seq_read, \
	.llseek = seq_lseek, \
	.release = single_release, \
}

void qedi_dbg_host_init(struct qedi_dbg_ctx *qedi,
			struct qedi_debugfs_ops *dops,
			const struct file_operations *fops);
void qedi_dbg_host_exit(struct qedi_dbg_ctx *qedi);
void qedi_dbg_init(char *drv_name);
void qedi_dbg_exit(void);
#endif /* CONFIG_DEBUG_FS */

#endif /* _QEDI_DBG_H_ */
