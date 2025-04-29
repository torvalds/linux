// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2014-2018 Broadcom */

#include <linux/circ_buf.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/string_helpers.h>

#include <drm/drm_debugfs.h>

#include "v3d_drv.h"
#include "v3d_regs.h"

#define REGDEF(min_ver, max_ver, reg) { min_ver, max_ver, reg, #reg }
struct v3d_reg_def {
	u32 min_ver;
	u32 max_ver;
	u32 reg;
	const char *name;
};

static const struct v3d_reg_def v3d_hub_reg_defs[] = {
	REGDEF(V3D_GEN_33, V3D_GEN_42, V3D_HUB_AXICFG),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_HUB_UIFCFG),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_HUB_IDENT0),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_HUB_IDENT1),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_HUB_IDENT2),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_HUB_IDENT3),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_HUB_INT_STS),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_HUB_INT_MSK_STS),

	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_MMU_CTL),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_MMU_VIO_ADDR),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_MMU_VIO_ID),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_MMU_DEBUG_INFO),

	REGDEF(V3D_GEN_71, V3D_GEN_71, V3D_GMP_STATUS(71)),
	REGDEF(V3D_GEN_71, V3D_GEN_71, V3D_GMP_CFG(71)),
	REGDEF(V3D_GEN_71, V3D_GEN_71, V3D_GMP_VIO_ADDR(71)),
};

static const struct v3d_reg_def v3d_gca_reg_defs[] = {
	REGDEF(V3D_GEN_33, V3D_GEN_33, V3D_GCA_SAFE_SHUTDOWN),
	REGDEF(V3D_GEN_33, V3D_GEN_33, V3D_GCA_SAFE_SHUTDOWN_ACK),
};

static const struct v3d_reg_def v3d_core_reg_defs[] = {
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_CTL_IDENT0),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_CTL_IDENT1),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_CTL_IDENT2),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_CTL_MISCCFG),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_CTL_INT_STS),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_CTL_INT_MSK_STS),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_CLE_CT0CS),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_CLE_CT0CA),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_CLE_CT0EA),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_CLE_CT1CS),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_CLE_CT1CA),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_CLE_CT1EA),

	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_PTB_BPCA),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_PTB_BPCS),

	REGDEF(V3D_GEN_33, V3D_GEN_42, V3D_GMP_STATUS(33)),
	REGDEF(V3D_GEN_33, V3D_GEN_42, V3D_GMP_CFG(33)),
	REGDEF(V3D_GEN_33, V3D_GEN_42, V3D_GMP_VIO_ADDR(33)),

	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_ERR_FDBGO),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_ERR_FDBGB),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_ERR_FDBGS),
	REGDEF(V3D_GEN_33, V3D_GEN_71, V3D_ERR_STAT),
};

static const struct v3d_reg_def v3d_csd_reg_defs[] = {
	REGDEF(V3D_GEN_41, V3D_GEN_71, V3D_CSD_STATUS),
	REGDEF(V3D_GEN_41, V3D_GEN_42, V3D_CSD_CURRENT_CFG0(41)),
	REGDEF(V3D_GEN_41, V3D_GEN_42, V3D_CSD_CURRENT_CFG1(41)),
	REGDEF(V3D_GEN_41, V3D_GEN_42, V3D_CSD_CURRENT_CFG2(41)),
	REGDEF(V3D_GEN_41, V3D_GEN_42, V3D_CSD_CURRENT_CFG3(41)),
	REGDEF(V3D_GEN_41, V3D_GEN_42, V3D_CSD_CURRENT_CFG4(41)),
	REGDEF(V3D_GEN_41, V3D_GEN_42, V3D_CSD_CURRENT_CFG5(41)),
	REGDEF(V3D_GEN_41, V3D_GEN_42, V3D_CSD_CURRENT_CFG6(41)),
	REGDEF(V3D_GEN_71, V3D_GEN_71, V3D_CSD_CURRENT_CFG0(71)),
	REGDEF(V3D_GEN_71, V3D_GEN_71, V3D_CSD_CURRENT_CFG1(71)),
	REGDEF(V3D_GEN_71, V3D_GEN_71, V3D_CSD_CURRENT_CFG2(71)),
	REGDEF(V3D_GEN_71, V3D_GEN_71, V3D_CSD_CURRENT_CFG3(71)),
	REGDEF(V3D_GEN_71, V3D_GEN_71, V3D_CSD_CURRENT_CFG4(71)),
	REGDEF(V3D_GEN_71, V3D_GEN_71, V3D_CSD_CURRENT_CFG5(71)),
	REGDEF(V3D_GEN_71, V3D_GEN_71, V3D_CSD_CURRENT_CFG6(71)),
	REGDEF(V3D_GEN_71, V3D_GEN_71, V3D_V7_CSD_CURRENT_CFG7),
};

