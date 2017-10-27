/*
 *  QLogic FCoE Offload Driver
 *  Copyright (c) 2016-2017 Cavium Inc.
 *
 *  This software is available under the terms of the GNU General Public License
 *  (GPL) Version 2, available from the file COPYING in the main directory of
 *  this source tree.
 */
#ifndef _QEDF_DBG_H_
#define _QEDF_DBG_H_

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <scsi/scsi_transport.h>
#include <linux/fs.h>

#include <linux/qed/common_hsi.h>
#include <linux/qed/qed_if.h>

extern uint qedf_debug;

/* Debug print level definitions */
#define QEDF_LOG_DEFAULT	0x1		/* Set default logging mask */
#define QEDF_LOG_INFO		0x2		/*
						 * Informational logs,
						 * MAC address, WWPN, WWNN
						 */
#define QEDF_LOG_DISC		0x4		/* Init, discovery, rport */
#define QEDF_LOG_LL2		0x8		/* LL2, VLAN logs */
#define QEDF_LOG_CONN		0x10		/* Connection setup, cleanup */
#define QEDF_LOG_EVT		0x20		/* Events, link, mtu */
#define QEDF_LOG_TIMER		0x40		/* Timer events */
#define QEDF_LOG_MP_REQ	0x80		/* Middle Path (MP) logs */
#define QEDF_LOG_SCSI_TM	0x100		/* SCSI Aborts, Task Mgmt */
#define QEDF_LOG_UNSOL		0x200		/* unsolicited event logs */
#define QEDF_LOG_IO		0x400		/* scsi cmd, completion */
#define QEDF_LOG_MQ		0x800		/* Multi Queue logs */
#define QEDF_LOG_BSG		0x1000		/* BSG logs */
#define QEDF_LOG_DEBUGFS	0x2000		/* debugFS logs */
#define QEDF_LOG_LPORT		0x4000		/* lport logs */
#define QEDF_LOG_ELS		0x8000		/* ELS logs */
#define QEDF_LOG_NPIV		0x10000		/* NPIV logs */
#define QEDF_LOG_SESS		0x20000		/* Conection setup, cleanup */
#define QEDF_LOG_TID		0x80000         /*
						 * FW TID context acquire
						 * free
						 */
#define QEDF_TRACK_TID		0x100000        /*
						 * Track TID state. To be
						 * enabled only at module load
						 * and not run-time.
						 */
#define QEDF_TRACK_CMD_LIST    0x300000        /*
						* Track active cmd list nodes,
						* done with reference to TID,
						* hence TRACK_TID also enabled.
						*/
#define QEDF_LOG_NOTICE	0x40000000	/* Notice logs */
#define QEDF_LOG_WARN		0x80000000	/* Warning logs */

/* Debug context structure */
struct qedf_dbg_ctx {
	unsigned int host_no;
	struct pci_dev *pdev;
#ifdef CONFIG_DEBUG_FS
	struct dentry *bdf_dentry;
#endif
};

#define QEDF_ERR(pdev, fmt, ...)	\
		qedf_dbg_err(pdev, __func__, __LINE__, fmt, ## __VA_ARGS__)
#define QEDF_WARN(pdev, fmt, ...)	\
		qedf_dbg_warn(pdev, __func__, __LINE__, fmt, ## __VA_ARGS__)
#define QEDF_NOTICE(pdev, fmt, ...)	\
		qedf_dbg_notice(pdev, __func__, __LINE__, fmt, ## __VA_ARGS__)
#define QEDF_INFO(pdev, level, fmt, ...)	\
		qedf_dbg_info(pdev, __func__, __LINE__, level, fmt,	\
			      ## __VA_ARGS__)
__printf(4, 5)
void qedf_dbg_err(struct qedf_dbg_ctx *qedf, const char *func, u32 line,
			  const char *fmt, ...);
__printf(4, 5)
void qedf_dbg_warn(struct qedf_dbg_ctx *qedf, const char *func, u32 line,
			   const char *, ...);
__printf(4, 5)
void qedf_dbg_notice(struct qedf_dbg_ctx *qedf, const char *func,
			    u32 line, const char *, ...);
__printf(5, 6)
void qedf_dbg_info(struct qedf_dbg_ctx *qedf, const char *func, u32 line,
			  u32 info, const char *fmt, ...);

/* GRC Dump related defines */

struct Scsi_Host;

#define QEDF_UEVENT_CODE_GRCDUMP 0

struct sysfs_bin_attrs {
	char *name;
	struct bin_attribute *attr;
};

extern int qedf_alloc_grc_dump_buf(uint8_t **buf, uint32_t len);
extern void qedf_free_grc_dump_buf(uint8_t **buf);
extern int qedf_get_grc_dump(struct qed_dev *cdev,
			     const struct qed_common_ops *common, uint8_t **buf,
			     uint32_t *grcsize);
extern void qedf_uevent_emit(struct Scsi_Host *shost, u32 code, char *msg);
extern int qedf_create_sysfs_attr(struct Scsi_Host *shost,
				   struct sysfs_bin_attrs *iter);
extern void qedf_remove_sysfs_attr(struct Scsi_Host *shost,
				    struct sysfs_bin_attrs *iter);

#ifdef CONFIG_DEBUG_FS
/* DebugFS related code */
struct qedf_list_of_funcs {
	char *oper_str;
	ssize_t (*oper_func)(struct qedf_dbg_ctx *qedf);
};

struct qedf_debugfs_ops {
	char *name;
	struct qedf_list_of_funcs *qedf_funcs;
};

#define qedf_dbg_fileops(drv, ops) \
{ \
	.owner  = THIS_MODULE, \
	.open   = simple_open, \
	.read   = drv##_dbg_##ops##_cmd_read, \
	.write  = drv##_dbg_##ops##_cmd_write \
}

/* Used for debugfs sequential files */
#define qedf_dbg_fileops_seq(drv, ops) \
{ \
	.owner = THIS_MODULE, \
	.open = drv##_dbg_##ops##_open, \
	.read = seq_read, \
	.llseek = seq_lseek, \
	.release = single_release, \
}

extern void qedf_dbg_host_init(struct qedf_dbg_ctx *qedf,
				struct qedf_debugfs_ops *dops,
				struct file_operations *fops);
extern void qedf_dbg_host_exit(struct qedf_dbg_ctx *qedf);
extern void qedf_dbg_init(char *drv_name);
extern void qedf_dbg_exit(void);
#endif /* CONFIG_DEBUG_FS */

#endif /* _QEDF_DBG_H_ */
