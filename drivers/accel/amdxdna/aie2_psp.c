// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022-2024, Advanced Micro Devices, Inc.
 */

#include <drm/drm_device.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_print.h>
#include <drm/gpu_scheduler.h>
#include <linux/bitfield.h>
#include <linux/iopoll.h>

#include "aie2_pci.h"
#include "amdxdna_mailbox.h"
#include "amdxdna_pci_drv.h"

#define PSP_STATUS_READY	BIT(31)

/* PSP commands */
#define PSP_VALIDATE		1
#define PSP_START		2
#define PSP_RELEASE_TMR		3

/* PSP special arguments */
#define PSP_START_COPY_FW	1

/* PSP response error code */
#define PSP_ERROR_CANCEL	0xFFFF0002
#define PSP_ERROR_BAD_STATE	0xFFFF0007

#define PSP_FW_ALIGN		0x10000
#define PSP_POLL_INTERVAL	20000	/* us */
#define PSP_POLL_TIMEOUT	1000000	/* us */

#define PSP_REG(p, reg) ((p)->psp_regs[reg])

struct psp_device {
	struct drm_device	*ddev;
	struct psp_config	conf;
	u32			fw_buf_sz;
	u64			fw_paddr;
	void			*fw_buffer;
	void __iomem		*psp_regs[PSP_MAX_REGS];
};

static int psp_exec(struct psp_device *psp, u32 *reg_vals)
{
	u32 resp_code;
	int ret, i;
	u32 ready;

	/* Write command and argument registers */
	for (i = 0; i < PSP_NUM_IN_REGS; i++)
		writel(reg_vals[i], PSP_REG(psp, i));

	/* clear and set PSP INTR register to kick off */
	writel(0, PSP_REG(psp, PSP_INTR_REG));
	writel(1, PSP_REG(psp, PSP_INTR_REG));

	/* PSP should be busy. Wait for ready, so we know task is done. */
	ret = readx_poll_timeout(readl, PSP_REG(psp, PSP_STATUS_REG), ready,
				 FIELD_GET(PSP_STATUS_READY, ready),
				 PSP_POLL_INTERVAL, PSP_POLL_TIMEOUT);
	if (ret) {
		drm_err(psp->ddev, "PSP is not ready, ret 0x%x", ret);
		return ret;
	}

	resp_code = readl(PSP_REG(psp, PSP_RESP_REG));
	if (resp_code) {
		drm_err(psp->ddev, "fw return error 0x%x", resp_code);
		return -EIO;
	}

	return 0;
}

void aie2_psp_stop(struct psp_device *psp)
{
	u32 reg_vals[PSP_NUM_IN_REGS] = { PSP_RELEASE_TMR, };
	int ret;

	ret = psp_exec(psp, reg_vals);
	if (ret)
		drm_err(psp->ddev, "release tmr failed, ret %d", ret);
}

int aie2_psp_start(struct psp_device *psp)
{
	u32 reg_vals[PSP_NUM_IN_REGS];
	int ret;

	reg_vals[0] = PSP_VALIDATE;
	reg_vals[1] = lower_32_bits(psp->fw_paddr);
	reg_vals[2] = upper_32_bits(psp->fw_paddr);
	reg_vals[3] = psp->fw_buf_sz;

	ret = psp_exec(psp, reg_vals);
	if (ret) {
		drm_err(psp->ddev, "failed to validate fw, ret %d", ret);
		return ret;
	}

	memset(reg_vals, 0, sizeof(reg_vals));
	reg_vals[0] = PSP_START;
	reg_vals[1] = PSP_START_COPY_FW;
	ret = psp_exec(psp, reg_vals);
	if (ret) {
		drm_err(psp->ddev, "failed to start fw, ret %d", ret);
		return ret;
	}

	return 0;
}

struct psp_device *aie2m_psp_create(struct drm_device *ddev, struct psp_config *conf)
{
	struct psp_device *psp;
	u64 offset;

	psp = drmm_kzalloc(ddev, sizeof(*psp), GFP_KERNEL);
	if (!psp)
		return NULL;

	psp->ddev = ddev;
	memcpy(psp->psp_regs, conf->psp_regs, sizeof(psp->psp_regs));

	psp->fw_buf_sz = ALIGN(conf->fw_size, PSP_FW_ALIGN) + PSP_FW_ALIGN;
	psp->fw_buffer = drmm_kmalloc(ddev, psp->fw_buf_sz, GFP_KERNEL);
	if (!psp->fw_buffer) {
		drm_err(ddev, "no memory for fw buffer");
		return NULL;
	}

	/*
	 * AMD Platform Security Processor(PSP) requires host physical
	 * address to load NPU firmware.
	 */
	psp->fw_paddr = virt_to_phys(psp->fw_buffer);
	offset = ALIGN(psp->fw_paddr, PSP_FW_ALIGN) - psp->fw_paddr;
	psp->fw_paddr += offset;
	memcpy(psp->fw_buffer + offset, conf->fw_buf, conf->fw_size);

	return psp;
}
