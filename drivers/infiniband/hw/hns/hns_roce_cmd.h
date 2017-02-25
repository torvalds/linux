/*
 * Copyright (c) 2016 Hisilicon Limited.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _HNS_ROCE_CMD_H
#define _HNS_ROCE_CMD_H

#define HNS_ROCE_MAILBOX_SIZE		4096
#define HNS_ROCE_CMD_TIMEOUT_MSECS	10000

enum {
	/* TPT commands */
	HNS_ROCE_CMD_SW2HW_MPT		= 0xd,
	HNS_ROCE_CMD_HW2SW_MPT		= 0xf,

	/* CQ commands */
	HNS_ROCE_CMD_SW2HW_CQ		= 0x16,
	HNS_ROCE_CMD_HW2SW_CQ		= 0x17,

	/* QP/EE commands */
	HNS_ROCE_CMD_RST2INIT_QP	= 0x19,
	HNS_ROCE_CMD_INIT2RTR_QP	= 0x1a,
	HNS_ROCE_CMD_RTR2RTS_QP		= 0x1b,
	HNS_ROCE_CMD_RTS2RTS_QP		= 0x1c,
	HNS_ROCE_CMD_2ERR_QP		= 0x1e,
	HNS_ROCE_CMD_RTS2SQD_QP		= 0x1f,
	HNS_ROCE_CMD_SQD2SQD_QP		= 0x38,
	HNS_ROCE_CMD_SQD2RTS_QP		= 0x20,
	HNS_ROCE_CMD_2RST_QP		= 0x21,
	HNS_ROCE_CMD_QUERY_QP		= 0x22,
};

int hns_roce_cmd_mbox(struct hns_roce_dev *hr_dev, u64 in_param, u64 out_param,
		      unsigned long in_modifier, u8 op_modifier, u16 op,
		      unsigned long timeout);

struct hns_roce_cmd_mailbox
	*hns_roce_alloc_cmd_mailbox(struct hns_roce_dev *hr_dev);
void hns_roce_free_cmd_mailbox(struct hns_roce_dev *hr_dev,
			       struct hns_roce_cmd_mailbox *mailbox);

#endif /* _HNS_ROCE_CMD_H */
