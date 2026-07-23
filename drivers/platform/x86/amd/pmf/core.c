// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD Platform Management Framework Driver
 *
 * Copyright (c) 2022, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 */

#include <linux/array_size.h>
#include <linux/cleanup.h>
#include <linux/debugfs.h>
#include <linux/dev_printk.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <asm/amd/node.h>
#include "pmf.h"

/* PMF-SMU communication registers */
#define AMD_PMF_REGISTER_MESSAGE	0xA18
#define AMD_PMF_REGISTER_RESPONSE	0xA78
#define AMD_PMF_REGISTER_ARGUMENT	0xA58

/* PMF-SMU communication registers for 1AH_M80H */
#define AMD_PMF_REGISTER_MESSAGE_V2	0xA04
#define AMD_PMF_REGISTER_RESPONSE_V2	0xA08
#define AMD_PMF_REGISTER_ARGUMENT0_V2	0xA0C
#define AMD_PMF_REGISTER_ARGUMENT1_V2	0xAAC
#define AMD_PMF_REGISTER_ARGUMENT2_V2	0xAB0

/* Base address of SMU for mapping physical address to virtual address */
#define AMD_PMF_MAPPING_SIZE		0x01000
#define AMD_PMF_BASE_ADDR_OFFSET	0x10000
#define AMD_PMF_BASE_ADDR_LO		0x13B102E8
#define AMD_PMF_BASE_ADDR_HI		0x13B102EC
#define AMD_PMF_BASE_ADDR_LO_MASK	GENMASK(15, 0)
#define AMD_PMF_BASE_ADDR_HI_MASK	GENMASK(31, 20)

/* SMU Response Codes */
#define AMD_PMF_RESULT_OK                    0x01
#define AMD_PMF_RESULT_CMD_REJECT_BUSY       0xFC
#define AMD_PMF_RESULT_CMD_REJECT_PREREQ     0xFD
#define AMD_PMF_RESULT_CMD_UNKNOWN           0xFE
#define AMD_PMF_RESULT_FAILED                0xFF

#define PMF_MSG_DELAY_MIN_US		50
#define RESPONSE_REGISTER_LOOP_MAX	20000

#define DELAY_MIN_US	2000
#define DELAY_MAX_US	3000

/* override Metrics Table sample size time (in ms) */
int metrics_table_loop_ms = 1000;
module_param(metrics_table_loop_ms, int, 0644);
MODULE_PARM_DESC(metrics_table_loop_ms, "Metrics Table sample size time (default = 1000ms)");

/* Force load on supported older platforms */
static bool force_load;
module_param(force_load, bool, 0444);
MODULE_PARM_DESC(force_load, "Force load this driver on supported older platforms (experimental)");

static bool smart_pc_support = true;
module_param(smart_pc_support, bool, 0444);
MODULE_PARM_DESC(smart_pc_support, "Smart PC Support (default = true)");

static bool amd_pmf_supports_accumulator_metrics(struct amd_pmf_dev *pdev)
{
	switch (pdev->cpu_id) {
	case PCI_DEVICE_ID_AMD_1AH_M80H_ROOT:
		return true;
	default:
		return false;
	}
}

static int amd_pmf_pwr_src_notify_call(struct notifier_block *nb, unsigned long event, void *data)
{
	struct amd_pmf_dev *pmf = container_of(nb, struct amd_pmf_dev, pwr_src_notifier);

	if (event != PSY_EVENT_PROP_CHANGED)
		return NOTIFY_OK;

	if (is_apmf_func_supported(pmf, APMF_FUNC_AUTO_MODE) ||
	    is_apmf_func_supported(pmf, APMF_FUNC_DYN_SLIDER_DC) ||
	    is_apmf_func_supported(pmf, APMF_FUNC_DYN_SLIDER_AC)) {
		if ((pmf->amt_enabled || pmf->cnqf_enabled) && is_pprof_balanced(pmf))
			return NOTIFY_DONE;
	}

	if (is_apmf_func_supported(pmf, APMF_FUNC_STATIC_SLIDER_GRANULAR))
		amd_pmf_set_sps_power_limits(pmf);

	if (is_apmf_func_supported(pmf, APMF_FUNC_OS_POWER_SLIDER_UPDATE))
		amd_pmf_power_slider_update_event(pmf);

	return NOTIFY_OK;
}

