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

#ifndef PCI_DEVICE_ID_AMD_17H_DF_F3
#define PCI_DEVICE_ID_AMD_17H_DF_F3	0x1463
#endif

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

/* F17h M01h Access througn SMN */
#define F17H_M01H_REPORTED_TEMP_CTRL_OFFSET	0x00059800

struct k10temp_data {
	struct pci_dev *pdev;
	void (*read_tempreg)(struct pci_dev *pdev, u32 *regval);
	int temp_offset;
};

struct tctl_offset {
	u8 model;
	char const *id;
	int offset;
};

static const struct tctl_offset tctl_offset_table[] = {
	{ 0x17, "AMD Ryzen 5 1600X", 20000 },
	{ 0x17, "AMD Ryzen 7 1700X", 20000 },
	{ 0x17, "AMD Ryzen 7 1800X", 20000 },
	{ 0x17, "AMD Ryzen Threadripper 1950X", 27000 },
	{ 0x17, "AMD Ryzen Threadripper 1920X", 27000 },
	{ 0x17, "AMD Ryzen Threadripper 1950", 10000 },
	{ 0x17, "AMD Ryzen Threadripper 1920", 10000 },
	{ 0x17, "AMD Ryzen Threadripper 1910", 10000 },
};

static void read_tempreg_pci(struct pci_dev *pdev, u32 *regval)
{
	pci_read_config_dword(pdev, REG_REPORTED_TEMPERATURE, regval);
}

static void amd_nb_index_read(struct pci_dev *pdev, unsigned int devfn,
			      unsigned int base, int offset, u32 *val)
{
	mutex_lock(&nb_smu_ind_mutex);
	pci_bus_write_config_dword(pdev->bus, devfn,
				   base, offset);
	pci_bus_read_config_dword(pdev->bus, devfn,
				  base + 4, val);
	mutex_unlock(&nb_smu_ind_mutex);
}

static void read_tempreg_nb_f15(struct pci_dev *pdev, u32 *regval)
{
	amd_nb_index_read(pdev, PCI_DEVFN(0, 0), 0xb8,
			  F15H_M60H_REPORTED_TEMP_CTRL_OFFSET, regval);
}

static void read_tempreg_nb_f17(struct pci_dev *pdev, u32 *regval)
{
	amd_nb_index_read(pdev, PCI_DEVFN(0, 0), 0x60,
			  F17H_M01H_REPORTED_TEMP_CTRL_OFFSET, regval);
}

static ssize_t temp1_input_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct k10temp_data *data = dev_get_drvdata(dev);
	u32 regval;
	unsigned int temp;

	data->read_tempreg(data->pdev, &regval);
	temp = (regval >> 21) * 125;
	temp -= data->temp_offset;

	return sprintf(buf, "%u\n", temp);
}

static ssize_t temp1_max_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", 70 * 1000);
}

static ssize_t show_temp_crit(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct k10temp_data *data = dev_get_drvdata(dev);
	int show_hyst = attr->index;
	u32 regval;
	int value;

	pci_read_config_dword(data->pdev,
			      REG_HARDWARE_THERMAL_CONTROL, &regval);
	value = ((regval >> 16) & 0x7f) * 500 + 52000;
	if (show_hyst)
		value -= ((regval >> 24) & 0xf) * 500;
	return sprintf(buf, "%d\n", value);
}

static DEVICE_ATTR_RO(temp1_input);
static DEVICE_ATTR_RO(temp1_max);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IRUGO, show_temp_crit, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_crit_hyst, S_IRUGO, show_temp_crit, NULL, 1);

static umode_t k10temp_is_visible(struct kobject *kobj,
				  struct attribute *attr, int index)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct k10temp_data *data = dev_get_drvdata(dev);
	struct pci_dev *pdev = data->pdev;

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
	struct k10temp_data *data;
	struct device *hwmon_dev;
	int i;

	if (unreliable) {
		if (!force) {
			dev_err(dev,
				"unreliable CPU thermal sensor; monitoring disabled\n");
			return -ENODEV;
		}
		dev_warn(dev,
			 "unreliable CPU thermal sensor; check erratum 319\n");
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->pdev = pdev;

	if (boot_cpu_data.x86 == 0x15 && (boot_cpu_data.x86_model == 0x60 ||
					  boot_cpu_data.x86_model == 0x70))
		data->read_tempreg = read_tempreg_nb_f15;
	else if (boot_cpu_data.x86 == 0x17)
		data->read_tempreg = read_tempreg_nb_f17;
	else
		data->read_tempreg = read_tempreg_pci;

	for (i = 0; i < ARRAY_SIZE(tctl_offset_table); i++) {
		const struct tctl_offset *entry = &tctl_offset_table[i];

		if (boot_cpu_data.x86 == entry->model &&
		    strstr(boot_cpu_data.x86_model_id, entry->id)) {
			data->temp_offset = entry->offset;
			break;
		}
	}

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, "k10temp", data,
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
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_DF_F3) },
	{}
};
MODULE_DEVICE_TABLE(pci, k10temp_id_table);

static struct pci_driver k10temp_driver = {
	.name = "k10temp",
	.id_table = k10temp_id_table,
	.probe = k10temp_probe,
};

module_pci_driver(k10temp_driver);
