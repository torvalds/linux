// SPDX-License-Identifier: MIT
/*
 * AMD Trusted Execution Environment (TEE) interface
 *
 * Author: Rijo Thomas <Rijo-john.Thomas@amd.com>
 * Author: Devaraj Rangasamy <Devaraj.Rangasamy@amd.com>
 *
 * Copyright 2019 Advanced Micro Devices, Inc.
 */

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/psp-sev.h>
#include <linux/psp-tee.h>

#include "psp-dev.h"
#include "tee-dev.h"

static bool psp_dead;

static int tee_alloc_ring(struct psp_tee_device *tee, int ring_size)
{
	struct ring_buf_manager *rb_mgr = &tee->rb_mgr;
	void *start_addr;

	if (!ring_size)
		return -EINVAL;

	/* We need actual physical address instead of DMA address, since
	 * Trusted OS running on AMD Secure Processor will map this region
	 */
	start_addr = (void *)__get_free_pages(GFP_KERNEL, get_order(ring_size));
	if (!start_addr)
		return -ENOMEM;

	rb_mgr->ring_start = start_addr;
	rb_mgr->ring_size = ring_size;
	rb_mgr->ring_pa = __psp_pa(start_addr);
	mutex_init(&rb_mgr->mutex);

	return 0;
}

static void tee_free_ring(struct psp_tee_device *tee)
{
	struct ring_buf_manager *rb_mgr = &tee->rb_mgr;

	if (!rb_mgr->ring_start)
		return;

	free_pages((unsigned long)rb_mgr->ring_start,
		   get_order(rb_mgr->ring_size));

	rb_mgr->ring_start = NULL;
	rb_mgr->ring_size = 0;
	rb_mgr->ring_pa = 0;
	mutex_destroy(&rb_mgr->mutex);
}

static int tee_wait_cmd_poll(struct psp_tee_device *tee, unsigned int timeout,
			     unsigned int *reg)
{
	/* ~10ms sleep per loop => nloop = timeout * 100 */
	int nloop = timeout * 100;

	while (--nloop) {
		*reg = ioread32(tee->io_regs + tee->vdata->cmdresp_reg);
		if (*reg & PSP_CMDRESP_RESP)
			return 0;

		usleep_range(10000, 10100);
	}

	dev_err(tee->dev, "tee: command timed out, disabling PSP\n");
	psp_dead = true;

	return -ETIMEDOUT;
}

static
struct tee_init_ring_cmd *tee_alloc_cmd_buffer(struct psp_tee_device *tee)
{
	struct tee_init_ring_cmd *cmd;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return NULL;

	cmd->hi_addr = upper_32_bits(tee->rb_mgr.ring_pa);
	cmd->low_addr = lower_32_bits(tee->rb_mgr.ring_pa);
	cmd->size = tee->rb_mgr.ring_size;

	dev_dbg(tee->dev, "tee: ring address: high = 0x%x low = 0x%x size = %u\n",
		cmd->hi_addr, cmd->low_addr, cmd->size);

	return cmd;
}

static inline void tee_free_cmd_buffer(struct tee_init_ring_cmd *cmd)
{
	kfree(cmd);
}

static int tee_init_ring(struct psp_tee_device *tee)
{
	int ring_size = MAX_RING_BUFFER_ENTRIES * sizeof(struct tee_ring_cmd);
	struct tee_init_ring_cmd *cmd;
	phys_addr_t cmd_buffer;
	unsigned int reg;
	int ret;

	BUILD_BUG_ON(sizeof(struct tee_ring_cmd) != 1024);

	ret = tee_alloc_ring(tee, ring_size);
	if (ret) {
		dev_err(tee->dev, "tee: ring allocation failed %d\n", ret);
		return ret;
	}

	tee->rb_mgr.wptr = 0;

	cmd = tee_alloc_cmd_buffer(tee);
	if (!cmd) {
		tee_free_ring(tee);
		return -ENOMEM;
	}

	cmd_buffer = __psp_pa((void *)cmd);

	/* Send command buffer details to Trusted OS by writing to
	 * CPU-PSP message registers
	 */

	iowrite32(lower_32_bits(cmd_buffer),
		  tee->io_regs + tee->vdata->cmdbuff_addr_lo_reg);
	iowrite32(upper_32_bits(cmd_buffer),
		  tee->io_regs + tee->vdata->cmdbuff_addr_hi_reg);
	iowrite32(TEE_RING_INIT_CMD,
		  tee->io_regs + tee->vdata->cmdresp_reg);

	ret = tee_wait_cmd_poll(tee, TEE_DEFAULT_TIMEOUT, &reg);
	if (ret) {
		dev_err(tee->dev, "tee: ring init command timed out\n");
		tee_free_ring(tee);
		goto free_buf;
	}

	if (reg & PSP_CMDRESP_ERR_MASK) {
		dev_err(tee->dev, "tee: ring init command failed (%#010x)\n",
			reg & PSP_CMDRESP_ERR_MASK);
		tee_free_ring(tee);
		ret = -EIO;
	}

free_buf:
	tee_free_cmd_buffer(cmd);

	return ret;
}

static void tee_destroy_ring(struct psp_tee_device *tee)
{
	unsigned int reg;
	int ret;

	if (!tee->rb_mgr.ring_start)
		return;

	if (psp_dead)
		goto free_ring;

	iowrite32(TEE_RING_DESTROY_CMD,
		  tee->io_regs + tee->vdata->cmdresp_reg);

	ret = tee_wait_cmd_poll(tee, TEE_DEFAULT_TIMEOUT, &reg);
	if (ret) {
		dev_err(tee->dev, "tee: ring destroy command timed out\n");
	} else if (reg & PSP_CMDRESP_ERR_MASK) {
		dev_err(tee->dev, "tee: ring destroy command failed (%#010x)\n",
			reg & PSP_CMDRESP_ERR_MASK);
	}

free_ring:
	tee_free_ring(tee);
}

