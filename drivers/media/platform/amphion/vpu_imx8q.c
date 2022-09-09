// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020-2021 NXP
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/types.h>
#include "vpu.h"
#include "vpu_core.h"
#include "vpu_imx8q.h"
#include "vpu_rpc.h"

#define IMX8Q_CSR_CM0Px_ADDR_OFFSET			0x00000000
#define IMX8Q_CSR_CM0Px_CPUWAIT				0x00000004

#ifdef CONFIG_IMX_SCU
#include <linux/firmware/imx/ipc.h>
#include <linux/firmware/imx/svc/misc.h>

#define VPU_DISABLE_BITS			0x7
#define VPU_IMX_DECODER_FUSE_OFFSET		14
#define VPU_ENCODER_MASK			0x1
#define VPU_DECODER_MASK			0x3UL
#define VPU_DECODER_H264_MASK			0x2UL
#define VPU_DECODER_HEVC_MASK			0x1UL

static u32 imx8q_fuse;

struct vpu_sc_msg_misc {
	struct imx_sc_rpc_msg hdr;
	u32 word;
} __packed;
#endif

int vpu_imx8q_setup_dec(struct vpu_dev *vpu)
{
	const off_t offset = DEC_MFD_XREG_SLV_BASE + MFD_BLK_CTRL;

	vpu_writel(vpu, offset + MFD_BLK_CTRL_MFD_SYS_CLOCK_ENABLE_SET, 0x1f);
	vpu_writel(vpu, offset + MFD_BLK_CTRL_MFD_SYS_RESET_SET, 0xffffffff);

	return 0;
}

int vpu_imx8q_setup_enc(struct vpu_dev *vpu)
{
	return 0;
}

int vpu_imx8q_setup(struct vpu_dev *vpu)
{
	const off_t offset = SCB_XREG_SLV_BASE + SCB_SCB_BLK_CTRL;

	vpu_readl(vpu, offset + 0x108);

	vpu_writel(vpu, offset + SCB_BLK_CTRL_SCB_CLK_ENABLE_SET, 0x1);
	vpu_writel(vpu, offset + 0x190, 0xffffffff);
	vpu_writel(vpu, offset + SCB_BLK_CTRL_XMEM_RESET_SET, 0xffffffff);
	vpu_writel(vpu, offset + SCB_BLK_CTRL_SCB_CLK_ENABLE_SET, 0xE);
	vpu_writel(vpu, offset + SCB_BLK_CTRL_CACHE_RESET_SET, 0x7);
	vpu_writel(vpu, XMEM_CONTROL, 0x102);

	vpu_readl(vpu, offset + 0x108);

	return 0;
}

static int vpu_imx8q_reset_enc(struct vpu_dev *vpu)
{
	return 0;
}

static int vpu_imx8q_reset_dec(struct vpu_dev *vpu)
{
	const off_t offset = DEC_MFD_XREG_SLV_BASE + MFD_BLK_CTRL;

	vpu_writel(vpu, offset + MFD_BLK_CTRL_MFD_SYS_RESET_CLR, 0xffffffff);

	return 0;
}

int vpu_imx8q_reset(struct vpu_dev *vpu)
{
	const off_t offset = SCB_XREG_SLV_BASE + SCB_SCB_BLK_CTRL;

	vpu_writel(vpu, offset + SCB_BLK_CTRL_CACHE_RESET_CLR, 0x7);
	vpu_imx8q_reset_enc(vpu);
	vpu_imx8q_reset_dec(vpu);

	return 0;
}

int vpu_imx8q_set_system_cfg_common(struct vpu_rpc_system_config *config, u32 regs, u32 core_id)
{
	if (!config)
		return -EINVAL;

	switch (core_id) {
	case 0:
		config->malone_base_addr[0] = regs + DEC_MFD_XREG_SLV_BASE;
		config->num_malones = 1;
		config->num_windsors = 0;
		break;
	case 1:
		config->windsor_base_addr[0] = regs + ENC_MFD_XREG_SLV_0_BASE;
		config->num_windsors = 1;
		config->num_malones = 0;
		break;
	case 2:
		config->windsor_base_addr[0] = regs + ENC_MFD_XREG_SLV_1_BASE;
		config->num_windsors = 1;
		config->num_malones = 0;
		break;
	default:
		return -EINVAL;
	}
	if (config->num_windsors) {
		config->windsor_irq_pin[0x0][0x0] = WINDSOR_PAL_IRQ_PIN_L;
		config->windsor_irq_pin[0x0][0x1] = WINDSOR_PAL_IRQ_PIN_H;
	}

	config->malone_base_addr[0x1] = 0x0;
	config->hif_offset[0x0] = MFD_HIF;
	config->hif_offset[0x1] = 0x0;

	config->dpv_base_addr = 0x0;
	config->dpv_irq_pin = 0x0;
	config->pixif_base_addr = regs + DEC_MFD_XREG_SLV_BASE + MFD_PIX_IF;
	config->cache_base_addr[0] = regs + MC_CACHE_0_BASE;
	config->cache_base_addr[1] = regs + MC_CACHE_1_BASE;

	return 0;
}

