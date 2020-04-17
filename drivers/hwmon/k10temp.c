// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * k10temp.c - AMD Family 10h/11h/12h/14h/15h/16h/17h
 *		processor hardware monitoring
 *
 * Copyright (c) 2009 Clemens Ladisch <clemens@ladisch.de>
 * Copyright (c) 2020 Guenter Roeck <linux@roeck-us.net>
 *
 * Implementation notes:
 * - CCD register address information as well as the calculation to
 *   convert raw register values is from https://github.com/ocerman/zenpower.
 *   The information is not confirmed from chip datasheets, but experiments
 *   suggest that it provides reasonable temperature values.
 * - Register addresses to read chip voltage and current are also from
 *   https://github.com/ocerman/zenpower, and not confirmed from chip
 *   datasheets. Current calibration is board specific and not typically
 *   shared by board vendors. For this reason, current values are
 *   normalized to report 1A/LSB for core current and and 0.25A/LSB for SoC
 *   current. Reported values can be adjusted using the sensors configuration
 *   file.
 */

#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <asm/amd_nb.h>
#include <asm/processor.h>

MODULE_DESCRIPTION("AMD Family 10h+ CPU core temperature monitor");
MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_LICENSE("GPL");

static bool force;
module_param(force, bool, 0444);
MODULE_PARM_DESC(force, "force loading on processors with erratum 319");

/* Provide lock for writing to NB_SMU_IND_ADDR */
static DEFINE_MUTEX(nb_smu_ind_mutex);

#ifndef PCI_DEVICE_ID_AMD_15H_M70H_NB_F3
#define PCI_DEVICE_ID_AMD_15H_M70H_NB_F3	0x15b3
#endif

/* CPUID function 0x80000001, ebx */
#define CPUID_PKGTYPE_MASK	GENMASK(31, 28)
#define CPUID_PKGTYPE_F		0x00000000
#define CPUID_PKGTYPE_AM2R2_AM3	0x10000000

/* DRAM controller (PCI function 2) */
#define REG_DCT0_CONFIG_HIGH		0x094
#define  DDR3_MODE			BIT(8)

/* miscellaneous (PCI function 3) */
#define REG_HARDWARE_THERMAL_CONTROL	0x64
#define  HTC_ENABLE			BIT(0)

#define REG_REPORTED_TEMPERATURE	0xa4

#define REG_NORTHBRIDGE_CAPABILITIES	0xe8
#define  NB_CAP_HTC			BIT(10)

/*
 * For F15h M60h and M70h, REG_HARDWARE_THERMAL_CONTROL
 * and REG_REPORTED_TEMPERATURE have been moved to
 * D0F0xBC_xD820_0C64 [Hardware Temperature Control]
 * D0F0xBC_xD820_0CA4 [Reported Temperature Control]
 */
#define F15H_M60H_HARDWARE_TEMP_CTRL_OFFSET	0xd8200c64
#define F15H_M60H_REPORTED_TEMP_CTRL_OFFSET	0xd8200ca4

/* F17h M01h Access througn SMN */
#define F17H_M01H_REPORTED_TEMP_CTRL_OFFSET	0x00059800

#define F17H_M70H_CCD_TEMP(x)			(0x00059954 + ((x) * 4))
#define F17H_M70H_CCD_TEMP_VALID		BIT(11)
#define F17H_M70H_CCD_TEMP_MASK			GENMASK(10, 0)

#define F17H_M01H_SVI				0x0005A000
#define F17H_M01H_SVI_TEL_PLANE0		(F17H_M01H_SVI + 0xc)
#define F17H_M01H_SVI_TEL_PLANE1		(F17H_M01H_SVI + 0x10)

#define CUR_TEMP_SHIFT				21
#define CUR_TEMP_RANGE_SEL_MASK			BIT(19)

#define CFACTOR_ICORE				1000000	/* 1A / LSB	*/
#define CFACTOR_ISOC				250000	/* 0.25A / LSB	*/

struct k10temp_data {
	struct pci_dev *pdev;
	void (*read_htcreg)(struct pci_dev *pdev, u32 *regval);
	void (*read_tempreg)(struct pci_dev *pdev, u32 *regval);
	int temp_offset;
	u32 temp_adjust_mask;
	u32 show_temp;
	u32 svi_addr[2];
	bool is_zen;
	bool show_current;
	int cfactor[2];
};

#define TCTL_BIT	0
#define TDIE_BIT	1
#define TCCD_BIT(x)	((x) + 2)