int tee_dev_init(struct psp_device *psp)
{
	struct device *dev = psp->dev;
	struct psp_tee_device *tee;
	int ret;

	ret = -ENOMEM;
	tee = devm_kzalloc(dev, sizeof(*tee), GFP_KERNEL);
	if (!tee)
		goto e_err;

	psp->tee_data = tee;

	tee->dev = dev;
	tee->psp = psp;

	tee->io_regs = psp->io_regs;

	tee->vdata = (struct tee_vdata *)psp->vdata->tee;
	if (!tee->vdata) {
		ret = -ENODEV;
		dev_err(dev, "tee: missing driver data\n");
		goto e_err;
	}

	ret = tee_init_ring(tee);
	if (ret) {
		dev_err(dev, "tee: failed to init ring buffer\n");
		goto e_err;
	}

	dev_notice(dev, "tee enabled\n");

	return 0;

e_err:
	psp->tee_data = NULL;

	dev_notice(dev, "tee initialization failed\n");

	return ret;
}

void tee_dev_destroy(struct psp_device *psp)
{
	struct psp_tee_device *tee = psp->tee_data;

	if (!tee)
		return;

	tee_destroy_ring(tee);
}

static int tee_submit_cmd(struct psp_tee_device *tee, enum tee_cmd_id cmd_id,
			  void *buf, size_t len, struct tee_ring_cmd **resp)
{
	struct tee_ring_cmd *cmd;
	u32 rptr, wptr;
	int nloop = 1000, ret = 0;

	*resp = NULL;

	mutex_lock(&tee->rb_mgr.mutex);

	wptr = tee->rb_mgr.wptr;

	/* Check if ring buffer is full */
	do {
		rptr = ioread32(tee->io_regs + tee->vdata->ring_rptr_reg);

		if (!(wptr + sizeof(struct tee_ring_cmd) == rptr))
			break;

		dev_info(tee->dev, "tee: ring buffer full. rptr = %u wptr = %u\n",
			 rptr, wptr);

		/* Wait if ring buffer is full */
		mutex_unlock(&tee->rb_mgr.mutex);
		schedule_timeout_interruptible(msecs_to_jiffies(10));
		mutex_lock(&tee->rb_mgr.mutex);

	} while (--nloop);

	if (!nloop && (wptr + sizeof(struct tee_ring_cmd) == rptr)) {
		dev_err(tee->dev, "tee: ring buffer full. rptr = %u wptr = %u\n",
			rptr, wptr);
		ret = -EBUSY;
		goto unlock;
	}

	/* Pointer to empty data entry in ring buffer */
	cmd = (struct tee_ring_cmd *)(tee->rb_mgr.ring_start + wptr);

	/* Write command data into ring buffer */
	cmd->cmd_id = cmd_id;
	cmd->cmd_state = TEE_CMD_STATE_INIT;
	memset(&cmd->buf[0], 0, sizeof(cmd->buf));
	memcpy(&cmd->buf[0], buf, len);

	/* Update local copy of write pointer */
	tee->rb_mgr.wptr += sizeof(struct tee_ring_cmd);
	if (tee->rb_mgr.wptr >= tee->rb_mgr.ring_size)
		tee->rb_mgr.wptr = 0;

	/* Trigger interrupt to Trusted OS */
	iowrite32(tee->rb_mgr.wptr, tee->io_regs + tee->vdata->ring_wptr_reg);

	/* The response is provided by Trusted OS in same
	 * location as submitted data entry within ring buffer.
	 */
	*resp = cmd;

unlock:
	mutex_unlock(&tee->rb_mgr.mutex);

	return ret;
}

static int tee_wait_cmd_completion(struct psp_tee_device *tee,
				   struct tee_ring_cmd *resp,
				   unsigned int timeout)
{
	/* ~5ms sleep per loop => nloop = timeout * 200 */
	int nloop = timeout * 200;

	while (--nloop) {
		if (resp->cmd_state == TEE_CMD_STATE_COMPLETED)
			return 0;

		usleep_range(5000, 5100);
	}

	dev_err(tee->dev, "tee: command 0x%x timed out, disabling PSP\n",
		resp->cmd_id);

	psp_dead = true;

	return -ETIMEDOUT;
}

int psp_tee_process_cmd(enum tee_cmd_id cmd_id, void *buf, size_t len,
			u32 *status)
{
	struct psp_device *psp = psp_get_master_device();
	struct psp_tee_device *tee;
	struct tee_ring_cmd *resp;
	int ret;

	if (!buf || !status || !len || len > sizeof(resp->buf))
		return -EINVAL;

	*status = 0;

	if (!psp || !psp->tee_data)
		return -ENODEV;

	if (psp_dead)
		return -EBUSY;

	tee = psp->tee_data;

	ret = tee_submit_cmd(tee, cmd_id, buf, len, &resp);
	if (ret)
		return ret;

	ret = tee_wait_cmd_completion(tee, resp, TEE_DEFAULT_TIMEOUT);
	if (ret)
		return ret;

	memcpy(buf, &resp->buf[0], len);
	*status = resp->status;

	return 0;
}
EXPORT_SYMBOL(psp_tee_process_cmd);

int psp_check_tee_status(void)
{
	struct psp_device *psp = psp_get_master_device();

	if (!psp || !psp->tee_data)
		return -ENODEV;

	return 0;
}
EXPORT_SYMBOL(psp_check_tee_status);
