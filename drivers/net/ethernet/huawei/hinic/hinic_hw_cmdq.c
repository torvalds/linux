/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#include <linux/types.h>
#include <linux/errno.h>

#include "hinic_hw_if.h"
#include "hinic_hw_cmdq.h"

/**
 * hinic_alloc_cmdq_buf - alloc buffer for sending command
 * @cmdqs: the cmdqs
 * @cmdq_buf: the buffer returned in this struct
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_alloc_cmdq_buf(struct hinic_cmdqs *cmdqs,
			 struct hinic_cmdq_buf *cmdq_buf)
{
	/* should be implemented */
	return -ENOMEM;
}

/**
 * hinic_free_cmdq_buf - free buffer
 * @cmdqs: the cmdqs
 * @cmdq_buf: the buffer to free that is in this struct
 **/
void hinic_free_cmdq_buf(struct hinic_cmdqs *cmdqs,
			 struct hinic_cmdq_buf *cmdq_buf)
{
	/* should be implemented */
}

/**
 * hinic_cmdq_direct_resp - send command with direct data as resp
 * @cmdqs: the cmdqs
 * @mod: module on the card that will handle the command
 * @cmd: the command
 * @buf_in: the buffer for the command
 * @resp: the response to return
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_cmdq_direct_resp(struct hinic_cmdqs *cmdqs,
			   enum hinic_mod_type mod, u8 cmd,
			   struct hinic_cmdq_buf *buf_in, u64 *resp)
{
	/* should be implemented */
	return -EINVAL;
}

/**
 * hinic_init_cmdqs - init all cmdqs
 * @cmdqs: cmdqs to init
 * @hwif: HW interface for accessing cmdqs
 * @db_area: doorbell areas for all the cmdqs
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_init_cmdqs(struct hinic_cmdqs *cmdqs, struct hinic_hwif *hwif,
		     void __iomem **db_area)
{
	/* should be implemented */
	return -EINVAL;
}

/**
 * hinic_free_cmdqs - free all cmdqs
 * @cmdqs: cmdqs to free
 **/
void hinic_free_cmdqs(struct hinic_cmdqs *cmdqs)
{
	/* should be implemented */
}