#define HAVE_TEMP(d, channel)	((d)->show_temp & BIT(channel))
#define HAVE_TDIE(d)		HAVE_TEMP(d, TDIE_BIT)

struct tctl_offset {
	u8 model;
	char const *id;
	int offset;
};

static const struct tctl_offset tctl_offset_table[] = {
	{ 0x17, "AMD Ryzen 5 1600X", 20000 },
	{ 0x17, "AMD Ryzen 7 1700X", 20000 },
	{ 0x17, "AMD Ryzen 7 1800X", 20000 },
	{ 0x17, "AMD Ryzen 7 2700X", 10000 },
	{ 0x17, "AMD Ryzen Threadripper 19", 27000 }, /* 19{00,20,50}X */
	{ 0x17, "AMD Ryzen Threadripper 29", 27000 }, /* 29{20,50,70,90}[W]X */
};

static bool is_threadripper(void)
{
	return strstr(boot_cpu_data.x86_model_id, "Threadripper");
}

static bool is_epyc(void)
{
	return strstr(boot_cpu_data.x86_model_id, "EPYC");
}

static void read_htcreg_pci(struct pci_dev *pdev, u32 *regval)
{
	pci_read_config_dword(pdev, REG_HARDWARE_THERMAL_CONTROL, regval);
}

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

static void read_htcreg_nb_f15(struct pci_dev *pdev, u32 *regval)
{
	amd_nb_index_read(pdev, PCI_DEVFN(0, 0), 0xb8,
			  F15H_M60H_HARDWARE_TEMP_CTRL_OFFSET, regval);
}

static void read_tempreg_nb_f15(struct pci_dev *pdev, u32 *regval)
{
	amd_nb_index_read(pdev, PCI_DEVFN(0, 0), 0xb8,
			  F15H_M60H_REPORTED_TEMP_CTRL_OFFSET, regval);
}

static void read_tempreg_nb_f17(struct pci_dev *pdev, u32 *regval)
{
	amd_smn_read(amd_pci_dev_to_node_id(pdev),
		     F17H_M01H_REPORTED_TEMP_CTRL_OFFSET, regval);
}

static long get_raw_temp(struct k10temp_data *data)
{
	u32 regval;
	long temp;

	data->read_tempreg(data->pdev, &regval);
	temp = (regval >> CUR_TEMP_SHIFT) * 125;
	if (regval & data->temp_adjust_mask)
		temp -= 49000;
	return temp;
}

const char *k10temp_temp_label[] = {
	"Tctl",
	"Tdie",
	"Tccd1",
	"Tccd2",
	"Tccd3",
	"Tccd4",
	"Tccd5",
	"Tccd6",
	"Tccd7",
	"Tccd8",
};

const char *k10temp_in_label[] = {
	"Vcore",
	"Vsoc",
};

const char *k10temp_curr_label[] = {
	"Icore",
	"Isoc",
};

