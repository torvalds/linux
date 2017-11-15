/*
 * AMD Platform Security Processor (PSP) interface driver
 *
 * Copyright (C) 2017 Advanced Micro Devices, Inc.
 *
 * Author: Brijesh Singh <brijesh.singh@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __PSP_DEV_H__
#define __PSP_DEV_H__

#include <linux/device.h>
#include <linux/pci.h>
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

#include "sp-dev.h"

#define PSP_P2CMSG(_num)		((_num) << 2)
#define PSP_CMD_COMPLETE_REG		1
#define PSP_CMD_COMPLETE		PSP_P2CMSG(PSP_CMD_COMPLETE_REG)

#define PSP_P2CMSG_INTEN		0x0090
#define PSP_P2CMSG_INTSTS		0x0094

#define PSP_CMDRESP_CMD_SHIFT		16
#define PSP_CMDRESP_IOC			BIT(0)
#define PSP_CMDRESP_RESP		BIT(31)
#define PSP_CMDRESP_ERR_MASK		0xffff

#define MAX_PSP_NAME_LEN		16

struct psp_device {
	struct list_head entry;

	struct psp_vdata *vdata;
	char name[MAX_PSP_NAME_LEN];

	struct device *dev;
	struct sp_device *sp;

	void __iomem *io_regs;
	void __iomem *int_enable_reg;
	void __iomem *int_status_reg;

	void *tee_data;
};

#endif /* __PSP_DEV_H */
