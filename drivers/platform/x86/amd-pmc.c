// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD SoC Power Management Controller Driver
 *
 * Copyright (c) 2020, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/suspend.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

/* SMU communication registers */
#define AMD_PMC_REGISTER_MESSAGE	0x538
#define AMD_PMC_REGISTER_RESPONSE	0x980
#define AMD_PMC_REGISTER_ARGUMENT	0x9BC

/* PMC Scratch Registers */
#define AMD_PMC_SCRATCH_REG_CZN		0x94
#define AMD_PMC_SCRATCH_REG_YC		0xD14

/* Base address of SMU for mapping physical address to virtual address */
#define AMD_PMC_SMU_INDEX_ADDRESS	0xB8
#define AMD_PMC_SMU_INDEX_DATA		0xBC
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
/* List of supported CPU ids */
#define AMD_CPU_ID_RV			0x15D0
#define AMD_CPU_ID_RN			0x1630
#define AMD_CPU_ID_PCO			AMD_CPU_ID_RV
#define AMD_CPU_ID_CZN			AMD_CPU_ID_RN
#define AMD_CPU_ID_YC			0x14B5

#define PMC_MSG_DELAY_MIN_US		50
#define RESPONSE_REGISTER_LOOP_MAX	20000

#define SOC_SUBSYSTEM_IP_MAX	12
#define DELAY_MIN_US		2000
#define DELAY_MAX_US		3000
enum amd_pmc_def {
	MSG_TEST = 0x01,
	MSG_OS_HINT_PCO,
	MSG_OS_HINT_RN,
};

struct amd_pmc_bit_map {
	const char *name;
	u32 bit_mask;
};

static const struct amd_pmc_bit_map soc15_ip_blk[] = {
	{"DISPLAY",	BIT(0)},
	{"CPU",		BIT(1)},
	{"GFX",		BIT(2)},
	{"VDD",		BIT(3)},
	{"ACP",		BIT(4)},
	{"VCN",		BIT(5)},
	{"ISP",		BIT(6)},
	{"NBIO",	BIT(7)},
	{"DF",		BIT(8)},
	{"USB0",	BIT(9)},
	{"USB1",	BIT(10)},
	{"LAPIC",	BIT(11)},
	{}
};

struct amd_pmc_dev {
	void __iomem *regbase;
	void __iomem *smu_virt_addr;
	void __iomem *fch_virt_addr;
	u32 base_addr;
	u32 cpu_id;
	u32 active_ips;
/* SMU version information */
	u16 major;
	u16 minor;
	u16 rev;
	struct device *dev;
	struct mutex lock; /* generic mutex lock */
#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry *dbgfs_dir;
#endif /* CONFIG_DEBUG_FS */
};

static struct amd_pmc_dev pmc;
static int amd_pmc_send_cmd(struct amd_pmc_dev *dev, u32 arg, u32 *data, u8 msg, bool ret);

static inline u32 amd_pmc_reg_read(struct amd_pmc_dev *dev, int reg_offset)
{
	return ioread32(dev->regbase + reg_offset);
}

static inline void amd_pmc_reg_write(struct amd_pmc_dev *dev, int reg_offset, u32 val)
{
	iowrite32(val, dev->regbase + reg_offset);
}

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
	u64 timecondition_notmet_lastcapture[SOC_SUBSYSTEM_IP_MAX];
	u64 timecondition_notmet_totaltime[SOC_SUBSYSTEM_IP_MAX];
} __packed;

static int amd_pmc_get_smu_version(struct amd_pmc_dev *dev)
{
	int rc;
	u32 val;

	rc = amd_pmc_send_cmd(dev, 0, &val, SMU_MSG_GETSMUVERSION, 1);
	if (rc)
		return rc;

	dev->major = (val >> 16) & GENMASK(15, 0);
	dev->minor = (val >> 8) & GENMASK(7, 0);
	dev->rev = (val >> 0) & GENMASK(7, 0);

	dev_dbg(dev->dev, "SMU version is %u.%u.%u\n", dev->major, dev->minor, dev->rev);

	return 0;
}

static int amd_pmc_idlemask_read(struct amd_pmc_dev *pdev, struct device *dev,
				 struct seq_file *s)
{
	u32 val;