static int v3d_v3d_debugfs_regs(struct seq_file *m, void *unused)
{
	struct drm_debugfs_entry *entry = m->private;
	struct drm_device *dev = entry->dev;
	struct v3d_dev *v3d = to_v3d_dev(dev);
	int i, core;

	for (i = 0; i < ARRAY_SIZE(v3d_hub_reg_defs); i++) {
		const struct v3d_reg_def *def = &v3d_hub_reg_defs[i];

		if (v3d->ver >= def->min_ver && v3d->ver <= def->max_ver) {
			seq_printf(m, "%s (0x%04x): 0x%08x\n",
				   def->name, def->reg, V3D_READ(def->reg));
		}
	}

	for (i = 0; i < ARRAY_SIZE(v3d_gca_reg_defs); i++) {
		const struct v3d_reg_def *def = &v3d_gca_reg_defs[i];

		if (v3d->ver >= def->min_ver && v3d->ver <= def->max_ver) {
			seq_printf(m, "%s (0x%04x): 0x%08x\n",
				   def->name, def->reg, V3D_GCA_READ(def->reg));
		}
	}

	for (core = 0; core < v3d->cores; core++) {
		for (i = 0; i < ARRAY_SIZE(v3d_core_reg_defs); i++) {
			const struct v3d_reg_def *def = &v3d_core_reg_defs[i];

			if (v3d->ver >= def->min_ver && v3d->ver <= def->max_ver) {
				seq_printf(m, "core %d %s (0x%04x): 0x%08x\n",
					   core, def->name, def->reg,
					   V3D_CORE_READ(core, def->reg));
			}
		}

		for (i = 0; i < ARRAY_SIZE(v3d_csd_reg_defs); i++) {
			const struct v3d_reg_def *def = &v3d_csd_reg_defs[i];

			if (v3d->ver >= def->min_ver && v3d->ver <= def->max_ver) {
				seq_printf(m, "core %d %s (0x%04x): 0x%08x\n",
					   core, def->name, def->reg,
					   V3D_CORE_READ(core, def->reg));
			}
		}
	}

	return 0;
}

static int v3d_v3d_debugfs_ident(struct seq_file *m, void *unused)
{
	struct drm_debugfs_entry *entry = m->private;
	struct drm_device *dev = entry->dev;
	struct v3d_dev *v3d = to_v3d_dev(dev);
	u32 ident0, ident1, ident2, ident3, cores;
	int core;

	ident0 = V3D_READ(V3D_HUB_IDENT0);
	ident1 = V3D_READ(V3D_HUB_IDENT1);
	ident2 = V3D_READ(V3D_HUB_IDENT2);
	ident3 = V3D_READ(V3D_HUB_IDENT3);
	cores = V3D_GET_FIELD(ident1, V3D_HUB_IDENT1_NCORES);

	seq_printf(m, "Revision:   %d.%d.%d.%d\n",
		   V3D_GET_FIELD(ident1, V3D_HUB_IDENT1_TVER),
		   V3D_GET_FIELD(ident1, V3D_HUB_IDENT1_REV),
		   V3D_GET_FIELD(ident3, V3D_HUB_IDENT3_IPREV),
		   V3D_GET_FIELD(ident3, V3D_HUB_IDENT3_IPIDX));
	seq_printf(m, "MMU:        %s\n",
		   str_yes_no(ident2 & V3D_HUB_IDENT2_WITH_MMU));
	seq_printf(m, "TFU:        %s\n",
		   str_yes_no(ident1 & V3D_HUB_IDENT1_WITH_TFU));
	if (v3d->ver <= V3D_GEN_42) {
		seq_printf(m, "TSY:        %s\n",
			   str_yes_no(ident1 & V3D_HUB_IDENT1_WITH_TSY));
	}
	seq_printf(m, "MSO:        %s\n",
		   str_yes_no(ident1 & V3D_HUB_IDENT1_WITH_MSO));
	seq_printf(m, "L3C:        %s (%dkb)\n",
		   str_yes_no(ident1 & V3D_HUB_IDENT1_WITH_L3C),
		   V3D_GET_FIELD(ident2, V3D_HUB_IDENT2_L3C_NKB));

	for (core = 0; core < cores; core++) {
		u32 misccfg;
		u32 nslc, ntmu, qups;

		ident0 = V3D_CORE_READ(core, V3D_CTL_IDENT0);
		ident1 = V3D_CORE_READ(core, V3D_CTL_IDENT1);
		ident2 = V3D_CORE_READ(core, V3D_CTL_IDENT2);
		misccfg = V3D_CORE_READ(core, V3D_CTL_MISCCFG);

		nslc = V3D_GET_FIELD(ident1, V3D_IDENT1_NSLC);
		ntmu = V3D_GET_FIELD(ident1, V3D_IDENT1_NTMU);
		qups = V3D_GET_FIELD(ident1, V3D_IDENT1_QUPS);

		seq_printf(m, "Core %d:\n", core);
		seq_printf(m, "  Revision:     %d.%d\n",
			   V3D_GET_FIELD(ident0, V3D_IDENT0_VER),
			   V3D_GET_FIELD(ident1, V3D_IDENT1_REV));
		seq_printf(m, "  Slices:       %d\n", nslc);
		seq_printf(m, "  TMUs:         %d\n", nslc * ntmu);
		seq_printf(m, "  QPUs:         %d\n", nslc * qups);
		seq_printf(m, "  Semaphores:   %d\n",
			   V3D_GET_FIELD(ident1, V3D_IDENT1_NSEM));
		if (v3d->ver <= V3D_GEN_42) {
			seq_printf(m, "  BCG int:      %d\n",
				   (ident2 & V3D_IDENT2_BCG_INT) != 0);
		}
		if (v3d->ver < V3D_GEN_41) {
			seq_printf(m, "  Override TMU: %d\n",
				   (misccfg & V3D_MISCCFG_OVRTMUOUT) != 0);
		}
	}

	return 0;
}