static int current_power_limits_show(struct seq_file *seq, void *unused)
{
	struct amd_pmf_dev *dev = seq->private;
	struct amd_pmf_static_slider_granular table;
	int mode, src = 0;

	mode = amd_pmf_get_pprof_modes(dev);
	if (mode < 0)
		return mode;

	src = amd_pmf_get_power_source();
	amd_pmf_update_slider(dev, SLIDER_OP_GET, mode, &table);
	seq_printf(seq, "spl:%u fppt:%u sppt:%u sppt_apu_only:%u stt_min:%u stt[APU]:%u stt[HS2]: %u\n",
		   table.prop[src][mode].spl,
		   table.prop[src][mode].fppt,
		   table.prop[src][mode].sppt,
		   table.prop[src][mode].sppt_apu_only,
		   table.prop[src][mode].stt_min,
		   table.prop[src][mode].stt_skin_temp[STT_TEMP_APU],
		   table.prop[src][mode].stt_skin_temp[STT_TEMP_HS2]);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(current_power_limits);

static void amd_pmf_dbgfs_unregister(struct amd_pmf_dev *dev)
{
	debugfs_remove_recursive(dev->dbgfs_dir);
}

static void amd_pmf_dbgfs_register(struct amd_pmf_dev *dev)
{
	dev->dbgfs_dir = debugfs_create_dir("amd_pmf", NULL);
	if (dev->pmf_if_version == PMF_IF_V1)
		debugfs_create_file("current_power_limits", 0644, dev->dbgfs_dir, dev,
				    &current_power_limits_fops);
}

int amd_pmf_get_power_source(void)
{
	if (power_supply_is_system_supplied() > 0)
		return POWER_SOURCE_AC;
	else
		return POWER_SOURCE_DC;
}

static inline u32 amd_pmf_reg_read(struct amd_pmf_dev *dev, int reg_offset)
{
	return ioread32(dev->regbase + reg_offset);
}

static inline void amd_pmf_reg_write(struct amd_pmf_dev *dev, int reg_offset, u32 val)
{
	iowrite32(val, dev->regbase + reg_offset);
}

static void __maybe_unused amd_pmf_dump_registers(struct amd_pmf_dev *dev)
{
	u32 value;

	value = amd_pmf_reg_read(dev, dev->smu_regs->resp_reg);
	dev_dbg(dev->dev, "AMD_PMF_REGISTER_RESPONSE:%x\n", value);

	value = amd_pmf_reg_read(dev, dev->smu_regs->arg_reg[0]);
	dev_dbg(dev->dev, "AMD_PMF_REGISTER_ARGUMENT:%d\n", value);
	if (amd_pmf_supports_accumulator_metrics(dev)) {
		value = amd_pmf_reg_read(dev, dev->smu_regs->arg_reg[1]);
		dev_dbg(dev->dev, "AMD_PMF_REGISTER_ARGUMENT1:%d\n", value);
		value = amd_pmf_reg_read(dev, dev->smu_regs->arg_reg[2]);
		dev_dbg(dev->dev, "AMD_PMF_REGISTER_ARGUMENT2:%d\n", value);
	}

	value = amd_pmf_reg_read(dev, dev->smu_regs->msg_reg);
	dev_dbg(dev->dev, "AMD_PMF_REGISTER_MESSAGE:%x\n", value);
}

/**
 * fixp_q88_fromint: Convert integer to Q8.8
 * @val: input value
 *
 * Converts an integer into binary fixed point format where 8 bits
 * are used for integer and 8 bits are used for the decimal.
 *
 * Return: unsigned integer converted to Q8.8 format
 */
u32 fixp_q88_fromint(u32 val)
{
	return val << 8;
}

int amd_pmf_send_cmd(struct amd_pmf_dev *dev, u8 message, bool get, u32 arg, u32 *data)
{
	int rc;
	u32 val;

	guard(mutex)(&dev->lock);

	/* Wait until we get a valid response */
	rc = readx_poll_timeout(ioread32, dev->regbase + dev->smu_regs->resp_reg,
				val, val != 0, PMF_MSG_DELAY_MIN_US,
				PMF_MSG_DELAY_MIN_US * RESPONSE_REGISTER_LOOP_MAX);
	if (rc) {
		dev_err(dev->dev, "failed to talk to SMU\n");
		return rc;
	}

	/* Write zero to response register */
	amd_pmf_reg_write(dev, dev->smu_regs->resp_reg, 0);

	/* Write argument into argument register */
	amd_pmf_reg_write(dev, dev->smu_regs->arg_reg[0], arg);

	/* Write message ID to message ID register */
	amd_pmf_reg_write(dev, dev->smu_regs->msg_reg, message);

	/* Wait until we get a valid response */
	rc = readx_poll_timeout(ioread32, dev->regbase + dev->smu_regs->resp_reg,
				val, val != 0, PMF_MSG_DELAY_MIN_US,
				PMF_MSG_DELAY_MIN_US * RESPONSE_REGISTER_LOOP_MAX);
	if (rc) {
		dev_err(dev->dev, "SMU response timed out\n");
		return rc;
	}

	switch (val) {
	case AMD_PMF_RESULT_OK:
		if (get) {
			/* PMFW may take longer time to return back the data */
			usleep_range(DELAY_MIN_US, 10 * DELAY_MAX_US);
			*data = amd_pmf_reg_read(dev, dev->smu_regs->arg_reg[0]);
			if (amd_pmf_supports_accumulator_metrics(dev) &&
			    message == GET_1AH_M80H_METRICS_TABLE_DRAM_ADDR) {
				dev->dram_addr.hi = amd_pmf_reg_read(dev,
								     dev->smu_regs->arg_reg[1]);
				dev->dram_addr.size = amd_pmf_reg_read(dev,
								       dev->smu_regs->arg_reg[2]);
			}
		}
		break;
	case AMD_PMF_RESULT_CMD_REJECT_BUSY:
		dev_err(dev->dev, "SMU not ready. err: 0x%x\n", val);
		rc = -EBUSY;
		break;
	case AMD_PMF_RESULT_CMD_UNKNOWN:
		dev_err(dev->dev, "SMU cmd unknown. err: 0x%x\n", val);
		rc = -EINVAL;
		break;
	case AMD_PMF_RESULT_CMD_REJECT_PREREQ:
	case AMD_PMF_RESULT_FAILED:
	default:
		dev_err(dev->dev, "SMU cmd failed. err: 0x%x\n", val);
		rc = -EIO;
		break;
	}

	amd_pmf_dump_registers(dev);
	return rc;
}

/* RMB, PS, 1AH_M20H and 1AH_M60H share the same v1 SMU mailbox registers */
static const struct amd_pmf_smu_regs amd_pmf_smu_regs_v1 = {
	.msg_reg	= AMD_PMF_REGISTER_MESSAGE,
	.resp_reg	= AMD_PMF_REGISTER_RESPONSE,
	.arg_reg	= { AMD_PMF_REGISTER_ARGUMENT, 0, 0 },
};

/* 1AH_M80H uses an extended mailbox with three argument registers */
static const struct amd_pmf_smu_regs amd_pmf_smu_regs_v2 = {
	.msg_reg	= AMD_PMF_REGISTER_MESSAGE_V2,
	.resp_reg	= AMD_PMF_REGISTER_RESPONSE_V2,
	.arg_reg	= {
		AMD_PMF_REGISTER_ARGUMENT0_V2,
		AMD_PMF_REGISTER_ARGUMENT1_V2,
		AMD_PMF_REGISTER_ARGUMENT2_V2,
	},
};

static const struct pci_device_id pmf_pci_ids[] = {
	{ PCI_DEVICE_DATA(AMD, CPU_ID_RMB,    &amd_pmf_smu_regs_v1) },
	{ PCI_DEVICE_DATA(AMD, CPU_ID_PS,     &amd_pmf_smu_regs_v1) },
	{ PCI_DEVICE_DATA(AMD, 1AH_M20H_ROOT, &amd_pmf_smu_regs_v1) },
	{ PCI_DEVICE_DATA(AMD, 1AH_M60H_ROOT, &amd_pmf_smu_regs_v1) },
	{ PCI_DEVICE_DATA(AMD, 1AH_M80H_ROOT, &amd_pmf_smu_regs_v2) },
	{ }
};

static int amd_pmf_reinit_ta(struct amd_pmf_dev *pdev)
{
	bool status;
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(amd_pmf_ta_uuid); i++) {
		ret = amd_pmf_tee_init(pdev, &amd_pmf_ta_uuid[i]);
		if (ret) {
			dev_err(pdev->dev, "TEE init failed for UUID[%d] ret: %d\n", i, ret);
			return ret;
		}

		ret = amd_pmf_start_policy_engine(pdev);
		dev_dbg(pdev->dev, "start policy engine ret: %d (UUID idx: %d)\n", ret, i);
		status = ret == TA_PMF_TYPE_SUCCESS;
		if (status)
			break;
		amd_pmf_tee_deinit(pdev);
	}

	return 0;
}

