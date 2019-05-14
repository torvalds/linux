/*
 * Intel SOC Punit device state debug driver
 * Punit controls power management for North Complex devices (Graphics
 * blocks, Image Signal Processing, video processing, display, DSP etc.)
 *
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/io.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/iosf_mbi.h>

/* Subsystem config/status Video processor */
#define VED_SS_PM0		0x32
/* Subsystem config/status ISP (Image Signal Processor) */
#define ISP_SS_PM0		0x39
/* Subsystem config/status Input/output controller */
#define MIO_SS_PM		0x3B
/* Shift bits for getting status for video, isp and i/o */
#define SSS_SHIFT		24

/* Power gate status reg */
#define PWRGT_STATUS		0x61
/* Shift bits for getting status for graphics rendering */
#define RENDER_POS		0
/* Shift bits for getting status for media control */
#define MEDIA_POS		2
/* Shift bits for getting status for Valley View/Baytrail display */
#define VLV_DISPLAY_POS		6

/* Subsystem config/status display for Cherry Trail SOC */
#define CHT_DSP_SSS		0x36
/* Shift bits for getting status for display */
#define CHT_DSP_SSS_POS		16

struct punit_device {
	char *name;
	int reg;
	int sss_pos;
};

static const struct punit_device punit_device_tng[] = {
	{ "DISPLAY",	CHT_DSP_SSS,	SSS_SHIFT },
	{ "VED",	VED_SS_PM0,	SSS_SHIFT },
	{ "ISP",	ISP_SS_PM0,	SSS_SHIFT },
	{ "MIO",	MIO_SS_PM,	SSS_SHIFT },
	{ NULL }
};

static const struct punit_device punit_device_byt[] = {
	{ "GFX RENDER",	PWRGT_STATUS,	RENDER_POS },
	{ "GFX MEDIA",	PWRGT_STATUS,	MEDIA_POS },
	{ "DISPLAY",	PWRGT_STATUS,	VLV_DISPLAY_POS },
	{ "VED",	VED_SS_PM0,	SSS_SHIFT },
	{ "ISP",	ISP_SS_PM0,	SSS_SHIFT },
	{ "MIO",	MIO_SS_PM,	SSS_SHIFT },
	{ NULL }
};

static const struct punit_device punit_device_cht[] = {
	{ "GFX RENDER",	PWRGT_STATUS,	RENDER_POS },
	{ "GFX MEDIA",	PWRGT_STATUS,	MEDIA_POS },
	{ "DISPLAY",	CHT_DSP_SSS,	CHT_DSP_SSS_POS },
	{ "VED",	VED_SS_PM0,	SSS_SHIFT },
	{ "ISP",	ISP_SS_PM0,	SSS_SHIFT },
	{ "MIO",	MIO_SS_PM,	SSS_SHIFT },
	{ NULL }
};

static const char * const dstates[] = {"D0", "D0i1", "D0i2", "D0i3"};

static int punit_dev_state_show(struct seq_file *seq_file, void *unused)
{
	u32 punit_pwr_status;
	struct punit_device *punit_devp = seq_file->private;
	int index;
	int status;

	seq_puts(seq_file, "\n\nPUNIT NORTH COMPLEX DEVICES :\n");
	while (punit_devp->name) {
		status = iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_REG_READ,
				       punit_devp->reg, &punit_pwr_status);
		if (status) {
			seq_printf(seq_file, "%9s : Read Failed\n",
				   punit_devp->name);
		} else  {
			index = (punit_pwr_status >> punit_devp->sss_pos) & 3;
			seq_printf(seq_file, "%9s : %s\n", punit_devp->name,
				   dstates[index]);
		}
		punit_devp++;
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(punit_dev_state);

static struct dentry *punit_dbg_file;

static int punit_dbgfs_register(struct punit_device *punit_device)
{
	static struct dentry *dev_state;

	punit_dbg_file = debugfs_create_dir("punit_atom", NULL);
	if (!punit_dbg_file)
		return -ENXIO;

	dev_state = debugfs_create_file("dev_power_state", 0444,
					punit_dbg_file, punit_device,
					&punit_dev_state_fops);
	if (!dev_state) {
		pr_err("punit_dev_state register failed\n");
		debugfs_remove(punit_dbg_file);
		return -ENXIO;
	}

	return 0;
}

static void punit_dbgfs_unregister(void)
{
	debugfs_remove_recursive(punit_dbg_file);
}

#define ICPU(model, drv_data) \
	{ X86_VENDOR_INTEL, 6, model, X86_FEATURE_MWAIT,\
	  (kernel_ulong_t)&drv_data }

static const struct x86_cpu_id intel_punit_cpu_ids[] = {
	ICPU(INTEL_FAM6_ATOM_SILVERMONT, punit_device_byt),
	ICPU(INTEL_FAM6_ATOM_SILVERMONT_MID,  punit_device_tng),
	ICPU(INTEL_FAM6_ATOM_AIRMONT,	  punit_device_cht),
	{}
};

MODULE_DEVICE_TABLE(x86cpu, intel_punit_cpu_ids);

static int __init punit_atom_debug_init(void)
{
	const struct x86_cpu_id *id;
	int ret;

	id = x86_match_cpu(intel_punit_cpu_ids);
	if (!id)
		return -ENODEV;

	ret = punit_dbgfs_register((struct punit_device *)id->driver_data);
	if (ret < 0)
		return ret;

	return 0;
}

static void __exit punit_atom_debug_exit(void)
{
	punit_dbgfs_unregister();
}

module_init(punit_atom_debug_init);
module_exit(punit_atom_debug_exit);

MODULE_AUTHOR("Kumar P, Mahesh <mahesh.kumar.p@intel.com>");
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_DESCRIPTION("Driver for Punit devices states debugging");
MODULE_LICENSE("GPL v2");