static int v3d_debugfs_bo_stats(struct seq_file *m, void *unused)
{
	struct drm_debugfs_entry *entry = m->private;
	struct drm_device *dev = entry->dev;
	struct v3d_dev *v3d = to_v3d_dev(dev);

	mutex_lock(&v3d->bo_lock);
	seq_printf(m, "allocated bos:          %d\n",
		   v3d->bo_stats.num_allocated);
	seq_printf(m, "allocated bo size (kb): %ld\n",
		   (long)v3d->bo_stats.pages_allocated << (V3D_MMU_PAGE_SHIFT - 10));
	mutex_unlock(&v3d->bo_lock);

	return 0;
}

static int v3d_measure_clock(struct seq_file *m, void *unused)
{
	struct drm_debugfs_entry *entry = m->private;
	struct drm_device *dev = entry->dev;
	struct v3d_dev *v3d = to_v3d_dev(dev);
	uint32_t cycles;
	int core = 0;
	int measure_ms = 1000;

	if (v3d->ver >= V3D_GEN_41) {
		int cycle_count_reg = V3D_PCTR_CYCLE_COUNT(v3d->ver);
		V3D_CORE_WRITE(core, V3D_V4_PCTR_0_SRC_0_3,
			       V3D_SET_FIELD_VER(cycle_count_reg,
						 V3D_PCTR_S0, v3d->ver));
		V3D_CORE_WRITE(core, V3D_V4_PCTR_0_CLR, 1);
		V3D_CORE_WRITE(core, V3D_V4_PCTR_0_EN, 1);
	} else {
		V3D_CORE_WRITE(core, V3D_V3_PCTR_0_PCTRS0,
			       V3D_PCTR_CYCLE_COUNT(v3d->ver));
		V3D_CORE_WRITE(core, V3D_V3_PCTR_0_CLR, 1);
		V3D_CORE_WRITE(core, V3D_V3_PCTR_0_EN,
			       V3D_V3_PCTR_0_EN_ENABLE |
			       1);
	}
	msleep(measure_ms);
	cycles = V3D_CORE_READ(core, V3D_PCTR_0_PCTR0);

	seq_printf(m, "cycles: %d (%d.%d Mhz)\n",
		   cycles,
		   cycles / (measure_ms * 1000),
		   (cycles / (measure_ms * 100)) % 10);

	return 0;
}

static int v3d_debugfs_mm(struct seq_file *m, void *unused)
{
	struct drm_printer p = drm_seq_file_printer(m);
	struct drm_debugfs_entry *entry = m->private;
	struct drm_device *dev = entry->dev;
	struct v3d_dev *v3d = to_v3d_dev(dev);

	spin_lock(&v3d->mm_lock);
	drm_mm_print(&v3d->mm, &p);
	spin_unlock(&v3d->mm_lock);

	return 0;
}

static const struct drm_debugfs_info v3d_debugfs_list[] = {
	{"v3d_ident", v3d_v3d_debugfs_ident, 0},
	{"v3d_regs", v3d_v3d_debugfs_regs, 0},
	{"measure_clock", v3d_measure_clock, 0},
	{"bo_stats", v3d_debugfs_bo_stats, 0},
	{"v3d_mm", v3d_debugfs_mm, 0},
};

void
v3d_debugfs_init(struct drm_minor *minor)
{
	drm_debugfs_add_files(minor->dev, v3d_debugfs_list, ARRAY_SIZE(v3d_debugfs_list));
}
