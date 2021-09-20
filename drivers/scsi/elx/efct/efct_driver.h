/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#if !defined(__EFCT_DRIVER_H__)
#define __EFCT_DRIVER_H__

/***************************************************************************
 * OS specific includes
 */
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/firmware.h>
#include "../include/efc_common.h"
#include "../libefc/efclib.h"
#include "efct_hw.h"
#include "efct_io.h"
#include "efct_xport.h"

#define EFCT_DRIVER_NAME			"efct"
#define EFCT_DRIVER_VERSION			"1.0.0.0"

/* EFCT_DEFAULT_FILTER-
 * MRQ filter to segregate the IO flow.
 */
#define EFCT_DEFAULT_FILTER			"0x01ff22ff,0,0,0"

/* EFCT_OS_MAX_ISR_TIME_MSEC -
 * maximum time driver code should spend in an interrupt
 * or kernel thread context without yielding
 */
#define EFCT_OS_MAX_ISR_TIME_MSEC		1000

#define EFCT_FC_MAX_SGL				64
#define EFCT_FC_DIF_SEED			0

/* Watermark */
#define EFCT_WATERMARK_HIGH_PCT			90
#define EFCT_WATERMARK_LOW_PCT			80
#define EFCT_IO_WATERMARK_PER_INITIATOR		8

#define EFCT_PCI_MAX_REGS			6
#define MAX_PCI_INTERRUPTS			16

struct efct_intr_context {
	struct efct		*efct;
	u32			index;
};

struct efct {
	struct pci_dev			*pci;
	void __iomem			*reg[EFCT_PCI_MAX_REGS];

	u32				n_msix_vec;
	bool				attached;
	bool				soft_wwn_enable;
	u8				efct_req_fw_upgrade;
	struct efct_intr_context	intr_context[MAX_PCI_INTERRUPTS];
	u32				numa_node;

	char				name[EFC_NAME_LENGTH];
	u32				instance_index;
	struct list_head		list_entry;
	struct efct_scsi_tgt		tgt_efct;
	struct efct_xport		*xport;
	struct efc			*efcport;
	struct Scsi_Host		*shost;
	int				logmask;
	u32				max_isr_time_msec;

	const char			*desc;

	const char			*model;

	struct efct_hw			hw;

	u32				rq_selection_policy;
	char				*filter_def;
	int				topology;

	/* Look up for target node */
	struct xarray			lookup;

	/*
	 * Target IO timer value:
	 * Zero: target command timeout disabled.
	 * Non-zero: Timeout value, in seconds, for target commands
	 */
	u32				target_io_timer_sec;

	int				speed;
	struct dentry			*sess_debugfs_dir;
};

#define FW_WRITE_BUFSIZE		(64 * 1024)

struct efct_fw_write_result {
	struct completion done;
	int status;
	u32 actual_xfer;
	u32 change_status;
};

extern struct list_head			efct_devices;

#endif /* __EFCT_DRIVER_H__ */
