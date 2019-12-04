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
