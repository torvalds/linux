/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AMD SoC Power Management Controller Driver
 *
 * Copyright (c) 2023, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Mario Limonciello <mario.limonciello@amd.com>
 */

#ifndef PMC_H
#define PMC_H

#include <linux/types.h>
#include <linux/mutex.h>

struct amd_mp2_dev {
	void __iomem *mmio;
	void __iomem *vslbase;
	void *stbdata;
	void *devres_gid;
	struct pci_dev *pdev;
	dma_addr_t dma_addr;
	int stb_len;
	bool is_stb_data;
};

struct amd_pmc_dev {
	void __iomem *regbase;
	void __iomem *smu_virt_addr;
	void __iomem *stb_virt_addr;
	void __iomem *fch_virt_addr;
	bool msg_port;
	u32 base_addr;
	u32 cpu_id;
	u32 active_ips;
	u32 dram_size;
	u32 num_ips;
	u32 s2d_msg_id;
	u32 smu_msg;
/* SMU version information */
	u8 smu_program;
	u8 major;
	u8 minor;
	u8 rev;
	struct device *dev;
	struct pci_dev *rdev;
	struct mutex lock; /* generic mutex lock */
	struct dentry *dbgfs_dir;
	struct quirk_entry *quirks;
	bool disable_8042_wakeup;
	struct amd_mp2_dev *mp2;
};

void amd_pmc_process_restore_quirks(struct amd_pmc_dev *dev);
void amd_pmc_quirks_init(struct amd_pmc_dev *dev);
void amd_mp2_stb_init(struct amd_pmc_dev *dev);
void amd_mp2_stb_deinit(struct amd_pmc_dev *dev);

/* List of supported CPU ids */
#define AMD_CPU_ID_RV			0x15D0
#define AMD_CPU_ID_RN			0x1630
#define AMD_CPU_ID_PCO			AMD_CPU_ID_RV
#define AMD_CPU_ID_CZN			AMD_CPU_ID_RN
#define AMD_CPU_ID_YC			0x14B5
#define AMD_CPU_ID_CB			0x14D8
#define AMD_CPU_ID_PS			0x14E8
#define AMD_CPU_ID_SP			0x14A4
#define PCI_DEVICE_ID_AMD_1AH_M20H_ROOT 0x1507
#define PCI_DEVICE_ID_AMD_MP2_STB	0x172c

#endif /* PMC_H */
