// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#include <drm/drm_device.h>
#include <drm/drm_managed.h>
#include <drm/drm_print.h>
#include <linux/bitfield.h>
#include <linux/iopoll.h>
#include <linux/slab.h>

#include "aie.h"

#define PSP_STATUS_READY	BIT(31)

/* PSP commands */
#define PSP_VALIDATE		1
#define PSP_START		2
#define PSP_RELEASE_TMR		3
#define PSP_VALIDATE_CERT       4

/* PSP special arguments */
#define PSP_START_COPY_FW	1

/* PSP response error code */
#define PSP_ERROR_CANCEL	0xFFFF0002
#define PSP_ERROR_BAD_STATE	0xFFFF0007

#define PSP_FW_ALIGN		0x10000
#define PSP_CFW_ALIGN           0x8000
#define PSP_POLL_INTERVAL	20000	/* us */
#define PSP_POLL_TIMEOUT	1000000	/* us */

#define PSP_REG(p, reg) ((p)->conf.psp_regs[reg])
#define PSP_SET_CMD(psp, reg_vals, cmd, arg0, arg1, arg2)		\
({									\
	u32 *_regs = reg_vals;						\
	u32 _cmd = cmd;							\
	_regs[0] = _cmd;						\
	_regs[1] = arg0;						\
	_regs[2] = arg1;						\
	_regs[3] = ((arg2) | ((_cmd) << 24)) & (psp)->conf.arg2_mask;	\
})

struct psp_device {
	struct drm_device	*ddev;
	struct psp_config	conf;
	u32			fw_buf_sz;
	u64			fw_paddr;
	void			*fw_buffer;
	u32                     certfw_buf_sz;
	u64                     certfw_paddr;
	void                    *certfw_buffer;
};

static int psp_exec(struct psp_device *psp, u32 *reg_vals)
{
	u32 resp_code;
	int ret, i;
	u32 ready;

	/* Check for PSP ready before any write */
	ret = readx_poll_timeout(readl, PSP_REG(psp, PSP_STATUS_REG), ready,
				 FIELD_GET(PSP_STATUS_READY, ready),
				 PSP_POLL_INTERVAL, PSP_POLL_TIMEOUT);
	if (ret) {
		drm_err(psp->ddev, "PSP is not ready, ret 0x%x", ret);
		return ret;
	}

	/* Write command and argument registers */
	for (i = 0; i < PSP_NUM_IN_REGS; i++)
		writel(reg_vals[i], PSP_REG(psp, i));

	/* clear and set PSP INTR register to kick off */
	writel(0, PSP_REG(psp, PSP_INTR_REG));
	writel(psp->conf.notify_val, PSP_REG(psp, PSP_INTR_REG));

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

int aie_psp_waitmode_poll(struct psp_device *psp)
{
	struct amdxdna_dev *xdna = to_xdna_dev(psp->ddev);
	u32 mode_reg;
	int ret;

	ret = readx_poll_timeout(readl, PSP_REG(psp, PSP_PWAITMODE_REG), mode_reg,
				 (mode_reg & 0x1) == 1,
				 PSP_POLL_INTERVAL, PSP_POLL_TIMEOUT);
	if (ret)
		XDNA_ERR(xdna, "fw waitmode reg error, ret %d", ret);

	return ret;
}

void aie_psp_stop(struct psp_device *psp)
{
	u32 reg_vals[PSP_NUM_IN_REGS];
	int ret;

	PSP_SET_CMD(psp, reg_vals, PSP_RELEASE_TMR, 0, 0, 0);

	ret = psp_exec(psp, reg_vals);
	if (ret)
		drm_err(psp->ddev, "release tmr failed, ret %d", ret);
}

static int psp_validate_fw(struct psp_device *psp, u8 cmd, u64 paddr, u32 buf_sz)
{
	u32 reg_vals[PSP_NUM_IN_REGS];
	int ret;

	PSP_SET_CMD(psp, reg_vals, cmd, lower_32_bits(paddr),
		    upper_32_bits(paddr), buf_sz);

	ret = psp_exec(psp, reg_vals);
	if (ret)
		drm_err(psp->ddev, "failed to validate fw, ret %d", ret);

	return ret;
}

static int psp_start(struct psp_device *psp)
{
	u32 reg_vals[PSP_NUM_IN_REGS];
	int ret;

	PSP_SET_CMD(psp, reg_vals, PSP_START, PSP_START_COPY_FW, 0, 0);

	ret = psp_exec(psp, reg_vals);
	if (ret)
		drm_err(psp->ddev, "failed to start fw, ret %d", ret);

	return ret;
}

int aie_psp_start(struct psp_device *psp)
{
	int ret;

	ret = psp_validate_fw(psp, PSP_VALIDATE,
			      psp->fw_paddr, psp->fw_buf_sz);
	if (ret)
		return ret;

	if (!psp->certfw_buf_sz)
		goto psp_start;

	ret = psp_validate_fw(psp, PSP_VALIDATE_CERT,
			      psp->certfw_paddr, psp->certfw_buf_sz);
	if (ret)
		return ret;
psp_start:
	return psp_start(psp);
}

/*
 * PSP requires host physical address to load firmware.
 * Allocate a buffer, obtain its physical address, align, and copy data in.
 */
static void *psp_alloc_fw_buf(struct psp_device *psp, const void *fw_data,
			      u32 fw_size, u32 align, u32 *buf_sz,
			      u64 *paddr)
{
	u32 alloc_sz;
	void *buffer;
	u64 offset;

	*buf_sz = ALIGN(fw_size, align);
	alloc_sz = *buf_sz + align;

	buffer = drmm_kmalloc(psp->ddev, alloc_sz, GFP_KERNEL);
	if (!buffer)
		return NULL;

	*paddr = virt_to_phys(buffer);
	offset = ALIGN(*paddr, align) - *paddr;
	*paddr += offset;
	memcpy(buffer + offset, fw_data, fw_size);

	return buffer;
}

struct psp_device *aiem_psp_create(struct drm_device *ddev, struct psp_config *conf)
{
	struct psp_device *psp;

	psp = drmm_kzalloc(ddev, sizeof(*psp), GFP_KERNEL);
	if (!psp)
		return NULL;

	psp->ddev = ddev;
	psp->fw_buffer = psp_alloc_fw_buf(psp, conf->fw_buf, conf->fw_size,
					  PSP_FW_ALIGN, &psp->fw_buf_sz,
					  &psp->fw_paddr);
	if (!psp->fw_buffer)
		return NULL;

	if (!conf->certfw_size) {
		drm_dbg(ddev, "no cert fw");
		goto done;
	}

	/* CERT firmware */
	psp->certfw_buffer = psp_alloc_fw_buf(psp, conf->certfw_buf,
					      conf->certfw_size, PSP_CFW_ALIGN,
					      &psp->certfw_buf_sz,
					      &psp->certfw_paddr);
	if (!psp->certfw_buffer) {
		drm_err(ddev, "no memory for cert fw buffer");
		return NULL;
	}

done:
	memcpy(&psp->conf, conf, sizeof(psp->conf));

	return psp;
}
