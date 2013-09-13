/*
 * k10temp.c - AMD Family 10h/11h/12h/14h/15h/16h processor hardware monitoring
 *
 * Copyright (c) 2009 Clemens Ladisch <clemens@ladisch.de>
 *
 *
 * This driver is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this driver; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <asm/processor.h>

MODULE_DESCRIPTION("AMD Family 10h+ CPU core temperature monitor");
MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_LICENSE("GPL");

static bool force;
module_param(force, bool, 0444);
MODULE_PARM_DESC(force, "force loading on processors with erratum 319");

/* CPUID function 0x80000001, ebx */
#define CPUID_PKGTYPE_MASK	0xf0000000
#define CPUID_PKGTYPE_F		0x00000000
#define CPUID_PKGTYPE_AM2R2_AM3	0x10000000

/* DRAM controller (PCI function 2) */
#define REG_DCT0_CONFIG_HIGH		0x094
#define  DDR3_MODE			0x00000100

/* miscellaneous (PCI function 3) */
#define REG_HARDWARE_THERMAL_CONTROL	0x64
#define  HTC_ENABLE			0x00000001

#define REG_REPORTED_TEMPERATURE	0xa4

#define REG_NORTHBRIDGE_CAPABILITIES	0xe8
#define  NB_CAP_HTC			0x00000400

static ssize_t show_temp(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	u32 regval;

	pci_read_config_dword(to_pci_dev(dev),
			      REG_REPORTED_TEMPERATURE, &regval);
	return sprintf(buf, "%u\n", (regval >> 21) * 125);
}

static ssize_t show_temp_max(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", 70 * 1000);
}

static ssize_t show_temp_crit(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int show_hyst = attr->index;
	u32 regval;
	int value;

	pci_read_config_dword(to_pci_dev(dev),
			      REG_HARDWARE_THERMAL_CONTROL, &regval);
	value = ((regval >> 16) & 0x7f) * 500 + 52000;
	if (show_hyst)
		value -= ((regval >> 24) & 0xf) * 500;
	return sprintf(buf, "%d\n", value);
}

static ssize_t show_name(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "k10temp\n");
}

static DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL);
static DEVICE_ATTR(temp1_max, S_IRUGO, show_temp_max, NULL);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IRUGO, show_temp_crit, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_crit_hyst, S_IRUGO, show_temp_crit, NULL, 1);
static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

static bool has_erratum_319(struct pci_dev *pdev)
{
	u32 pkg_type, reg_dram_cfg;

	if (boot_cpu_data.x86 != 0x10)
		return false;

	/*
	 * Erratum 319: The thermal sensor of Socket F/AM2+ processors
	 *              may be unreliable.
	 */
	pkg_type = cpuid_ebx(0x80000001) & CPUID_PKGTYPE_MASK;
	if (pkg_type == CPUID_PKGTYPE_F)
		return true;
	if (pkg_type != CPUID_PKGTYPE_AM2R2_AM3)
		return false;

	/* DDR3 memory implies socket AM3, which is good */
	pci_bus_read_config_dword(pdev->bus,
				  PCI_DEVFN(PCI_SLOT(pdev->devfn), 2),
				  REG_DCT0_CONFIG_HIGH, &reg_dram_cfg);
	if (reg_dram_cfg & DDR3_MODE)
		return false;

	/*
	 * Unfortunately it is possible to run a socket AM3 CPU with DDR2
	 * memory. We blacklist all the cores which do exist in socket AM2+
	 * format. It still isn't perfect, as RB-C2 cores exist in both AM2+
	 * and AM3 formats, but that's the best we can do.
	 */
	return boot_cpu_data.x86_model < 4 ||
	       (boot_cpu_data.x86_model == 4 && boot_cpu_data.x86_mask <= 2);
}

static int k10temp_probe(struct pci_dev *pdev,
				   const struct pci_device_id *id)
{
	struct device *hwmon_dev;
	u32 reg_caps, reg_htc;
	int unreliable = has_erratum_319(pdev);
	int err;

	if (unreliable && !force) {
		dev_err(&pdev->dev,
			"unreliable CPU thermal sensor; monitoring disabled\n");
		err = -ENODEV;
		goto exit;
	}

	err = device_create_file(&pdev->dev, &dev_attr_temp1_input);
	if (err)
		goto exit;
	err = device_create_file(&pdev->dev, &dev_attr_temp1_max);
	if (err)
		goto exit_remove;

	pci_read_config_dword(pdev, REG_NORTHBRIDGE_CAPABILITIES, &reg_caps);
	pci_read_config_dword(pdev, REG_HARDWARE_THERMAL_CONTROL, &reg_htc);
	if ((reg_caps & NB_CAP_HTC) && (reg_htc & HTC_ENABLE)) {
		err = device_create_file(&pdev->dev,
				&sensor_dev_attr_temp1_crit.dev_attr);
		if (err)
			goto exit_remove;
		err = device_create_file(&pdev->dev,
				&sensor_dev_attr_temp1_crit_hyst.dev_attr);
		if (err)
			goto exit_remove;
	}

	err = device_create_file(&pdev->dev, &dev_attr_name);
	if (err)
		goto exit_remove;

	hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(hwmon_dev)) {
		err = PTR_ERR(hwmon_dev);
		goto exit_remove;
	}
	pci_set_drvdata(pdev, hwmon_dev);

	if (unreliable && force)
		dev_warn(&pdev->dev,
			 "unreliable CPU thermal sensor; check erratum 319\n");
	return 0;

exit_remove:
	device_remove_file(&pdev->dev, &dev_attr_name);
	device_remove_file(&pdev->dev, &dev_attr_temp1_input);
	device_remove_file(&pdev->dev, &dev_attr_temp1_max);
	device_remove_file(&pdev->dev,
			   &sensor_dev_attr_temp1_crit.dev_attr);
	device_remove_file(&pdev->dev,
			   &sensor_dev_attr_temp1_crit_hyst.dev_attr);
exit:
	return err;
}

static void k10temp_remove(struct pci_dev *pdev)
{
	hwmon_device_unregister(pci_get_drvdata(pdev));
	device_remove_file(&pdev->dev, &dev_attr_name);
	device_remove_file(&pdev->dev, &dev_attr_temp1_input);
	device_remove_file(&pdev->dev, &dev_attr_temp1_max);
	device_remove_file(&pdev->dev,
			   &sensor_dev_attr_temp1_crit.dev_attr);
	device_remove_file(&pdev->dev,
			   &sensor_dev_attr_temp1_crit_hyst.dev_attr);
}

static DEFINE_PCI_DEVICE_TABLE(k10temp_id_table) = {
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_10H_NB_MISC) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_11H_NB_MISC) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_CNB17H_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_NB_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_M10H_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_16H_NB_F3) },
	{}
};
MODULE_DEVICE_TABLE(pci, k10temp_id_table);

static struct pci_driver k10temp_driver = {
	.name = "k10temp",
	.id_table = k10temp_id_table,
	.probe = k10temp_probe,
	.remove = k10temp_remove,
};

module_pci_driver(k10temp_driver);
