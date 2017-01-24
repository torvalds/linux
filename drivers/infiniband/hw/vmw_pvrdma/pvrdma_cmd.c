/*
 * Copyright (c) 2012-2016 VMware, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of EITHER the GNU General Public License
 * version 2 as published by the Free Software Foundation or the BSD
 * 2-Clause License. This program is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License version 2 for more details at
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program available in the file COPYING in the main
 * directory of this source tree.
 *
 * The BSD 2-Clause License
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/list.h>

#include "pvrdma.h"

#define PVRDMA_CMD_TIMEOUT	10000 /* ms */

static inline int pvrdma_cmd_recv(struct pvrdma_dev *dev,
				  union pvrdma_cmd_resp *resp,
				  unsigned resp_code)
{
	int err;

	dev_dbg(&dev->pdev->dev, "receive response from device\n");

	err = wait_for_completion_interruptible_timeout(&dev->cmd_done,
			msecs_to_jiffies(PVRDMA_CMD_TIMEOUT));
	if (err == 0 || err == -ERESTARTSYS) {
		dev_warn(&dev->pdev->dev,
			 "completion timeout or interrupted\n");
		return -ETIMEDOUT;
	}

	spin_lock(&dev->cmd_lock);
	memcpy(resp, dev->resp_slot, sizeof(*resp));
	spin_unlock(&dev->cmd_lock);

	if (resp->hdr.ack != resp_code) {
		dev_warn(&dev->pdev->dev,
			 "unknown response %#x expected %#x\n",
			 resp->hdr.ack, resp_code);
		return -EFAULT;
	}

	return 0;
}

int
pvrdma_cmd_post(struct pvrdma_dev *dev, union pvrdma_cmd_req *req,
		union pvrdma_cmd_resp *resp, unsigned resp_code)
{
	int err;

	dev_dbg(&dev->pdev->dev, "post request to device\n");

	/* Serializiation */
	down(&dev->cmd_sema);

	BUILD_BUG_ON(sizeof(union pvrdma_cmd_req) !=
		     sizeof(struct pvrdma_cmd_modify_qp));

	spin_lock(&dev->cmd_lock);
	memcpy(dev->cmd_slot, req, sizeof(*req));
	spin_unlock(&dev->cmd_lock);

	init_completion(&dev->cmd_done);
	pvrdma_write_reg(dev, PVRDMA_REG_REQUEST, 0);

	/* Make sure the request is written before reading status. */
	mb();

	err = pvrdma_read_reg(dev, PVRDMA_REG_ERR);
	if (err == 0) {
		if (resp != NULL)
			err = pvrdma_cmd_recv(dev, resp, resp_code);
	} else {
		dev_warn(&dev->pdev->dev,
			 "failed to write request error reg: %d\n", err);
		err = -EFAULT;
	}

	up(&dev->cmd_sema);

	return err;
}
