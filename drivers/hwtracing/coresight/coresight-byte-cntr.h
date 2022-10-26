/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _CORESIGHT_BYTE_CNTR_H
#define _CORESIGHT_BYTE_CNTR_H
#include <linux/cdev.h>
#include <linux/amba/bus.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include "coresight-tmc.h"

struct byte_cntr {
	struct cdev		dev;
	struct class		*driver_class;
	bool			enable;
	bool			read_active;
	uint32_t		byte_cntr_value;
	uint32_t		block_size;
	int			byte_cntr_irq;
	atomic_t		irq_cnt;
	atomic_t	usb_free_buf;
	wait_queue_head_t	wq;
	wait_queue_head_t	usb_wait_wq;
	struct workqueue_struct	*usb_wq;
	struct qdss_request	*usb_req;
	struct work_struct	read_work;
	struct mutex		usb_bypass_lock;
	struct mutex		byte_cntr_lock;
	struct coresight_csr		*csr;
	struct tmc_drvdata	*tmcdrvdata;
	const char		*name;
	const char		*class_name;
	int			irqctrl_offset;
	unsigned long	offset;
	unsigned long	rwp_offset;
	uint64_t		total_size;
	uint64_t		total_irq;

};

extern void tmc_etr_byte_cntr_start(struct byte_cntr *byte_cntr_data);
extern void tmc_etr_byte_cntr_stop(struct byte_cntr *byte_cntr_data);

#endif