static int amd_pmf_restore_handler(struct device *dev)
{
	struct amd_pmf_dev *pdev = dev_get_drvdata(dev);
	int ret;

	if (pdev->buf) {
		ret = amd_pmf_set_dram_addr(pdev, false);
		if (ret)
			return ret;
	}

	if (pdev->smart_pc_enabled)
		amd_pmf_reinit_ta(pdev);

	return 0;
}

static int amd_pmf_freeze_handler(struct device *dev)
{
	struct amd_pmf_dev *pdev = dev_get_drvdata(dev);

	if (!pdev->smart_pc_enabled)
		return 0;

	cancel_delayed_work_sync(&pdev->pb_work);
	/* Clear all TEE resources */
	amd_pmf_tee_deinit(pdev);
	pdev->session_id = 0;

	return 0;
}

static int amd_pmf_suspend_handler(struct device *dev)
{
	struct amd_pmf_dev *pdev = dev_get_drvdata(dev);

	if (pdev->smart_pc_enabled)
		cancel_delayed_work_sync(&pdev->pb_work);

	if (is_apmf_func_supported(pdev, APMF_FUNC_SBIOS_HEARTBEAT_V2))
		amd_pmf_notify_sbios_heartbeat_event_v2(pdev, ON_SUSPEND);

	return 0;
}

