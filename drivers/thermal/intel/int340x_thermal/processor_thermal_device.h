/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * processor_thermal_device.h
 * Copyright (c) 2020, Intel Corporation.
 */

#ifndef __PROCESSOR_THERMAL_DEVICE_H__
#define __PROCESSOR_THERMAL_DEVICE_H__

#include <linux/intel_rapl.h>

#define PCI_DEVICE_ID_INTEL_ADL_THERMAL	0x461d
#define PCI_DEVICE_ID_INTEL_BDW_THERMAL	0x1603
#define PCI_DEVICE_ID_INTEL_BSW_THERMAL	0x22DC

#define PCI_DEVICE_ID_INTEL_BXT0_THERMAL	0x0A8C
#define PCI_DEVICE_ID_INTEL_BXT1_THERMAL	0x1A8C
#define PCI_DEVICE_ID_INTEL_BXTX_THERMAL	0x4A8C
#define PCI_DEVICE_ID_INTEL_BXTP_THERMAL	0x5A8C

#define PCI_DEVICE_ID_INTEL_CNL_THERMAL	0x5a03
#define PCI_DEVICE_ID_INTEL_CFL_THERMAL	0x3E83
#define PCI_DEVICE_ID_INTEL_GLK_THERMAL	0x318C
#define PCI_DEVICE_ID_INTEL_HSB_THERMAL	0x0A03
#define PCI_DEVICE_ID_INTEL_ICL_THERMAL	0x8a03
#define PCI_DEVICE_ID_INTEL_JSL_THERMAL	0x4E03
#define PCI_DEVICE_ID_INTEL_SKL_THERMAL	0x1903
#define PCI_DEVICE_ID_INTEL_TGL_THERMAL	0x9A03

struct power_config {
	u32	index;
	u32	min_uw;
	u32	max_uw;
	u32	tmin_us;
	u32	tmax_us;
	u32	step_uw;
};

struct proc_thermal_device {
	struct device *dev;
	struct acpi_device *adev;
	struct power_config power_limits[2];
	struct int34x_thermal_zone *int340x_zone;
	struct intel_soc_dts_sensors *soc_dts;
	u32 mmio_feature_mask;
	void __iomem *mmio_base;
	void *priv_data;
};

struct rapl_mmio_regs {
	u64 reg_unit;
	u64 regs[RAPL_DOMAIN_MAX][RAPL_DOMAIN_REG_MAX];
	int limits[RAPL_DOMAIN_MAX];
};

#define PROC_THERMAL_FEATURE_NONE	0x00
#define PROC_THERMAL_FEATURE_RAPL	0x01
#define PROC_THERMAL_FEATURE_FIVR	0x02
#define PROC_THERMAL_FEATURE_DVFS	0x04
#define PROC_THERMAL_FEATURE_MBOX	0x08

#if IS_ENABLED(CONFIG_PROC_THERMAL_MMIO_RAPL)
int proc_thermal_rapl_add(struct pci_dev *pdev, struct proc_thermal_device *proc_priv);
void proc_thermal_rapl_remove(void);
#else
static int __maybe_unused proc_thermal_rapl_add(struct pci_dev *pdev,
						struct proc_thermal_device *proc_priv)
{
	return 0;
}

static void __maybe_unused proc_thermal_rapl_remove(void)
{
}
#endif

int proc_thermal_rfim_add(struct pci_dev *pdev, struct proc_thermal_device *proc_priv);
void proc_thermal_rfim_remove(struct pci_dev *pdev);

int proc_thermal_mbox_add(struct pci_dev *pdev, struct proc_thermal_device *proc_priv);
void proc_thermal_mbox_remove(struct pci_dev *pdev);

int processor_thermal_send_mbox_cmd(struct pci_dev *pdev, u16 cmd_id, u32 cmd_data, u64 *cmd_resp);
int proc_thermal_add(struct device *dev, struct proc_thermal_device *priv);
void proc_thermal_remove(struct proc_thermal_device *proc_priv);
int proc_thermal_suspend(struct device *dev);
int proc_thermal_resume(struct device *dev);
int proc_thermal_mmio_add(struct pci_dev *pdev,
			  struct proc_thermal_device *proc_priv,
			  kernel_ulong_t feature_mask);
void proc_thermal_mmio_remove(struct pci_dev *pdev, struct proc_thermal_device *proc_priv);
#endif
