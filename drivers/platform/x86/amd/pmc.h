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
};

void amd_pmc_process_restore_quirks(struct amd_pmc_dev *dev);
void amd_pmc_quirks_init(struct amd_pmc_dev *dev);

#endif /* PMC_H */
