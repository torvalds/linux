// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD Platform Management Framework Driver
 *
 * Copyright (c) 2022, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 */

#include <asm/amd_nb.h>
#include <linux/debugfs.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include "pmf.h"

/* PMF-SMU communication registers */
#define AMD_PMF_REGISTER_MESSAGE	0xA18
#define AMD_PMF_REGISTER_RESPONSE	0xA78
#define AMD_PMF_REGISTER_ARGUMENT	0xA58

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

/* List of supported CPU ids */
#define AMD_CPU_ID_RMB			0x14b5
#define AMD_CPU_ID_PS			0x14e8

#define PMF_MSG_DELAY_MIN_US		50
#define RESPONSE_REGISTER_LOOP_MAX	20000

#define DELAY_MIN_US	2000
#define DELAY_MAX_US	3000

/* override Metrics Table sample size time (in ms) */
static int metrics_table_loop_ms = 1000;
module_param(metrics_table_loop_ms, int, 0644);
MODULE_PARM_DESC(metrics_table_loop_ms, "Metrics Table sample size time (default = 1000ms)");

/* Force load on supported older platforms */
static bool force_load;
module_param(force_load, bool, 0444);
MODULE_PARM_DESC(force_load, "Force load this driver on supported older platforms (experimental)");

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

	amd_pmf_set_sps_power_limits(pmf);

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

static void amd_pmf_get_metrics(struct work_struct *work)
{
	struct amd_pmf_dev *dev = container_of(work, struct amd_pmf_dev, work_buffer.work);
	ktime_t time_elapsed_ms;
	int socket_power;

	mutex_lock(&dev->update_mutex);
	/* Transfer table contents */
	memset(dev->buf, 0, sizeof(dev->m_table));
	amd_pmf_send_cmd(dev, SET_TRANSFER_TABLE, 0, 7, NULL);
	memcpy(&dev->m_table, dev->buf, sizeof(dev->m_table));

	time_elapsed_ms = ktime_to_ms(ktime_get()) - dev->start_time;
	/* Calculate the avg SoC power consumption */
	socket_power = dev->m_table.apu_power + dev->m_table.dgpu_power;

	if (dev->amt_enabled) {
		/* Apply the Auto Mode transition */
		amd_pmf_trans_automode(dev, socket_power, time_elapsed_ms);
	}

	if (dev->cnqf_enabled) {
		/* Apply the CnQF transition */
		amd_pmf_trans_cnqf(dev, socket_power, time_elapsed_ms);
	}

	dev->start_time = ktime_to_ms(ktime_get());
	schedule_delayed_work(&dev->work_buffer, msecs_to_jiffies(metrics_table_loop_ms));
	mutex_unlock(&dev->update_mutex);
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

	value = amd_pmf_reg_read(dev, AMD_PMF_REGISTER_RESPONSE);
	dev_dbg(dev->dev, "AMD_PMF_REGISTER_RESPONSE:%x\n", value);

	value = amd_pmf_reg_read(dev, AMD_PMF_REGISTER_ARGUMENT);
	dev_dbg(dev->dev, "AMD_PMF_REGISTER_ARGUMENT:%d\n", value);

	value = amd_pmf_reg_read(dev, AMD_PMF_REGISTER_MESSAGE);
	dev_dbg(dev->dev, "AMD_PMF_REGISTER_MESSAGE:%x\n", value);
}

int amd_pmf_send_cmd(struct amd_pmf_dev *dev, u8 message, bool get, u32 arg, u32 *data)
{
	int rc;
	u32 val;

	mutex_lock(&dev->lock);

	/* Wait until we get a valid response */
	rc = readx_poll_timeout(ioread32, dev->regbase + AMD_PMF_REGISTER_RESPONSE,
				val, val != 0, PMF_MSG_DELAY_MIN_US,
				PMF_MSG_DELAY_MIN_US * RESPONSE_REGISTER_LOOP_MAX);
	if (rc) {
		dev_err(dev->dev, "failed to talk to SMU\n");
		goto out_unlock;
	}

	/* Write zero to response register */
	amd_pmf_reg_write(dev, AMD_PMF_REGISTER_RESPONSE, 0);

	/* Write argument into argument register */
	amd_pmf_reg_write(dev, AMD_PMF_REGISTER_ARGUMENT, arg);

	/* Write message ID to message ID register */
	amd_pmf_reg_write(dev, AMD_PMF_REGISTER_MESSAGE, message);

	/* Wait until we get a valid response */
	rc = readx_poll_timeout(ioread32, dev->regbase + AMD_PMF_REGISTER_RESPONSE,
				val, val != 0, PMF_MSG_DELAY_MIN_US,
				PMF_MSG_DELAY_MIN_US * RESPONSE_REGISTER_LOOP_MAX);
	if (rc) {
		dev_err(dev->dev, "SMU response timed out\n");
		goto out_unlock;
	}

	switch (val) {
	case AMD_PMF_RESULT_OK:
		if (get) {
			/* PMFW may take longer time to return back the data */
			usleep_range(DELAY_MIN_US, 10 * DELAY_MAX_US);
			*data = amd_pmf_reg_read(dev, AMD_PMF_REGISTER_ARGUMENT);
		}
		break;
	case AMD_PMF_RESULT_CMD_REJECT_BUSY:
		dev_err(dev->dev, "SMU not ready. err: 0x%x\n", val);
		rc = -EBUSY;
		goto out_unlock;
	case AMD_PMF_RESULT_CMD_UNKNOWN:
		dev_err(dev->dev, "SMU cmd unknown. err: 0x%x\n", val);
		rc = -EINVAL;
		goto out_unlock;
	case AMD_PMF_RESULT_CMD_REJECT_PREREQ:
	case AMD_PMF_RESULT_FAILED:
	default:
		dev_err(dev->dev, "SMU cmd failed. err: 0x%x\n", val);
		rc = -EIO;
		goto out_unlock;
	}

out_unlock:
	mutex_unlock(&dev->lock);
	amd_pmf_dump_registers(dev);
	return rc;
}

