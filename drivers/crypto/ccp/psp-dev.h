/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AMD Platform Security Processor (PSP) interface driver
 *
 * Copyright (C) 2017-2018 Advanced Micro Devices, Inc.
 *
 * Author: Brijesh Singh <brijesh.singh@amd.com>
 */

#ifndef __PSP_DEV_H__
#define __PSP_DEV_H__

#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/dmapool.h>
#include <linux/hw_random.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/dmaengine.h>
#include <linux/psp-sev.h>
#include <linux/miscdevice.h>

#include "sp-dev.h"

#define PSP_CMD_COMPLETE		BIT(1)

#define PSP_CMDRESP_CMD_SHIFT		16
#define PSP_CMDRESP_IOC			BIT(0)
#define PSP_CMDRESP_RESP		BIT(31)
#define PSP_CMDRESP_ERR_MASK		0xffff

#define MAX_PSP_NAME_LEN		16

struct sev_misc_dev {
	struct kref refcount;
	struct miscdevice misc;
};

struct psp_device {
	struct list_head entry;

	struct psp_vdata *vdata;
	char name[MAX_PSP_NAME_LEN];

	struct device *dev;
	struct sp_device *sp;

	void __iomem *io_regs;

	int sev_state;
	unsigned int sev_int_rcvd;
	wait_queue_head_t sev_int_queue;
	struct sev_misc_dev *sev_misc;
	struct sev_user_data_status status_cmd_buf;
	struct sev_data_init init_cmd_buf;

	u8 api_major;
	u8 api_minor;
	u8 build;
};

#endif /* __PSP_DEV_H */