static int amd_pmf_resume_handler(struct device *dev)
{
	struct amd_pmf_dev *pdev = dev_get_drvdata(dev);
	int ret;

	if (pdev->buf) {
		ret = amd_pmf_set_dram_addr(pdev, false);
		if (ret)
			return ret;
	}

	if (is_apmf_func_supported(pdev, APMF_FUNC_SBIOS_HEARTBEAT_V2))
		amd_pmf_notify_sbios_heartbeat_event_v2(pdev, ON_RESUME);

	if (pdev->smart_pc_enabled)
		schedule_delayed_work(&pdev->pb_work, msecs_to_jiffies(2000));

	return 0;
}

static const struct dev_pm_ops amd_pmf_pm = {
	.suspend = amd_pmf_suspend_handler,
	.resume = amd_pmf_resume_handler,
	.freeze = amd_pmf_freeze_handler,
	.restore = amd_pmf_restore_handler,
};

static void amd_pmf_init_features(struct amd_pmf_dev *dev)
{
	int ret;

	/* Enable Static Slider */
	if (is_apmf_func_supported(dev, APMF_FUNC_STATIC_SLIDER_GRANULAR) ||
	    is_apmf_func_supported(dev, APMF_FUNC_OS_POWER_SLIDER_UPDATE)) {
		amd_pmf_init_sps(dev);
		dev->pwr_src_notifier.notifier_call = amd_pmf_pwr_src_notify_call;
		power_supply_reg_notifier(&dev->pwr_src_notifier);
		dev_dbg(dev->dev, "SPS enabled and Platform Profiles registered\n");
	}

	if (smart_pc_support) {
		amd_pmf_init_smart_pc(dev);
		if (dev->smart_pc_enabled) {
			dev_dbg(dev->dev, "Smart PC Solution Enabled\n");
			/* If Smart PC is enabled, no need to check for other features */
			return;
		}
	} else {
		dev->smart_pc_enabled = false;
	}

	if (is_apmf_func_supported(dev, APMF_FUNC_AUTO_MODE)) {
		amd_pmf_init_auto_mode(dev);
		dev_dbg(dev->dev, "Auto Mode Init done\n");
	} else if (is_apmf_func_supported(dev, APMF_FUNC_DYN_SLIDER_AC) ||
			  is_apmf_func_supported(dev, APMF_FUNC_DYN_SLIDER_DC)) {
		ret = amd_pmf_init_cnqf(dev);
		if (ret)
			dev_warn(dev->dev, "CnQF Init failed\n");
	}
}