	switch (pdev->cpu_id) {
	case AMD_CPU_ID_CZN:
		val = amd_pmc_reg_read(pdev, AMD_PMC_SCRATCH_REG_CZN);
		break;
	case AMD_CPU_ID_YC:
		val = amd_pmc_reg_read(pdev, AMD_PMC_SCRATCH_REG_YC);
		break;
	default:
		return -EINVAL;
	}

	if (dev)
		dev_dbg(pdev->dev, "SMU idlemask s0i3: 0x%x\n", val);

	if (s)
		seq_printf(s, "SMU idlemask : 0x%x\n", val);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static int smu_fw_info_show(struct seq_file *s, void *unused)
{
	struct amd_pmc_dev *dev = s->private;
	struct smu_metrics table;
	int idx;

	if (dev->cpu_id == AMD_CPU_ID_PCO)
		return -EINVAL;

	memcpy_fromio(&table, dev->smu_virt_addr, sizeof(struct smu_metrics));

	seq_puts(s, "\n=== SMU Statistics ===\n");
	seq_printf(s, "Table Version: %d\n", table.table_version);
	seq_printf(s, "Hint Count: %d\n", table.hint_count);
	seq_printf(s, "Last S0i3 Status: %s\n", table.s0i3_last_entry_status ? "Success" :
		   "Unknown/Fail");
	seq_printf(s, "Time (in us) to S0i3: %lld\n", table.timeentering_s0i3_lastcapture);
	seq_printf(s, "Time (in us) in S0i3: %lld\n", table.timein_s0i3_lastcapture);
	seq_printf(s, "Time (in us) to resume from S0i3: %lld\n",
		   table.timeto_resume_to_os_lastcapture);

	seq_puts(s, "\n=== Active time (in us) ===\n");
	for (idx = 0 ; idx < SOC_SUBSYSTEM_IP_MAX ; idx++) {
		if (soc15_ip_blk[idx].bit_mask & dev->active_ips)
			seq_printf(s, "%-8s : %lld\n", soc15_ip_blk[idx].name,
				   table.timecondition_notmet_lastcapture[idx]);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(smu_fw_info);

static int s0ix_stats_show(struct seq_file *s, void *unused)
{
	struct amd_pmc_dev *dev = s->private;
	u64 entry_time, exit_time, residency;

	entry_time = ioread32(dev->fch_virt_addr + FCH_S0I3_ENTRY_TIME_H_OFFSET);
	entry_time = entry_time << 32 | ioread32(dev->fch_virt_addr + FCH_S0I3_ENTRY_TIME_L_OFFSET);

	exit_time = ioread32(dev->fch_virt_addr + FCH_S0I3_EXIT_TIME_H_OFFSET);
	exit_time = exit_time << 32 | ioread32(dev->fch_virt_addr + FCH_S0I3_EXIT_TIME_L_OFFSET);

	/* It's in 48MHz. We need to convert it */
	residency = exit_time - entry_time;
	do_div(residency, 48);

	seq_puts(s, "=== S0ix statistics ===\n");
	seq_printf(s, "S0ix Entry Time: %lld\n", entry_time);
	seq_printf(s, "S0ix Exit Time: %lld\n", exit_time);
	seq_printf(s, "Residency Time: %lld\n", residency);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(s0ix_stats);

static int amd_pmc_idlemask_show(struct seq_file *s, void *unused)
{
	struct amd_pmc_dev *dev = s->private;
	int rc;

	if (dev->major > 56 || (dev->major >= 55 && dev->minor >= 37)) {
		rc = amd_pmc_idlemask_read(dev, NULL, s);
		if (rc)
			return rc;
	} else {
		seq_puts(s, "Unsupported SMU version for Idlemask\n");
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(amd_pmc_idlemask);

static void amd_pmc_dbgfs_unregister(struct amd_pmc_dev *dev)
{
	debugfs_remove_recursive(dev->dbgfs_dir);
}

static void amd_pmc_dbgfs_register(struct amd_pmc_dev *dev)
{
	dev->dbgfs_dir = debugfs_create_dir("amd_pmc", NULL);
	debugfs_create_file("smu_fw_info", 0644, dev->dbgfs_dir, dev,
			    &smu_fw_info_fops);
	debugfs_create_file("s0ix_stats", 0644, dev->dbgfs_dir, dev,
			    &s0ix_stats_fops);
	debugfs_create_file("amd_pmc_idlemask", 0644, dev->dbgfs_dir, dev,
			    &amd_pmc_idlemask_fops);
}
#else
static inline void amd_pmc_dbgfs_register(struct amd_pmc_dev *dev)
{
}

static inline void amd_pmc_dbgfs_unregister(struct amd_pmc_dev *dev)
{
}
#endif /* CONFIG_DEBUG_FS */

static int amd_pmc_setup_smu_logging(struct amd_pmc_dev *dev)
{
	u32 phys_addr_low, phys_addr_hi;
	u64 smu_phys_addr;

	if (dev->cpu_id == AMD_CPU_ID_PCO)
		return -EINVAL;

	/* Get Active devices list from SMU */
	amd_pmc_send_cmd(dev, 0, &dev->active_ips, SMU_MSG_GET_SUP_CONSTRAINTS, 1);

	/* Get dram address */
	amd_pmc_send_cmd(dev, 0, &phys_addr_low, SMU_MSG_LOG_GETDRAM_ADDR_LO, 1);
	amd_pmc_send_cmd(dev, 0, &phys_addr_hi, SMU_MSG_LOG_GETDRAM_ADDR_HI, 1);
	smu_phys_addr = ((u64)phys_addr_hi << 32 | phys_addr_low);

	dev->smu_virt_addr = devm_ioremap(dev->dev, smu_phys_addr, sizeof(struct smu_metrics));
	if (!dev->smu_virt_addr)
		return -ENOMEM;

	/* Start the logging */
	amd_pmc_send_cmd(dev, 0, NULL, SMU_MSG_LOG_START, 0);

	return 0;
}

static void amd_pmc_dump_registers(struct amd_pmc_dev *dev)
{
	u32 value;

	value = amd_pmc_reg_read(dev, AMD_PMC_REGISTER_RESPONSE);
	dev_dbg(dev->dev, "AMD_PMC_REGISTER_RESPONSE:%x\n", value);

	value = amd_pmc_reg_read(dev, AMD_PMC_REGISTER_ARGUMENT);
	dev_dbg(dev->dev, "AMD_PMC_REGISTER_ARGUMENT:%x\n", value);

	value = amd_pmc_reg_read(dev, AMD_PMC_REGISTER_MESSAGE);
	dev_dbg(dev->dev, "AMD_PMC_REGISTER_MESSAGE:%x\n", value);
}

static int amd_pmc_send_cmd(struct amd_pmc_dev *dev, u32 arg, u32 *data, u8 msg, bool ret)
{
	int rc;
	u32 val;

	mutex_lock(&dev->lock);
	/* Wait until we get a valid response */
	rc = readx_poll_timeout(ioread32, dev->regbase + AMD_PMC_REGISTER_RESPONSE,
				val, val != 0, PMC_MSG_DELAY_MIN_US,
				PMC_MSG_DELAY_MIN_US * RESPONSE_REGISTER_LOOP_MAX);
	if (rc) {
		dev_err(dev->dev, "failed to talk to SMU\n");
		goto out_unlock;
	}

	/* Write zero to response register */
	amd_pmc_reg_write(dev, AMD_PMC_REGISTER_RESPONSE, 0);

	/* Write argument into response register */
	amd_pmc_reg_write(dev, AMD_PMC_REGISTER_ARGUMENT, arg);

	/* Write message ID to message ID register */
	amd_pmc_reg_write(dev, AMD_PMC_REGISTER_MESSAGE, msg);

	/* Wait until we get a valid response */
	rc = readx_poll_timeout(ioread32, dev->regbase + AMD_PMC_REGISTER_RESPONSE,
				val, val != 0, PMC_MSG_DELAY_MIN_US,
				PMC_MSG_DELAY_MIN_US * RESPONSE_REGISTER_LOOP_MAX);
	if (rc) {
		dev_err(dev->dev, "SMU response timed out\n");
		goto out_unlock;
	}

	switch (val) {
	case AMD_PMC_RESULT_OK:
		if (ret) {
			/* PMFW may take longer time to return back the data */
			usleep_range(DELAY_MIN_US, 10 * DELAY_MAX_US);
			*data = amd_pmc_reg_read(dev, AMD_PMC_REGISTER_ARGUMENT);
		}
		break;
	case AMD_PMC_RESULT_CMD_REJECT_BUSY:
		dev_err(dev->dev, "SMU not ready. err: 0x%x\n", val);
		rc = -EBUSY;
		goto out_unlock;
	case AMD_PMC_RESULT_CMD_UNKNOWN:
		dev_err(dev->dev, "SMU cmd unknown. err: 0x%x\n", val);
		rc = -EINVAL;
		goto out_unlock;
	case AMD_PMC_RESULT_CMD_REJECT_PREREQ:
	case AMD_PMC_RESULT_FAILED:
	default:
		dev_err(dev->dev, "SMU cmd failed. err: 0x%x\n", val);
		rc = -EIO;
		goto out_unlock;
	}

out_unlock:
	mutex_unlock(&dev->lock);
	amd_pmc_dump_registers(dev);
	return rc;
}

static int amd_pmc_get_os_hint(struct amd_pmc_dev *dev)
{
	switch (dev->cpu_id) {
	case AMD_CPU_ID_PCO:
		return MSG_OS_HINT_PCO;
	case AMD_CPU_ID_RN:
	case AMD_CPU_ID_YC:
		return MSG_OS_HINT_RN;
	}
	return -EINVAL;
}

static int amd_pmc_verify_czn_rtc(struct amd_pmc_dev *pdev, u32 *arg)
{
	struct rtc_device *rtc_device;
	time64_t then, now, duration;
	struct rtc_wkalrm alarm;
	struct rtc_time tm;
	int rc;

	if (pdev->major < 64 || (pdev->major == 64 && pdev->minor < 53))
		return 0;

	rtc_device = rtc_class_open("rtc0");
	if (!rtc_device)
		return 0;
	rc = rtc_read_alarm(rtc_device, &alarm);
	if (rc)
		return rc;
	if (!alarm.enabled) {
		dev_dbg(pdev->dev, "alarm not enabled\n");
		return 0;
	}
	rc = rtc_read_time(rtc_device, &tm);
	if (rc)
		return rc;
	then = rtc_tm_to_time64(&alarm.time);
	now = rtc_tm_to_time64(&tm);
	duration = then-now;

	/* in the past */
	if (then < now)
		return 0;

	/* will be stored in upper 16 bits of s0i3 hint argument,
	 * so timer wakeup from s0i3 is limited to ~18 hours or less
	 */
	if (duration <= 4 || duration > U16_MAX)
		return -EINVAL;

	*arg |= (duration << 16);
	rc = rtc_alarm_irq_enable(rtc_device, 0);
	dev_dbg(pdev->dev, "wakeup timer programmed for %lld seconds\n", duration);

	return rc;
}

static int __maybe_unused amd_pmc_suspend(struct device *dev)
{
	struct amd_pmc_dev *pdev = dev_get_drvdata(dev);
	int rc;
	u8 msg;
	u32 arg = 1;

	/* Reset and Start SMU logging - to monitor the s0i3 stats */
	amd_pmc_send_cmd(pdev, 0, NULL, SMU_MSG_LOG_RESET, 0);
	amd_pmc_send_cmd(pdev, 0, NULL, SMU_MSG_LOG_START, 0);

	/* Activate CZN specific RTC functionality */
	if (pdev->cpu_id == AMD_CPU_ID_CZN) {
		rc = amd_pmc_verify_czn_rtc(pdev, &arg);
		if (rc < 0)
			return rc;
	}

	/* Dump the IdleMask before we send hint to SMU */
	amd_pmc_idlemask_read(pdev, dev, NULL);
	msg = amd_pmc_get_os_hint(pdev);
	rc = amd_pmc_send_cmd(pdev, arg, NULL, msg, 0);
	if (rc)
		dev_err(pdev->dev, "suspend failed\n");

	return rc;
}

static int __maybe_unused amd_pmc_resume(struct device *dev)
{
	struct amd_pmc_dev *pdev = dev_get_drvdata(dev);
	int rc;
	u8 msg;

	msg = amd_pmc_get_os_hint(pdev);
	rc = amd_pmc_send_cmd(pdev, 0, NULL, msg, 0);
	if (rc)
		dev_err(pdev->dev, "resume failed\n");

	/* Let SMU know that we are looking for stats */
	amd_pmc_send_cmd(pdev, 0, NULL, SMU_MSG_LOG_DUMP_DATA, 0);

	/* Dump the IdleMask to see the blockers */
	amd_pmc_idlemask_read(pdev, dev, NULL);

	return 0;
}

static const struct dev_pm_ops amd_pmc_pm_ops = {
	.suspend_noirq = amd_pmc_suspend,
	.resume_noirq = amd_pmc_resume,
};

static const struct pci_device_id pmc_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, AMD_CPU_ID_YC) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, AMD_CPU_ID_CZN) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, AMD_CPU_ID_RN) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, AMD_CPU_ID_PCO) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, AMD_CPU_ID_RV) },
	{ }
};

