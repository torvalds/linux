// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019-2020 NXP
 */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>
#include <linux/types.h>

#include "imx8-isi-core.h"
#include "imx8-isi-regs.h"

static inline u32 mxc_isi_read(struct mxc_isi_pipe *pipe, u32 reg)
{
	return readl(pipe->regs + reg);
}

static int mxc_isi_debug_dump_regs_show(struct seq_file *m, void *p)
{
#define MXC_ISI_DEBUG_REG(name)		{ name, #name }
	static const struct {
		u32 offset;
		const char * const name;
	} registers[] = {
		MXC_ISI_DEBUG_REG(CHNL_CTRL),
		MXC_ISI_DEBUG_REG(CHNL_IMG_CTRL),
		MXC_ISI_DEBUG_REG(CHNL_OUT_BUF_CTRL),
		MXC_ISI_DEBUG_REG(CHNL_IMG_CFG),
		MXC_ISI_DEBUG_REG(CHNL_IER),
		MXC_ISI_DEBUG_REG(CHNL_STS),
		MXC_ISI_DEBUG_REG(CHNL_SCALE_FACTOR),
		MXC_ISI_DEBUG_REG(CHNL_SCALE_OFFSET),
		MXC_ISI_DEBUG_REG(CHNL_CROP_ULC),
		MXC_ISI_DEBUG_REG(CHNL_CROP_LRC),
		MXC_ISI_DEBUG_REG(CHNL_CSC_COEFF0),
		MXC_ISI_DEBUG_REG(CHNL_CSC_COEFF1),
		MXC_ISI_DEBUG_REG(CHNL_CSC_COEFF2),
		MXC_ISI_DEBUG_REG(CHNL_CSC_COEFF3),
		MXC_ISI_DEBUG_REG(CHNL_CSC_COEFF4),
		MXC_ISI_DEBUG_REG(CHNL_CSC_COEFF5),
		MXC_ISI_DEBUG_REG(CHNL_ROI_0_ALPHA),
		MXC_ISI_DEBUG_REG(CHNL_ROI_0_ULC),
		MXC_ISI_DEBUG_REG(CHNL_ROI_0_LRC),
		MXC_ISI_DEBUG_REG(CHNL_ROI_1_ALPHA),
		MXC_ISI_DEBUG_REG(CHNL_ROI_1_ULC),
		MXC_ISI_DEBUG_REG(CHNL_ROI_1_LRC),
		MXC_ISI_DEBUG_REG(CHNL_ROI_2_ALPHA),
		MXC_ISI_DEBUG_REG(CHNL_ROI_2_ULC),
		MXC_ISI_DEBUG_REG(CHNL_ROI_2_LRC),
		MXC_ISI_DEBUG_REG(CHNL_ROI_3_ALPHA),
		MXC_ISI_DEBUG_REG(CHNL_ROI_3_ULC),
		MXC_ISI_DEBUG_REG(CHNL_ROI_3_LRC),
		MXC_ISI_DEBUG_REG(CHNL_OUT_BUF1_ADDR_Y),
		MXC_ISI_DEBUG_REG(CHNL_OUT_BUF1_ADDR_U),
		MXC_ISI_DEBUG_REG(CHNL_OUT_BUF1_ADDR_V),
		MXC_ISI_DEBUG_REG(CHNL_OUT_BUF_PITCH),
		MXC_ISI_DEBUG_REG(CHNL_IN_BUF_ADDR),
		MXC_ISI_DEBUG_REG(CHNL_IN_BUF_PITCH),
		MXC_ISI_DEBUG_REG(CHNL_MEM_RD_CTRL),
		MXC_ISI_DEBUG_REG(CHNL_OUT_BUF2_ADDR_Y),
		MXC_ISI_DEBUG_REG(CHNL_OUT_BUF2_ADDR_U),
		MXC_ISI_DEBUG_REG(CHNL_OUT_BUF2_ADDR_V),
		MXC_ISI_DEBUG_REG(CHNL_SCL_IMG_CFG),
		MXC_ISI_DEBUG_REG(CHNL_FLOW_CTRL),
	};

	struct mxc_isi_pipe *pipe = m->private;
	unsigned int i;

	if (!pm_runtime_get_if_in_use(pipe->isi->dev))
		return 0;

	seq_printf(m, "--- ISI pipe %u registers ---\n", pipe->id);

	for (i = 0; i < ARRAY_SIZE(registers); ++i)
		seq_printf(m, "%20s[0x%02x]: 0x%08x\n",
			   registers[i].name, registers[i].offset,
			   mxc_isi_read(pipe, registers[i].offset));

	pm_runtime_put(pipe->isi->dev);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(mxc_isi_debug_dump_regs);

void mxc_isi_debug_init(struct mxc_isi_dev *isi)
{
	unsigned int i;

	isi->debugfs_root = debugfs_create_dir(dev_name(isi->dev), NULL);

	for (i = 0; i < isi->pdata->num_channels; ++i) {
		struct mxc_isi_pipe *pipe = &isi->pipes[i];
		char name[8];

		sprintf(name, "pipe%u", pipe->id);
		debugfs_create_file(name, 0444, isi->debugfs_root, pipe,
				    &mxc_isi_debug_dump_regs_fops);
	}
}

void mxc_isi_debug_cleanup(struct mxc_isi_dev *isi)
{
	debugfs_remove_recursive(isi->debugfs_root);
}