static void amd_pmf_deinit_features(struct amd_pmf_dev *dev)
{
	if (is_apmf_func_supported(dev, APMF_FUNC_STATIC_SLIDER_GRANULAR) ||
	    is_apmf_func_supported(dev, APMF_FUNC_OS_POWER_SLIDER_UPDATE)) {
		power_supply_unreg_notifier(&dev->pwr_src_notifier);
	}

	if (dev->smart_pc_enabled) {
		amd_pmf_deinit_smart_pc(dev);
	} else if (is_apmf_func_supported(dev, APMF_FUNC_AUTO_MODE)) {
		amd_pmf_deinit_auto_mode(dev);
	} else if (is_apmf_func_supported(dev, APMF_FUNC_DYN_SLIDER_AC) ||
			  is_apmf_func_supported(dev, APMF_FUNC_DYN_SLIDER_DC)) {
		amd_pmf_deinit_cnqf(dev);
	}
}

static int amd_pmf_get_smu_mb_offset(struct amd_pmf_dev *pdev, struct pci_dev *rdev)
{
	const struct pci_device_id *id;

	id = pci_match_id(pmf_pci_ids, rdev);
	if (!id)
		return -ENODEV;

	pdev->smu_regs = (const struct amd_pmf_smu_regs *)id->driver_data;

	return 0;
}

static const struct acpi_device_id amd_pmf_acpi_ids[] = {
	{"AMDI0100", 0x100},
	{"AMDI0102", 0},
	{"AMDI0103", 0},
	{"AMDI0105", 0},
	{"AMDI0107", 0},
	{"AMDI0108", 0},
	{"AMDI0109", 0},
	{ }
};
MODULE_DEVICE_TABLE(acpi, amd_pmf_acpi_ids);

