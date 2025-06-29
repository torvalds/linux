// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2023 Intel Corporation

#include <linux/acpi.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_intel.h>
#include <linux/soundwire/sdw_registers.h>
#include "bus.h"
#include "cadence_master.h"
#include "intel.h"

/*
 * debugfs
 */
#ifdef CONFIG_DEBUG_FS

#define RD_BUF (2 * PAGE_SIZE)

static ssize_t intel_sprintf(void __iomem *mem, bool l,
			     char *buf, size_t pos, unsigned int reg)
{
	int value;

	if (l)
		value = intel_readl(mem, reg);
	else
		value = intel_readw(mem, reg);

	return scnprintf(buf + pos, RD_BUF - pos, "%4x\t%4x\n", reg, value);
}

static int intel_reg_show(struct seq_file *s_file, void *data)
{
	struct sdw_intel *sdw = s_file->private;
	void __iomem *s = sdw->link_res->shim;
	void __iomem *vs_s = sdw->link_res->shim_vs;
	ssize_t ret;
	u32 pcm_cap;
	int pcm_bd;
	char *buf;
	int j;

	buf = kzalloc(RD_BUF, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = scnprintf(buf, RD_BUF, "Register  Value\n");
	ret += scnprintf(buf + ret, RD_BUF - ret, "\nShim\n");

	ret += intel_sprintf(s, true, buf, ret, SDW_SHIM2_LECAP);
	ret += intel_sprintf(s, false, buf, ret, SDW_SHIM2_PCMSCAP);

	pcm_cap = intel_readw(s, SDW_SHIM2_PCMSCAP);
	pcm_bd = FIELD_GET(SDW_SHIM2_PCMSCAP_BSS, pcm_cap);

	for (j = 0; j < pcm_bd; j++) {
		ret += intel_sprintf(s, false, buf, ret,
				SDW_SHIM2_PCMSYCHM(j));
		ret += intel_sprintf(s, false, buf, ret,
				SDW_SHIM2_PCMSYCHC(j));
	}

	ret += scnprintf(buf + ret, RD_BUF - ret, "\nVS CLK controls\n");
	ret += intel_sprintf(vs_s, true, buf, ret, SDW_SHIM2_INTEL_VS_LVSCTL);

	ret += scnprintf(buf + ret, RD_BUF - ret, "\nVS Wake registers\n");
	ret += intel_sprintf(vs_s, false, buf, ret, SDW_SHIM2_INTEL_VS_WAKEEN);
	ret += intel_sprintf(vs_s, false, buf, ret, SDW_SHIM2_INTEL_VS_WAKESTS);

	ret += scnprintf(buf + ret, RD_BUF - ret, "\nVS IOCTL, ACTMCTL\n");
	ret += intel_sprintf(vs_s, false, buf, ret, SDW_SHIM2_INTEL_VS_IOCTL);
	ret += intel_sprintf(vs_s, false, buf, ret, SDW_SHIM2_INTEL_VS_ACTMCTL);

	if (sdw->link_res->mic_privacy) {
		ret += scnprintf(buf + ret, RD_BUF - ret, "\nVS PVCCS\n");
		ret += intel_sprintf(vs_s, false, buf, ret,
				     SDW_SHIM2_INTEL_VS_PVCCS);
	}

	seq_printf(s_file, "%s", buf);
	kfree(buf);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(intel_reg);

static int intel_set_m_datamode(void *data, u64 value)
{
	struct sdw_intel *sdw = data;
	struct sdw_bus *bus = &sdw->cdns.bus;

	if (value > SDW_PORT_DATA_MODE_STATIC_1)
		return -EINVAL;

	/* Userspace changed the hardware state behind the kernel's back */
	add_taint(TAINT_USER, LOCKDEP_STILL_OK);

	bus->params.m_data_mode = value;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(intel_set_m_datamode_fops, NULL,
			 intel_set_m_datamode, "%llu\n");

static int intel_set_s_datamode(void *data, u64 value)
{
	struct sdw_intel *sdw = data;
	struct sdw_bus *bus = &sdw->cdns.bus;

	if (value > SDW_PORT_DATA_MODE_STATIC_1)
		return -EINVAL;

	/* Userspace changed the hardware state behind the kernel's back */
	add_taint(TAINT_USER, LOCKDEP_STILL_OK);

	bus->params.s_data_mode = value;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(intel_set_s_datamode_fops, NULL,
			 intel_set_s_datamode, "%llu\n");

void intel_ace2x_debugfs_init(struct sdw_intel *sdw)
{
	struct dentry *root = sdw->cdns.bus.debugfs;

	if (!root)
		return;

	sdw->debugfs = debugfs_create_dir("intel-sdw", root);

	debugfs_create_file("intel-registers", 0400, sdw->debugfs, sdw,
			    &intel_reg_fops);

	debugfs_create_file("intel-m-datamode", 0200, sdw->debugfs, sdw,
			    &intel_set_m_datamode_fops);

	debugfs_create_file("intel-s-datamode", 0200, sdw->debugfs, sdw,
			    &intel_set_s_datamode_fops);

	sdw_cdns_debugfs_init(&sdw->cdns, sdw->debugfs);
}

void intel_ace2x_debugfs_exit(struct sdw_intel *sdw)
{
	debugfs_remove_recursive(sdw->debugfs);
}
#endif /* CONFIG_DEBUG_FS */
