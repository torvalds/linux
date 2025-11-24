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

/* SMU communication registers */
#define AMD_PMC_REGISTER_RESPONSE	0x980
#define AMD_PMC_REGISTER_ARGUMENT	0x9BC

/* PMC Scratch Registers */
#define AMD_PMC_SCRATCH_REG_CZN		0x94
#define AMD_PMC_SCRATCH_REG_YC		0xD14
#define AMD_PMC_SCRATCH_REG_1AH		0xF14

/* STB Registers */
#define AMD_PMC_STB_S2IDLE_PREPARE	0xC6000001
#define AMD_PMC_STB_S2IDLE_RESTORE	0xC6000002
#define AMD_PMC_STB_S2IDLE_CHECK	0xC6000003

/* Base address of SMU for mapping physical address to virtual address */
#define AMD_PMC_MAPPING_SIZE		0x01000
#define AMD_PMC_BASE_ADDR_OFFSET	0x10000
#define AMD_PMC_BASE_ADDR_LO		0x13B102E8
#define AMD_PMC_BASE_ADDR_HI		0x13B102EC
#define AMD_PMC_BASE_ADDR_LO_MASK	GENMASK(15, 0)
#define AMD_PMC_BASE_ADDR_HI_MASK	GENMASK(31, 20)

/* SMU Response Codes */
#define AMD_PMC_RESULT_OK                    0x01
#define AMD_PMC_RESULT_CMD_REJECT_BUSY       0xFC
#define AMD_PMC_RESULT_CMD_REJECT_PREREQ     0xFD
#define AMD_PMC_RESULT_CMD_UNKNOWN           0xFE
#define AMD_PMC_RESULT_FAILED                0xFF

/* FCH SSC Registers */
#define FCH_S0I3_ENTRY_TIME_L_OFFSET	0x30
#define FCH_S0I3_ENTRY_TIME_H_OFFSET	0x34
#define FCH_S0I3_EXIT_TIME_L_OFFSET	0x38
#define FCH_S0I3_EXIT_TIME_H_OFFSET	0x3C
#define FCH_SSC_MAPPING_SIZE		0x800
#define FCH_BASE_PHY_ADDR_LOW		0xFED81100
#define FCH_BASE_PHY_ADDR_HIGH		0x00000000

/* SMU Message Definations */
#define SMU_MSG_GETSMUVERSION		0x02
#define SMU_MSG_LOG_GETDRAM_ADDR_HI	0x04
#define SMU_MSG_LOG_GETDRAM_ADDR_LO	0x05
#define SMU_MSG_LOG_START		0x06
#define SMU_MSG_LOG_RESET		0x07
#define SMU_MSG_LOG_DUMP_DATA		0x08
#define SMU_MSG_GET_SUP_CONSTRAINTS	0x09

#define PMC_MSG_DELAY_MIN_US		50
#define RESPONSE_REGISTER_LOOP_MAX	20000

#define DELAY_MIN_US		2000
#define DELAY_MAX_US		3000

enum s2d_msg_port {
	MSG_PORT_PMC,
	MSG_PORT_S2D,
};

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

struct stb_arg {
	u32 s2d_msg_id;
	u32 msg;
	u32 arg;
	u32 resp;
};

struct amd_pmc_dev {
	void __iomem *regbase;
	void __iomem *smu_virt_addr;
	void __iomem *stb_virt_addr;
	void __iomem *fch_virt_addr;
	u32 base_addr;
	u32 cpu_id;
	u32 dram_size;
	u32 active_ips;
	const struct amd_pmc_bit_map *ips_ptr;
	u32 num_ips;
	u32 smu_msg;
/* SMU version information */
	u8 smu_program;
	u8 major;
	u8 minor;
	u8 rev;
	u8 msg_port;
	struct device *dev;
	struct pci_dev *rdev;
	struct mutex lock; /* generic mutex lock */
	struct dentry *dbgfs_dir;
	struct quirk_entry *quirks;
	bool disable_8042_wakeup;
	struct amd_mp2_dev *mp2;
	struct stb_arg stb_arg;
};

struct amd_pmc_bit_map {
	const char *name;
	u32 bit_mask;
};

struct smu_metrics {
	u32 table_version;
	u32 hint_count;
	u32 s0i3_last_entry_status;
	u32 timein_s0i2;
	u64 timeentering_s0i3_lastcapture;
	u64 timeentering_s0i3_totaltime;
	u64 timeto_resume_to_os_lastcapture;
	u64 timeto_resume_to_os_totaltime;
	u64 timein_s0i3_lastcapture;
	u64 timein_s0i3_totaltime;
	u64 timein_swdrips_lastcapture;
	u64 timein_swdrips_totaltime;
	u64 timecondition_notmet_lastcapture[32];
	u64 timecondition_notmet_totaltime[32];
} __packed;

enum amd_pmc_def {
	MSG_TEST = 0x01,
	MSG_OS_HINT_PCO,
	MSG_OS_HINT_RN,
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
#define AMD_CPU_ID_VG			0x1645
#define AMD_CPU_ID_YC			0x14B5
#define AMD_CPU_ID_CB			0x14D8
#define AMD_CPU_ID_PS			0x14E8
#define AMD_CPU_ID_SP			0x14A4
#define AMD_CPU_ID_SHP			0x153A
#define PCI_DEVICE_ID_AMD_1AH_M20H_ROOT 0x1507
#define PCI_DEVICE_ID_AMD_1AH_M60H_ROOT 0x1122
#define PCI_DEVICE_ID_AMD_MP2_STB	0x172c

int amd_stb_s2d_init(struct amd_pmc_dev *dev);
int amd_stb_read(struct amd_pmc_dev *dev, u32 *buf);
int amd_stb_write(struct amd_pmc_dev *dev, u32 data);
int amd_pmc_send_cmd(struct amd_pmc_dev *dev, u32 arg, u32 *data, u8 msg, bool ret);

#endif /* PMC_H */
