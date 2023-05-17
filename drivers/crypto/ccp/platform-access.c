// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Platform Security Processor (PSP) Platform Access interface
 *
 * Copyright (C) 2023 Advanced Micro Devices, Inc.
 *
 * Author: Mario Limonciello <mario.limonciello@amd.com>
 *
 * Some of this code is adapted from drivers/i2c/busses/i2c-designware-amdpsp.c
 * developed by Jan Dabros <jsd@semihalf.com> and Copyright (C) 2022 Google Inc.
 *
 */

#include <linux/bitfield.h>
#include <linux/errno.h>
#include <linux/iopoll.h>
#include <linux/mutex.h>

#include "platform-access.h"

#define PSP_CMD_TIMEOUT_US	(500 * USEC_PER_MSEC)
#define DOORBELL_CMDRESP_STS	GENMASK(7, 0)

/* Recovery field should be equal 0 to start sending commands */
static int check_recovery(u32 __iomem *cmd)
{
	return FIELD_GET(PSP_CMDRESP_RECOVERY, ioread32(cmd));
}

static int wait_cmd(u32 __iomem *cmd)
{
	u32 tmp, expected;

	/* Expect mbox_cmd to be cleared and ready bit to be set by PSP */
	expected = FIELD_PREP(PSP_CMDRESP_RESP, 1);

	/*
	 * Check for readiness of PSP mailbox in a tight loop in order to
	 * process further as soon as command was consumed.
	 */
	return readl_poll_timeout(cmd, tmp, (tmp & expected), 0,
				  PSP_CMD_TIMEOUT_US);
}

int psp_check_platform_access_status(void)
{
	struct psp_device *psp = psp_get_master_device();

	if (!psp || !psp->platform_access_data)
		return -ENODEV;

	return 0;
}
EXPORT_SYMBOL(psp_check_platform_access_status);

int psp_send_platform_access_msg(enum psp_platform_access_msg msg,
				 struct psp_request *req)
{
	struct psp_device *psp = psp_get_master_device();
	u32 __iomem *cmd, *lo, *hi;
	struct psp_platform_access_device *pa_dev;
	phys_addr_t req_addr;
	u32 cmd_reg;
	int ret;

	if (!psp || !psp->platform_access_data)
		return -ENODEV;

	pa_dev = psp->platform_access_data;
	cmd = psp->io_regs + pa_dev->vdata->cmdresp_reg;
	lo = psp->io_regs + pa_dev->vdata->cmdbuff_addr_lo_reg;
	hi = psp->io_regs + pa_dev->vdata->cmdbuff_addr_hi_reg;

	mutex_lock(&pa_dev->mailbox_mutex);

	if (check_recovery(cmd)) {
		dev_dbg(psp->dev, "platform mailbox is in recovery\n");
		ret = -EBUSY;
		goto unlock;
	}

	if (wait_cmd(cmd)) {
		dev_dbg(psp->dev, "platform mailbox is not done processing command\n");
		ret = -EBUSY;
		goto unlock;
	}

	/*
	 * Fill mailbox with address of command-response buffer, which will be
	 * used for sending i2c requests as well as reading status returned by
	 * PSP. Use physical address of buffer, since PSP will map this region.
	 */
	req_addr = __psp_pa(req);
	iowrite32(lower_32_bits(req_addr), lo);
	iowrite32(upper_32_bits(req_addr), hi);

	print_hex_dump_debug("->psp ", DUMP_PREFIX_OFFSET, 16, 2, req,
			     req->header.payload_size, false);

	/* Write command register to trigger processing */
	cmd_reg = FIELD_PREP(PSP_CMDRESP_CMD, msg);
	iowrite32(cmd_reg, cmd);

	if (wait_cmd(cmd)) {
		ret = -ETIMEDOUT;
		goto unlock;
	}

	/* Ensure it was triggered by this driver */
	if (ioread32(lo) != lower_32_bits(req_addr) ||
	    ioread32(hi) != upper_32_bits(req_addr)) {
		ret = -EBUSY;
		goto unlock;
	}

	/* Store the status in request header for caller to investigate */
	cmd_reg = ioread32(cmd);
	req->header.status = FIELD_GET(PSP_CMDRESP_STS, cmd_reg);
	if (req->header.status) {
		ret = -EIO;
		goto unlock;
	}

	print_hex_dump_debug("<-psp ", DUMP_PREFIX_OFFSET, 16, 2, req,
			     req->header.payload_size, false);

	ret = 0;

unlock:
	mutex_unlock(&pa_dev->mailbox_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(psp_send_platform_access_msg);

int psp_ring_platform_doorbell(int msg, u32 *result)
{
	struct psp_device *psp = psp_get_master_device();
	struct psp_platform_access_device *pa_dev;
	u32 __iomem *button, *cmd;
	int ret, val;

	if (!psp || !psp->platform_access_data)
		return -ENODEV;

	pa_dev = psp->platform_access_data;
	button = psp->io_regs + pa_dev->vdata->doorbell_button_reg;
	cmd = psp->io_regs + pa_dev->vdata->doorbell_cmd_reg;

	mutex_lock(&pa_dev->doorbell_mutex);

	if (wait_cmd(cmd)) {
		dev_err(psp->dev, "doorbell command not done processing\n");
		ret = -EBUSY;
		goto unlock;
	}

	iowrite32(FIELD_PREP(DOORBELL_CMDRESP_STS, msg), cmd);
	iowrite32(PSP_DRBL_RING, button);

	if (wait_cmd(cmd)) {
		ret = -ETIMEDOUT;
		goto unlock;
	}

	val = FIELD_GET(DOORBELL_CMDRESP_STS, ioread32(cmd));
	if (val) {
		if (result)
			*result = val;
		ret = -EIO;
		goto unlock;
	}

	ret = 0;
unlock:
	mutex_unlock(&pa_dev->doorbell_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(psp_ring_platform_doorbell);

void platform_access_dev_destroy(struct psp_device *psp)
{
	struct psp_platform_access_device *pa_dev = psp->platform_access_data;

	if (!pa_dev)
		return;

	mutex_destroy(&pa_dev->mailbox_mutex);
	mutex_destroy(&pa_dev->doorbell_mutex);
	psp->platform_access_data = NULL;
}

int platform_access_dev_init(struct psp_device *psp)
{
	struct device *dev = psp->dev;
	struct psp_platform_access_device *pa_dev;

	pa_dev = devm_kzalloc(dev, sizeof(*pa_dev), GFP_KERNEL);
	if (!pa_dev)
		return -ENOMEM;

	psp->platform_access_data = pa_dev;
	pa_dev->psp = psp;
	pa_dev->dev = dev;

	pa_dev->vdata = (struct platform_access_vdata *)psp->vdata->platform_access;

	mutex_init(&pa_dev->mailbox_mutex);
	mutex_init(&pa_dev->doorbell_mutex);

	dev_dbg(dev, "platform access enabled\n");

	return 0;
}
