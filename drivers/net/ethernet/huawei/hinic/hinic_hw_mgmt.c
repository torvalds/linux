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

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>

#include "hinic_hw_if.h"
#include "hinic_hw_eqs.h"
#include "hinic_hw_mgmt.h"
#include "hinic_hw_dev.h"

#define mgmt_to_pfhwdev(pf_mgmt)        \
		container_of(pf_mgmt, struct hinic_pfhwdev, pf_to_mgmt)

/**
 * hinic_msg_to_mgmt - send message to mgmt
 * @pf_to_mgmt: PF to MGMT channel
 * @mod: module in the chip that will get the message
 * @cmd: command of the message
 * @buf_in: the msg data
 * @in_size: the msg data length
 * @buf_out: response
 * @out_size: returned response length
 * @sync: sync msg or async msg
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_msg_to_mgmt(struct hinic_pf_to_mgmt *pf_to_mgmt,
		      enum hinic_mod_type mod, u8 cmd,
		      void *buf_in, u16 in_size, void *buf_out, u16 *out_size,
		      enum hinic_mgmt_msg_type sync)
{
	/* should be implemented */
	return -EINVAL;
}

/**
 * mgmt_msg_aeqe_handler - handler for a mgmt message event
 * @handle: PF to MGMT channel
 * @data: the header of the message
 * @size: unused
 **/
static void mgmt_msg_aeqe_handler(void *handle, void *data, u8 size)
{
	/* should be implemented */
}

/**
 * hinic_pf_to_mgmt_init - initialize PF to MGMT channel
 * @pf_to_mgmt: PF to MGMT channel
 * @hwif: HW interface the PF to MGMT will use for accessing HW
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_pf_to_mgmt_init(struct hinic_pf_to_mgmt *pf_to_mgmt,
			  struct hinic_hwif *hwif)
{
	struct hinic_pfhwdev *pfhwdev = mgmt_to_pfhwdev(pf_to_mgmt);
	struct hinic_hwdev *hwdev = &pfhwdev->hwdev;

	pf_to_mgmt->hwif = hwif;

	hinic_aeq_register_hw_cb(&hwdev->aeqs, HINIC_MSG_FROM_MGMT_CPU,
				 pf_to_mgmt,
				 mgmt_msg_aeqe_handler);
	return 0;
}

/**
 * hinic_pf_to_mgmt_free - free PF to MGMT channel
 * @pf_to_mgmt: PF to MGMT channel
 **/
void hinic_pf_to_mgmt_free(struct hinic_pf_to_mgmt *pf_to_mgmt)
{
	struct hinic_pfhwdev *pfhwdev = mgmt_to_pfhwdev(pf_to_mgmt);
	struct hinic_hwdev *hwdev = &pfhwdev->hwdev;

	hinic_aeq_unregister_hw_cb(&hwdev->aeqs, HINIC_MSG_FROM_MGMT_CPU);
}
