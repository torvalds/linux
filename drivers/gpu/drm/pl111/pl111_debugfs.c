/*
 *  Copyright © 2017 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/amba/clcd-regs.h>
#include <linux/seq_file.h>
#include <drm/drm_debugfs.h>
#include <drm/drmP.h>
#include "pl111_drm.h"

#define REGDEF(reg) { reg, #reg }
static const struct {
	u32 reg;
	const char *name;
} pl111_reg_defs[] = {
	REGDEF(CLCD_TIM0),
	REGDEF(CLCD_TIM1),
	REGDEF(CLCD_TIM2),
	REGDEF(CLCD_TIM3),
	REGDEF(CLCD_UBAS),
	REGDEF(CLCD_PL111_CNTL),
	REGDEF(CLCD_PL111_IENB),
};

int pl111_debugfs_regs(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *dev = node->minor->dev;
	struct pl111_drm_dev_private *priv = dev->dev_private;
	int i;

	for (i = 0; i < ARRAY_SIZE(pl111_reg_defs); i++) {
		seq_printf(m, "%s (0x%04x): 0x%08x\n",
			   pl111_reg_defs[i].name, pl111_reg_defs[i].reg,
			   readl(priv->regs + pl111_reg_defs[i].reg));
	}

	return 0;
}

static const struct drm_info_list pl111_debugfs_list[] = {
	{"regs", pl111_debugfs_regs, 0},
};

int
pl111_debugfs_init(struct drm_minor *minor)
{
	return drm_debugfs_create_files(pl111_debugfs_list,
					ARRAY_SIZE(pl111_debugfs_list),
					minor->debugfs_root, minor);
}