static const struct pci_device_id pmf_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, AMD_CPU_ID_RMB) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, AMD_CPU_ID_PS) },
	{ }
};

int amd_pmf_init_metrics_table(struct amd_pmf_dev *dev)
{
	u64 phys_addr;
	u32 hi, low;

	INIT_DELAYED_WORK(&dev->work_buffer, amd_pmf_get_metrics);

	/* Get Metrics Table Address */
	dev->buf = kzalloc(sizeof(dev->m_table), GFP_KERNEL);
	if (!dev->buf)
		return -ENOMEM;

	phys_addr = virt_to_phys(dev->buf);
	hi = phys_addr >> 32;
	low = phys_addr & GENMASK(31, 0);

	amd_pmf_send_cmd(dev, SET_DRAM_ADDR_HIGH, 0, hi, NULL);
	amd_pmf_send_cmd(dev, SET_DRAM_ADDR_LOW, 0, low, NULL);

	/*
	 * Start collecting the metrics data after a small delay
	 * or else, we might end up getting stale values from PMFW.
	 */
	schedule_delayed_work(&dev->work_buffer, msecs_to_jiffies(metrics_table_loop_ms * 3));

	return 0;
}

static void amd_pmf_init_features(struct amd_pmf_dev *dev)
{
	int ret;

	/* Enable Static Slider */
	if (is_apmf_func_supported(dev, APMF_FUNC_STATIC_SLIDER_GRANULAR)) {
		amd_pmf_init_sps(dev);
		dev_dbg(dev->dev, "SPS enabled and Platform Profiles registered\n");
	}

	/* Enable Auto Mode */
	if (is_apmf_func_supported(dev, APMF_FUNC_AUTO_MODE)) {
		amd_pmf_init_auto_mode(dev);
		dev_dbg(dev->dev, "Auto Mode Init done\n");
	} else if (is_apmf_func_supported(dev, APMF_FUNC_DYN_SLIDER_AC) ||
			  is_apmf_func_supported(dev, APMF_FUNC_DYN_SLIDER_DC)) {
		/* Enable Cool n Quiet Framework (CnQF) */
		ret = amd_pmf_init_cnqf(dev);
		if (ret)
			dev_warn(dev->dev, "CnQF Init failed\n");
	}
}

static void amd_pmf_deinit_features(struct amd_pmf_dev *dev)
{
	if (is_apmf_func_supported(dev, APMF_FUNC_STATIC_SLIDER_GRANULAR))
		amd_pmf_deinit_sps(dev);

	if (is_apmf_func_supported(dev, APMF_FUNC_AUTO_MODE)) {
		amd_pmf_deinit_auto_mode(dev);
	} else if (is_apmf_func_supported(dev, APMF_FUNC_DYN_SLIDER_AC) ||
			  is_apmf_func_supported(dev, APMF_FUNC_DYN_SLIDER_DC)) {
		amd_pmf_deinit_cnqf(dev);
	}
}

static const struct acpi_device_id amd_pmf_acpi_ids[] = {
	{"AMDI0100", 0x100},
	{"AMDI0102", 0},
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
		dev_err(dev->dev, "error in reading from 0x%x\n", AMD_PMF_BASE_ADDR_LO);
		pci_dev_put(rdev);
		return pcibios_err_to_errno(err);
	}

	base_addr_lo = val & AMD_PMF_BASE_ADDR_HI_MASK;

	err = amd_smn_read(0, AMD_PMF_BASE_ADDR_HI, &val);
	if (err) {
		dev_err(dev->dev, "error in reading from 0x%x\n", AMD_PMF_BASE_ADDR_HI);
		pci_dev_put(rdev);
		return pcibios_err_to_errno(err);
	}

	base_addr_hi = val & AMD_PMF_BASE_ADDR_LO_MASK;
	pci_dev_put(rdev);
	base_addr = ((u64)base_addr_hi << 32 | base_addr_lo);

	dev->regbase = devm_ioremap(dev->dev, base_addr + AMD_PMF_BASE_ADDR_OFFSET,
				    AMD_PMF_MAPPING_SIZE);
	if (!dev->regbase)
		return -ENOMEM;

	mutex_init(&dev->lock);
	mutex_init(&dev->update_mutex);

	apmf_acpi_init(dev);
	platform_set_drvdata(pdev, dev);
	amd_pmf_init_features(dev);
	apmf_install_handler(dev);
	amd_pmf_dbgfs_register(dev);

	dev->pwr_src_notifier.notifier_call = amd_pmf_pwr_src_notify_call;
	power_supply_reg_notifier(&dev->pwr_src_notifier);

	dev_info(dev->dev, "registered PMF device successfully\n");

	return 0;
}

static int amd_pmf_remove(struct platform_device *pdev)
{
	struct amd_pmf_dev *dev = platform_get_drvdata(pdev);

	power_supply_unreg_notifier(&dev->pwr_src_notifier);
	amd_pmf_deinit_features(dev);
	apmf_acpi_deinit(dev);
	amd_pmf_dbgfs_unregister(dev);
	mutex_destroy(&dev->lock);
	mutex_destroy(&dev->update_mutex);
	kfree(dev->buf);
	return 0;
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
	},
	.probe = amd_pmf_probe,
	.remove = amd_pmf_remove,
};
module_platform_driver(amd_pmf_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AMD Platform Management Framework Driver");
