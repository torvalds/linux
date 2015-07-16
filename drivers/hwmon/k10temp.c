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

/* Provide lock for writing to NB_SMU_IND_ADDR */
static DEFINE_MUTEX(nb_smu_ind_mutex);

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

/*
 * For F15h M60h, functionality of REG_REPORTED_TEMPERATURE
 * has been moved to D0F0xBC_xD820_0CA4 [Reported Temperature
 * Control]
 */
#define F15H_M60H_REPORTED_TEMP_CTRL_OFFSET	0xd8200ca4
#define PCI_DEVICE_ID_AMD_15H_M60H_NB_F3	0x1573

static void amd_nb_smu_index_read(struct pci_dev *pdev, unsigned int devfn,
				  int offset, u32 *val)
{
	mutex_lock(&nb_smu_ind_mutex);
	pci_bus_write_config_dword(pdev->bus, devfn,
				   0xb8, offset);
	pci_bus_read_config_dword(pdev->bus, devfn,
				  0xbc, val);
	mutex_unlock(&nb_smu_ind_mutex);
}

static ssize_t show_temp(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	u32 regval;
	struct pci_dev *pdev = dev_get_drvdata(dev);

	if (boot_cpu_data.x86 == 0x15 && boot_cpu_data.x86_model == 0x60) {
		amd_nb_smu_index_read(pdev, PCI_DEVFN(0, 0),
				      F15H_M60H_REPORTED_TEMP_CTRL_OFFSET,
				      &regval);
	} else {
		pci_read_config_dword(pdev, REG_REPORTED_TEMPERATURE, &regval);
	}
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

	pci_read_config_dword(dev_get_drvdata(dev),
			      REG_HARDWARE_THERMAL_CONTROL, &regval);
	value = ((regval >> 16) & 0x7f) * 500 + 52000;
	if (show_hyst)
		value -= ((regval >> 24) & 0xf) * 500;
	return sprintf(buf, "%d\n", value);
}

static DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL);
static DEVICE_ATTR(temp1_max, S_IRUGO, show_temp_max, NULL);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IRUGO, show_temp_crit, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_crit_hyst, S_IRUGO, show_temp_crit, NULL, 1);

static umode_t k10temp_is_visible(struct kobject *kobj,
				  struct attribute *attr, int index)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct pci_dev *pdev = dev_get_drvdata(dev);

	if (index >= 2) {
		u32 reg_caps, reg_htc;

		pci_read_config_dword(pdev, REG_NORTHBRIDGE_CAPABILITIES,
				      &reg_caps);
		pci_read_config_dword(pdev, REG_HARDWARE_THERMAL_CONTROL,
				      &reg_htc);
		if (!(reg_caps & NB_CAP_HTC) || !(reg_htc & HTC_ENABLE))
			return 0;
	}
	return attr->mode;
}

static struct attribute *k10temp_attrs[] = {
	&dev_attr_temp1_input.attr,
	&dev_attr_temp1_max.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_hyst.dev_attr.attr,
	NULL
};

static const struct attribute_group k10temp_group = {
	.attrs = k10temp_attrs,
	.is_visible = k10temp_is_visible,
};
__ATTRIBUTE_GROUPS(k10temp);

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
	int unreliable = has_erratum_319(pdev);
	struct device *dev = &pdev->dev;
	struct device *hwmon_dev;

	if (unreliable) {
		if (!force) {
			dev_err(dev,
				"unreliable CPU thermal sensor; monitoring disabled\n");
			return -ENODEV;
		}
		dev_warn(dev,
			 "unreliable CPU thermal sensor; check erratum 319\n");
	}

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, "k10temp", pdev,
							   k10temp_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct pci_device_id k10temp_id_table[] = {
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_10H_NB_MISC) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_11H_NB_MISC) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_CNB17H_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_NB_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_M10H_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_M30H_NB_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_M60H_NB_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_16H_NB_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_16H_M30H_NB_F3) },
	{}
};
MODULE_DEVICE_TABLE(pci, k10temp_id_table);

static struct pci_driver k10temp_driver = {
	.name = "k10temp",
	.id_table = k10temp_id_table,
	.probe = k10temp_probe,
};

module_pci_driver(k10temp_driver);
