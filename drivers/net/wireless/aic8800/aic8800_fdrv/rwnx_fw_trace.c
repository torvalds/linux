/**
 ******************************************************************************
 *
 * @file rwnx_fw_trace.c
 *
 * Copyright (C) RivieraWaves 2017-2019
 *
 ******************************************************************************
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include "rwnx_fw_trace.h"

int rwnx_fw_log_init(struct rwnx_fw_log *fw_log)
{
	u8 *buf = kmalloc(FW_LOG_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	fw_log->buf.data = buf;
	fw_log->buf.start = fw_log->buf.data;
	fw_log->buf.size  = 0;
	fw_log->buf.end   = fw_log->buf.data;
	fw_log->buf.dataend = fw_log->buf.data + FW_LOG_SIZE;
	spin_lock_init(&fw_log->lock);

	printk("fw_log_init: %lx, %lx\n", (unsigned long)fw_log->buf.start, (unsigned long)(fw_log->buf.dataend));
	return 0;
}

void rwnx_fw_log_deinit(struct rwnx_fw_log *fw_log)
{
	if (!fw_log)
		return;

	if (fw_log->buf.data)
		kfree(fw_log->buf.data);
	fw_log->buf.start = NULL;
	fw_log->buf.end   = NULL;
	fw_log->buf.size = 0;
}