static int k10temp_read_labels(struct device *dev,
			       enum hwmon_sensor_types type,
			       u32 attr, int channel, const char **str)
{
	switch (type) {
	case hwmon_temp:
		*str = k10temp_temp_label[channel];
		break;
	case hwmon_in:
		*str = k10temp_in_label[channel];
		break;
	case hwmon_curr:
		*str = k10temp_curr_label[channel];
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int k10temp_read_curr(struct device *dev, u32 attr, int channel,
			     long *val)
{
	struct k10temp_data *data = dev_get_drvdata(dev);
	u32 regval;

	switch (attr) {
	case hwmon_curr_input:
		amd_smn_read(amd_pci_dev_to_node_id(data->pdev),
			     data->svi_addr[channel], &regval);
		*val = DIV_ROUND_CLOSEST(data->cfactor[channel] *
					 (regval & 0xff),
					 1000);
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int k10temp_read_in(struct device *dev, u32 attr, int channel, long *val)
{
	struct k10temp_data *data = dev_get_drvdata(dev);
	u32 regval;

	switch (attr) {
	case hwmon_in_input:
		amd_smn_read(amd_pci_dev_to_node_id(data->pdev),
			     data->svi_addr[channel], &regval);
		regval = (regval >> 16) & 0xff;
		*val = DIV_ROUND_CLOSEST(155000 - regval * 625, 100);
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int k10temp_read_temp(struct device *dev, u32 attr, int channel,
			     long *val)
{
	struct k10temp_data *data = dev_get_drvdata(dev);
	u32 regval;

	switch (attr) {
	case hwmon_temp_input:
		switch (channel) {
		case 0:		/* Tctl */
			*val = get_raw_temp(data);
			if (*val < 0)
				*val = 0;
			break;
		case 1:		/* Tdie */
			*val = get_raw_temp(data) - data->temp_offset;
			if (*val < 0)
				*val = 0;
			break;
		case 2 ... 9:		/* Tccd{1-8} */
			amd_smn_read(amd_pci_dev_to_node_id(data->pdev),
				     F17H_M70H_CCD_TEMP(channel - 2), &regval);
			*val = (regval & F17H_M70H_CCD_TEMP_MASK) * 125 - 49000;
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	case hwmon_temp_max:
		*val = 70 * 1000;
		break;
	case hwmon_temp_crit:
		data->read_htcreg(data->pdev, &regval);
		*val = ((regval >> 16) & 0x7f) * 500 + 52000;
		break;
	case hwmon_temp_crit_hyst:
		data->read_htcreg(data->pdev, &regval);
		*val = (((regval >> 16) & 0x7f)
			- ((regval >> 24) & 0xf)) * 500 + 52000;
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int k10temp_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_temp:
		return k10temp_read_temp(dev, attr, channel, val);
	case hwmon_in:
		return k10temp_read_in(dev, attr, channel, val);
	case hwmon_curr:
		return k10temp_read_curr(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t k10temp_is_visible(const void *_data,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	const struct k10temp_data *data = _data;
	struct pci_dev *pdev = data->pdev;
	u32 reg;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			if (!HAVE_TEMP(data, channel))
				return 0;
			break;
		case hwmon_temp_max:
			if (channel || data->is_zen)
				return 0;
			break;
		case hwmon_temp_crit:
		case hwmon_temp_crit_hyst:
			if (channel || !data->read_htcreg)
				return 0;

			pci_read_config_dword(pdev,
					      REG_NORTHBRIDGE_CAPABILITIES,
					      &reg);
			if (!(reg & NB_CAP_HTC))
				return 0;

			data->read_htcreg(data->pdev, &reg);
			if (!(reg & HTC_ENABLE))
				return 0;
			break;
		case hwmon_temp_label:
			/* Show temperature labels only on Zen CPUs */
			if (!data->is_zen || !HAVE_TEMP(data, channel))
				return 0;
			break;
		default:
			return 0;
		}
		break;
	case hwmon_in:
	case hwmon_curr:
		if (!data->show_current)
			return 0;
		break;
	default:
		return 0;
	}
	return 0444;
}

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
	       (boot_cpu_data.x86_model == 4 && boot_cpu_data.x86_stepping <= 2);
}

#ifdef CONFIG_DEBUG_FS

static void k10temp_smn_regs_show(struct seq_file *s, struct pci_dev *pdev,
				  u32 addr, int count)
{
	u32 reg;
	int i;

	for (i = 0; i < count; i++) {
		if (!(i & 3))
			seq_printf(s, "0x%06x: ", addr + i * 4);
		amd_smn_read(amd_pci_dev_to_node_id(pdev), addr + i * 4, &reg);
		seq_printf(s, "%08x ", reg);
		if ((i & 3) == 3)
			seq_puts(s, "\n");
	}
}

static int svi_show(struct seq_file *s, void *unused)
{
	struct k10temp_data *data = s->private;

	k10temp_smn_regs_show(s, data->pdev, F17H_M01H_SVI, 32);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(svi);

static int thm_show(struct seq_file *s, void *unused)
{
	struct k10temp_data *data = s->private;

	k10temp_smn_regs_show(s, data->pdev,
			      F17H_M01H_REPORTED_TEMP_CTRL_OFFSET, 256);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(thm);

static void k10temp_debugfs_cleanup(void *ddir)
{
	debugfs_remove_recursive(ddir);
}

static void k10temp_init_debugfs(struct k10temp_data *data)
{
	struct dentry *debugfs;
	char name[32];

	/* Only show debugfs data for Family 17h/18h CPUs */
	if (!data->is_zen)
		return;

	scnprintf(name, sizeof(name), "k10temp-%s", pci_name(data->pdev));

	debugfs = debugfs_create_dir(name, NULL);
	if (debugfs) {
		debugfs_create_file("svi", 0444, debugfs, data, &svi_fops);
		debugfs_create_file("thm", 0444, debugfs, data, &thm_fops);
		devm_add_action_or_reset(&data->pdev->dev,
					 k10temp_debugfs_cleanup, debugfs);
	}
}

#else

static void k10temp_init_debugfs(struct k10temp_data *data)
{
}

#endif

static const struct hwmon_channel_info *k10temp_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_MAX |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL),
	NULL
};

static const struct hwmon_ops k10temp_hwmon_ops = {
	.is_visible = k10temp_is_visible,
	.read = k10temp_read,
	.read_string = k10temp_read_labels,
};

static const struct hwmon_chip_info k10temp_chip_info = {
	.ops = &k10temp_hwmon_ops,
	.info = k10temp_info,
};

static void k10temp_get_ccd_support(struct pci_dev *pdev,
				    struct k10temp_data *data, int limit)
{
	u32 regval;
	int i;

	for (i = 0; i < limit; i++) {
		amd_smn_read(amd_pci_dev_to_node_id(pdev),
			     F17H_M70H_CCD_TEMP(i), &regval);
		if (regval & F17H_M70H_CCD_TEMP_VALID)
			data->show_temp |= BIT(TCCD_BIT(i));
	}
}

static int k10temp_probe(struct pci_dev *pdev, const struct pci_device_id *id)
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
	data->show_temp |= BIT(TCTL_BIT);	/* Always show Tctl */

	if (boot_cpu_data.x86 == 0x15 &&
	    ((boot_cpu_data.x86_model & 0xf0) == 0x60 ||
	     (boot_cpu_data.x86_model & 0xf0) == 0x70)) {
		data->read_htcreg = read_htcreg_nb_f15;
		data->read_tempreg = read_tempreg_nb_f15;
	} else if (boot_cpu_data.x86 == 0x17 || boot_cpu_data.x86 == 0x18) {
		data->temp_adjust_mask = CUR_TEMP_RANGE_SEL_MASK;
		data->read_tempreg = read_tempreg_nb_f17;
		data->show_temp |= BIT(TDIE_BIT);	/* show Tdie */
		data->is_zen = true;

		switch (boot_cpu_data.x86_model) {
		case 0x1:	/* Zen */
		case 0x8:	/* Zen+ */
		case 0x11:	/* Zen APU */
		case 0x18:	/* Zen+ APU */
			data->show_current = !is_threadripper() && !is_epyc();
			data->svi_addr[0] = F17H_M01H_SVI_TEL_PLANE0;
			data->svi_addr[1] = F17H_M01H_SVI_TEL_PLANE1;
			data->cfactor[0] = CFACTOR_ICORE;
			data->cfactor[1] = CFACTOR_ISOC;
			k10temp_get_ccd_support(pdev, data, 4);
			break;
		case 0x31:	/* Zen2 Threadripper */
		case 0x71:	/* Zen2 */
			data->show_current = !is_threadripper() && !is_epyc();
			data->cfactor[0] = CFACTOR_ICORE;
			data->cfactor[1] = CFACTOR_ISOC;
			data->svi_addr[0] = F17H_M01H_SVI_TEL_PLANE1;
			data->svi_addr[1] = F17H_M01H_SVI_TEL_PLANE0;
			k10temp_get_ccd_support(pdev, data, 8);
			break;
		}
	} else {
		data->read_htcreg = read_htcreg_pci;
		data->read_tempreg = read_tempreg_pci;
	}

	for (i = 0; i < ARRAY_SIZE(tctl_offset_table); i++) {
		const struct tctl_offset *entry = &tctl_offset_table[i];

		if (boot_cpu_data.x86 == entry->model &&
		    strstr(boot_cpu_data.x86_model_id, entry->id)) {
			data->temp_offset = entry->offset;
			break;
		}
	}

	hwmon_dev = devm_hwmon_device_register_with_info(dev, "k10temp", data,
							 &k10temp_chip_info,
							 NULL);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	k10temp_init_debugfs(data);

	return 0;
}

static const struct pci_device_id k10temp_id_table[] = {
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_10H_NB_MISC) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_11H_NB_MISC) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_CNB17H_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_NB_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_M10H_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_M30H_NB_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_M60H_NB_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_M70H_NB_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_16H_NB_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_16H_M30H_NB_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_DF_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_M10H_DF_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_M30H_DF_F3) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_M70H_DF_F3) },
	{ PCI_VDEVICE(HYGON, PCI_DEVICE_ID_AMD_17H_DF_F3) },
	{}
};
MODULE_DEVICE_TABLE(pci, k10temp_id_table);

static struct pci_driver k10temp_driver = {
	.name = "k10temp",
	.id_table = k10temp_id_table,
	.probe = k10temp_probe,
};

module_pci_driver(k10temp_driver);