static int amd_pmf_probe(struct platform_device *pdev)
{
	const struct acpi_device_id *id;
	struct amd_pmf_dev *dev;
	struct pci_dev *rdev;
	u32 base_addr_lo;
	u32 base_addr_hi;
	u64 base_addr;
	u32 val;
	int err;

	id = acpi_match_device(amd_pmf_acpi_ids, &pdev->dev);
	if (!id)
		return -ENODEV;

	if (id->driver_data == 0x100 && !force_load)
		return -ENODEV;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->dev = &pdev->dev;

	rdev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(0, 0));
	if (!rdev || !pci_match_id(pmf_pci_ids, rdev)) {
		pci_dev_put(rdev);
		return -ENODEV;
	}

	dev->cpu_id = rdev->device;

	err = amd_smn_read(0, AMD_PMF_BASE_ADDR_LO, &val);
	if (err) {
		pci_dev_put(rdev);
		return dev_err_probe(dev->dev, pcibios_err_to_errno(err),
				     "error in reading from 0x%x\n", AMD_PMF_BASE_ADDR_LO);
	}

	base_addr_lo = val & AMD_PMF_BASE_ADDR_HI_MASK;

	err = amd_smn_read(0, AMD_PMF_BASE_ADDR_HI, &val);
	if (err) {
		pci_dev_put(rdev);
		return dev_err_probe(dev->dev, pcibios_err_to_errno(err),
				     "error in reading from 0x%x\n", AMD_PMF_BASE_ADDR_HI);
	}

	base_addr_hi = val & AMD_PMF_BASE_ADDR_LO_MASK;
	pci_dev_put(rdev);
	base_addr = ((u64)base_addr_hi << 32 | base_addr_lo);

	dev->regbase = devm_ioremap(dev->dev, base_addr + AMD_PMF_BASE_ADDR_OFFSET,
				    AMD_PMF_MAPPING_SIZE);
	if (!dev->regbase)
		return -ENOMEM;

	err = devm_mutex_init(dev->dev, &dev->lock);
	if (err)
		return err;

	err = devm_mutex_init(dev->dev, &dev->update_mutex);
	if (err)
		return err;

	err = devm_mutex_init(dev->dev, &dev->cb_mutex);
	if (err)
		return err;

	err = devm_mutex_init(dev->dev, &dev->cbi_mutex);
	if (err)
		return err;

	err = devm_mutex_init(dev->dev, &dev->metrics_mutex);
	if (err)
		return err;

	/* Populate smu_regs with SoC-specific SMU mailbox register offsets */
	err = amd_pmf_get_smu_mb_offset(dev, rdev);
	if (err)
		return err;

	if (amd_pmf_supports_accumulator_metrics(dev)) {
		err = amd_pmf_get_tbl_dram_addr(dev);
		if (err)
			return err;
	}

	apmf_acpi_init(dev);
	platform_set_drvdata(pdev, dev);
	amd_pmf_dbgfs_register(dev);
	amd_pmf_init_features(dev);
	apmf_install_handler(dev);
	if (is_apmf_func_supported(dev, APMF_FUNC_SBIOS_HEARTBEAT_V2))
		amd_pmf_notify_sbios_heartbeat_event_v2(dev, ON_LOAD);

	amd_pmf_set_device(dev->dev);

	err = amd_pmf_cdev_register(dev);
	if (err)
		dev_warn(dev->dev, "failed to register util interface: %d\n", err);

	dev_info(dev->dev, "registered PMF device successfully\n");

	return 0;
}

static void amd_pmf_remove(struct platform_device *pdev)
{
	struct amd_pmf_dev *dev = platform_get_drvdata(pdev);

	amd_pmf_cdev_unregister();
	amd_pmf_deinit_features(dev);
	if (is_apmf_func_supported(dev, APMF_FUNC_SBIOS_HEARTBEAT_V2))
		amd_pmf_notify_sbios_heartbeat_event_v2(dev, ON_UNLOAD);
	apmf_acpi_deinit(dev);
	amd_pmf_dbgfs_unregister(dev);
}

static const struct attribute_group *amd_pmf_driver_groups[] = {
	&cnqf_feature_attribute_group,
	NULL,
};

static struct platform_driver amd_pmf_driver = {
	.driver = {
		.name = "amd-pmf",
		.acpi_match_table = amd_pmf_acpi_ids,
		.dev_groups = amd_pmf_driver_groups,
		.pm = pm_sleep_ptr(&amd_pmf_pm),
	},
	.probe = amd_pmf_probe,
	.remove = amd_pmf_remove,
};
module_platform_driver(amd_pmf_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AMD Platform Management Framework Driver");
MODULE_SOFTDEP("pre: amdtee");