int vpu_imx8q_boot_core(struct vpu_core *core)
{
	csr_writel(core, IMX8Q_CSR_CM0Px_ADDR_OFFSET, core->fw.phys);
	csr_writel(core, IMX8Q_CSR_CM0Px_CPUWAIT, 0);
	return 0;
}

int vpu_imx8q_get_power_state(struct vpu_core *core)
{
	if (csr_readl(core, IMX8Q_CSR_CM0Px_CPUWAIT) == 1)
		return 0;
	return 1;
}

int vpu_imx8q_on_firmware_loaded(struct vpu_core *core)
{
	u8 *p;

	p = core->fw.virt;
	p[16] = core->vpu->res->plat_type;
	p[17] = core->id;
	p[18] = 1;

	return 0;
}

int vpu_imx8q_check_memory_region(dma_addr_t base, dma_addr_t addr, u32 size)
{
	const struct vpu_rpc_region_t imx8q_regions[] = {
		{0x00000000, 0x08000000, VPU_CORE_MEMORY_CACHED},
		{0x08000000, 0x10000000, VPU_CORE_MEMORY_UNCACHED},
		{0x10000000, 0x20000000, VPU_CORE_MEMORY_CACHED},
		{0x20000000, 0x40000000, VPU_CORE_MEMORY_UNCACHED}
	};
	int i;

	if (addr < base)
		return VPU_CORE_MEMORY_INVALID;

	addr -= base;
	for (i = 0; i < ARRAY_SIZE(imx8q_regions); i++) {
		const struct vpu_rpc_region_t *region = &imx8q_regions[i];

		if (addr >= region->start && addr + size < region->end)
			return region->type;
	}

	return VPU_CORE_MEMORY_INVALID;
}

#ifdef CONFIG_IMX_SCU
static u32 vpu_imx8q_get_fuse(void)
{
	static u32 fuse_got;
	struct imx_sc_ipc *ipc;
	struct vpu_sc_msg_misc msg;
	struct imx_sc_rpc_msg *hdr = &msg.hdr;
	int ret;

	if (fuse_got)
		return imx8q_fuse;

	ret = imx_scu_get_handle(&ipc);
	if (ret) {
		pr_err("error: get sct handle fail: %d\n", ret);
		return 0;
	}

	hdr->ver = IMX_SC_RPC_VERSION;
	hdr->svc = IMX_SC_RPC_SVC_MISC;
	hdr->func = IMX_SC_MISC_FUNC_OTP_FUSE_READ;
	hdr->size = 2;

	msg.word = VPU_DISABLE_BITS;

	ret = imx_scu_call_rpc(ipc, &msg, true);
	if (ret)
		return 0;

	imx8q_fuse = msg.word;
	fuse_got = 1;
	return imx8q_fuse;
}

bool vpu_imx8q_check_codec(enum vpu_core_type type)
{
	u32 fuse = vpu_imx8q_get_fuse();

	if (type == VPU_CORE_TYPE_ENC) {
		if (fuse & VPU_ENCODER_MASK)
			return false;
	} else if (type == VPU_CORE_TYPE_DEC) {
		fuse >>= VPU_IMX_DECODER_FUSE_OFFSET;
		fuse &= VPU_DECODER_MASK;

		if (fuse == VPU_DECODER_MASK)
			return false;
	}
	return true;
}

bool vpu_imx8q_check_fmt(enum vpu_core_type type, u32 pixelfmt)
{
	u32 fuse = vpu_imx8q_get_fuse();

	if (type == VPU_CORE_TYPE_DEC) {
		fuse >>= VPU_IMX_DECODER_FUSE_OFFSET;
		fuse &= VPU_DECODER_MASK;

		if (fuse == VPU_DECODER_HEVC_MASK && pixelfmt == V4L2_PIX_FMT_HEVC)
			return false;
		if (fuse == VPU_DECODER_H264_MASK && pixelfmt == V4L2_PIX_FMT_H264)
			return false;
		if (fuse == VPU_DECODER_MASK)
			return false;
	}

	return true;
}
#else
bool vpu_imx8q_check_codec(enum vpu_core_type type)
{
	return true;
}

bool vpu_imx8q_check_fmt(enum vpu_core_type type, u32 pixelfmt)
{
	return true;
}
#endif