static int amd_pmc_probe(struct platform_device *pdev)
{
	struct amd_pmc_dev *dev = &pmc;
	struct pci_dev *rdev;
	u32 base_addr_lo, base_addr_hi;
	u64 base_addr, fch_phys_addr;
	int err;
	u32 val;

	dev->dev = &pdev->dev;

	rdev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(0, 0));
	if (!rdev || !pci_match_id(pmc_pci_ids, rdev)) {
		pci_dev_put(rdev);
		return -ENODEV;
	}

	dev->cpu_id = rdev->device;
	err = pci_write_config_dword(rdev, AMD_PMC_SMU_INDEX_ADDRESS, AMD_PMC_BASE_ADDR_LO);
	if (err) {
		dev_err(dev->dev, "error writing to 0x%x\n", AMD_PMC_SMU_INDEX_ADDRESS);
		pci_dev_put(rdev);
		return pcibios_err_to_errno(err);
	}

	err = pci_read_config_dword(rdev, AMD_PMC_SMU_INDEX_DATA, &val);
	if (err) {
		pci_dev_put(rdev);
		return pcibios_err_to_errno(err);
	}

	base_addr_lo = val & AMD_PMC_BASE_ADDR_HI_MASK;

	err = pci_write_config_dword(rdev, AMD_PMC_SMU_INDEX_ADDRESS, AMD_PMC_BASE_ADDR_HI);
	if (err) {
		dev_err(dev->dev, "error writing to 0x%x\n", AMD_PMC_SMU_INDEX_ADDRESS);
		pci_dev_put(rdev);
		return pcibios_err_to_errno(err);
	}

	err = pci_read_config_dword(rdev, AMD_PMC_SMU_INDEX_DATA, &val);
	if (err) {
		pci_dev_put(rdev);
		return pcibios_err_to_errno(err);
	}

	base_addr_hi = val & AMD_PMC_BASE_ADDR_LO_MASK;
	pci_dev_put(rdev);
	base_addr = ((u64)base_addr_hi << 32 | base_addr_lo);

	dev->regbase = devm_ioremap(dev->dev, base_addr + AMD_PMC_BASE_ADDR_OFFSET,
				    AMD_PMC_MAPPING_SIZE);
	if (!dev->regbase)
		return -ENOMEM;

	mutex_init(&dev->lock);

	/* Use FCH registers to get the S0ix stats */
	base_addr_lo = FCH_BASE_PHY_ADDR_LOW;
	base_addr_hi = FCH_BASE_PHY_ADDR_HIGH;
	fch_phys_addr = ((u64)base_addr_hi << 32 | base_addr_lo);
	dev->fch_virt_addr = devm_ioremap(dev->dev, fch_phys_addr, FCH_SSC_MAPPING_SIZE);
	if (!dev->fch_virt_addr)
		return -ENOMEM;

	/* Use SMU to get the s0i3 debug stats */
	err = amd_pmc_setup_smu_logging(dev);
	if (err)
		dev_err(dev->dev, "SMU debugging info not supported on this platform\n");

	amd_pmc_get_smu_version(dev);
	platform_set_drvdata(pdev, dev);
	amd_pmc_dbgfs_register(dev);
	return 0;
}

static int amd_pmc_remove(struct platform_device *pdev)
{
	struct amd_pmc_dev *dev = platform_get_drvdata(pdev);

	amd_pmc_dbgfs_unregister(dev);
	mutex_destroy(&dev->lock);
	return 0;
}

static const struct acpi_device_id amd_pmc_acpi_ids[] = {
	{"AMDI0005", 0},
	{"AMDI0006", 0},
	{"AMDI0007", 0},
	{"AMD0004", 0},
	{"AMD0005", 0},
	{ }
};
MODULE_DEVICE_TABLE(acpi, amd_pmc_acpi_ids);

static struct platform_driver amd_pmc_driver = {
	.driver = {
		.name = "amd_pmc",
		.acpi_match_table = amd_pmc_acpi_ids,
		.pm = &amd_pmc_pm_ops,
	},
	.probe = amd_pmc_probe,
	.remove = amd_pmc_remove,
};
module_platform_driver(amd_pmc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("AMD PMC Driver");
