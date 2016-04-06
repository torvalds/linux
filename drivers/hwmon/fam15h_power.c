/*
 * fam15h_power.c - AMD Family 15h processor power monitoring
 *
 * Copyright (c) 2011-2016 Advanced Micro Devices, Inc.
 * Author: Andreas Herrmann <herrmann.der.user@googlemail.com>
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
#include <linux/bitops.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <asm/processor.h>
#include <asm/msr.h>

MODULE_DESCRIPTION("AMD Family 15h CPU processor power monitor");
MODULE_AUTHOR("Andreas Herrmann <herrmann.der.user@googlemail.com>");
MODULE_LICENSE("GPL");

/* D18F3 */
#define REG_NORTHBRIDGE_CAP		0xe8

/* D18F4 */
#define REG_PROCESSOR_TDP		0x1b8

/* D18F5 */
#define REG_TDP_RUNNING_AVERAGE		0xe0
#define REG_TDP_LIMIT3			0xe8

#define FAM15H_MIN_NUM_ATTRS		2
#define FAM15H_NUM_GROUPS		2
#define MAX_CUS				8

/* set maximum interval as 1 second */
#define MAX_INTERVAL			1000

#define MSR_F15H_CU_PWR_ACCUMULATOR	0xc001007a
#define MSR_F15H_CU_MAX_PWR_ACCUMULATOR	0xc001007b
#define MSR_F15H_PTSC			0xc0010280

#define PCI_DEVICE_ID_AMD_15H_M70H_NB_F4 0x15b4

struct fam15h_power_data {
	struct pci_dev *pdev;
	unsigned int tdp_to_watts;
	unsigned int base_tdp;
	unsigned int processor_pwr_watts;
	unsigned int cpu_pwr_sample_ratio;
	const struct attribute_group *groups[FAM15H_NUM_GROUPS];
	struct attribute_group group;
	/* maximum accumulated power of a compute unit */
	u64 max_cu_acc_power;
	/* accumulated power of the compute units */
	u64 cu_acc_power[MAX_CUS];
	/* performance timestamp counter */
	u64 cpu_sw_pwr_ptsc[MAX_CUS];
	/* online/offline status of current compute unit */
	int cu_on[MAX_CUS];
	unsigned long power_period;
};

static bool is_carrizo_or_later(void)
{
	return boot_cpu_data.x86 == 0x15 && boot_cpu_data.x86_model >= 0x60;
}

static ssize_t show_power(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	u32 val, tdp_limit, running_avg_range;
	s32 running_avg_capture;
	u64 curr_pwr_watts;
	struct fam15h_power_data *data = dev_get_drvdata(dev);
	struct pci_dev *f4 = data->pdev;

	pci_bus_read_config_dword(f4->bus, PCI_DEVFN(PCI_SLOT(f4->devfn), 5),
				  REG_TDP_RUNNING_AVERAGE, &val);

	/*
	 * On Carrizo and later platforms, TdpRunAvgAccCap bit field
	 * is extended to 4:31 from 4:25.
	 */
	if (is_carrizo_or_later()) {
		running_avg_capture = val >> 4;
		running_avg_capture = sign_extend32(running_avg_capture, 27);
	} else {
		running_avg_capture = (val >> 4) & 0x3fffff;
		running_avg_capture = sign_extend32(running_avg_capture, 21);
	}

	running_avg_range = (val & 0xf) + 1;

	pci_bus_read_config_dword(f4->bus, PCI_DEVFN(PCI_SLOT(f4->devfn), 5),
				  REG_TDP_LIMIT3, &val);

	/*
	 * On Carrizo and later platforms, ApmTdpLimit bit field
	 * is extended to 16:31 from 16:28.
	 */
	if (is_carrizo_or_later())
		tdp_limit = val >> 16;
	else
		tdp_limit = (val >> 16) & 0x1fff;

	curr_pwr_watts = ((u64)(tdp_limit +
				data->base_tdp)) << running_avg_range;
	curr_pwr_watts -= running_avg_capture;
	curr_pwr_watts *= data->tdp_to_watts;

	/*
	 * Convert to microWatt
	 *
	 * power is in Watt provided as fixed point integer with
	 * scaling factor 1/(2^16).  For conversion we use
	 * (10^6)/(2^16) = 15625/(2^10)
	 */
	curr_pwr_watts = (curr_pwr_watts * 15625) >> (10 + running_avg_range);
	return sprintf(buf, "%u\n", (unsigned int) curr_pwr_watts);
}
static DEVICE_ATTR(power1_input, S_IRUGO, show_power, NULL);

static ssize_t show_power_crit(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct fam15h_power_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", data->processor_pwr_watts);
}
static DEVICE_ATTR(power1_crit, S_IRUGO, show_power_crit, NULL);

static void do_read_registers_on_cu(void *_data)
{
	struct fam15h_power_data *data = _data;
	int cpu, cu;

	cpu = smp_processor_id();

	/*
	 * With the new x86 topology modelling, cpu core id actually
	 * is compute unit id.
	 */
	cu = cpu_data(cpu).cpu_core_id;

	rdmsrl_safe(MSR_F15H_CU_PWR_ACCUMULATOR, &data->cu_acc_power[cu]);
	rdmsrl_safe(MSR_F15H_PTSC, &data->cpu_sw_pwr_ptsc[cu]);

	data->cu_on[cu] = 1;
}

/*
 * This function is only able to be called when CPUID
 * Fn8000_0007:EDX[12] is set.
 */
static int read_registers(struct fam15h_power_data *data)
{
	int this_cpu, ret, cpu;
	int core, this_core;
	cpumask_var_t mask;

	ret = zalloc_cpumask_var(&mask, GFP_KERNEL);
	if (!ret)
		return -ENOMEM;

	memset(data->cu_on, 0, sizeof(int) * MAX_CUS);

	get_online_cpus();
	this_cpu = smp_processor_id();

	/*
	 * Choose the first online core of each compute unit, and then
	 * read their MSR value of power and ptsc in a single IPI,
	 * because the MSR value of CPU core represent the compute
	 * unit's.
	 */
	core = -1;

	for_each_online_cpu(cpu) {
		this_core = topology_core_id(cpu);

		if (this_core == core)
			continue;

		core = this_core;

		/* get any CPU on this compute unit */
		cpumask_set_cpu(cpumask_any(topology_sibling_cpumask(cpu)), mask);
	}

	if (cpumask_test_cpu(this_cpu, mask))
		do_read_registers_on_cu(data);

	smp_call_function_many(mask, do_read_registers_on_cu, data, true);
	put_online_cpus();

	free_cpumask_var(mask);

	return 0;
}

static ssize_t acc_show_power(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct fam15h_power_data *data = dev_get_drvdata(dev);
	u64 prev_cu_acc_power[MAX_CUS], prev_ptsc[MAX_CUS],
	    jdelta[MAX_CUS];
	u64 tdelta, avg_acc;
	int cu, cu_num, ret;
	signed long leftover;

	/*
	 * With the new x86 topology modelling, x86_max_cores is the
	 * compute unit number.
	 */
	cu_num = boot_cpu_data.x86_max_cores;

	ret = read_registers(data);
	if (ret)
		return 0;

	for (cu = 0; cu < cu_num; cu++) {
		prev_cu_acc_power[cu] = data->cu_acc_power[cu];
		prev_ptsc[cu] = data->cpu_sw_pwr_ptsc[cu];
	}

	leftover = schedule_timeout_interruptible(msecs_to_jiffies(data->power_period));
	if (leftover)
		return 0;

	ret = read_registers(data);
	if (ret)
		return 0;

	for (cu = 0, avg_acc = 0; cu < cu_num; cu++) {
		/* check if current compute unit is online */
		if (data->cu_on[cu] == 0)
			continue;

		if (data->cu_acc_power[cu] < prev_cu_acc_power[cu]) {
			jdelta[cu] = data->max_cu_acc_power + data->cu_acc_power[cu];
			jdelta[cu] -= prev_cu_acc_power[cu];
		} else {
			jdelta[cu] = data->cu_acc_power[cu] - prev_cu_acc_power[cu];
		}
		tdelta = data->cpu_sw_pwr_ptsc[cu] - prev_ptsc[cu];
		jdelta[cu] *= data->cpu_pwr_sample_ratio * 1000;
		do_div(jdelta[cu], tdelta);

		/* the unit is microWatt */
		avg_acc += jdelta[cu];
	}

	return sprintf(buf, "%llu\n", (unsigned long long)avg_acc);
}
static DEVICE_ATTR(power1_average, S_IRUGO, acc_show_power, NULL);

static ssize_t acc_show_power_period(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct fam15h_power_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%lu\n", data->power_period);
}

static ssize_t acc_set_power_period(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct fam15h_power_data *data = dev_get_drvdata(dev);
	unsigned long temp;
	int ret;

	ret = kstrtoul(buf, 10, &temp);
	if (ret)
		return ret;

	if (temp > MAX_INTERVAL)
		return -EINVAL;

	/* the interval value should be greater than 0 */
	if (temp <= 0)
		return -EINVAL;

	data->power_period = temp;

	return count;
}
static DEVICE_ATTR(power1_average_interval, S_IRUGO | S_IWUSR,
		   acc_show_power_period, acc_set_power_period);

static int fam15h_power_init_attrs(struct pci_dev *pdev,
				   struct fam15h_power_data *data)
{
	int n = FAM15H_MIN_NUM_ATTRS;
	struct attribute **fam15h_power_attrs;
	struct cpuinfo_x86 *c = &boot_cpu_data;

	if (c->x86 == 0x15 &&
	    (c->x86_model <= 0xf ||
	     (c->x86_model >= 0x60 && c->x86_model <= 0x7f)))
		n += 1;

	/* check if processor supports accumulated power */
	if (boot_cpu_has(X86_FEATURE_ACC_POWER))
		n += 2;

	fam15h_power_attrs = devm_kcalloc(&pdev->dev, n,
					  sizeof(*fam15h_power_attrs),
					  GFP_KERNEL);

	if (!fam15h_power_attrs)
		return -ENOMEM;

	n = 0;
	fam15h_power_attrs[n++] = &dev_attr_power1_crit.attr;
	if (c->x86 == 0x15 &&
	    (c->x86_model <= 0xf ||
	     (c->x86_model >= 0x60 && c->x86_model <= 0x7f)))
		fam15h_power_attrs[n++] = &dev_attr_power1_input.attr;

	if (boot_cpu_has(X86_FEATURE_ACC_POWER)) {
		fam15h_power_attrs[n++] = &dev_attr_power1_average.attr;
		fam15h_power_attrs[n++] = &dev_attr_power1_average_interval.attr;
	}

	data->group.attrs = fam15h_power_attrs;

	return 0;
}

static bool should_load_on_this_node(struct pci_dev *f4)
{
	u32 val;

	pci_bus_read_config_dword(f4->bus, PCI_DEVFN(PCI_SLOT(f4->devfn), 3),
				  REG_NORTHBRIDGE_CAP, &val);
	if ((val & BIT(29)) && ((val >> 30) & 3))
		return false;

	return true;
}

/*
 * Newer BKDG versions have an updated recommendation on how to properly
 * initialize the running average range (was: 0xE, now: 0x9). This avoids
 * counter saturations resulting in bogus power readings.
 * We correct this value ourselves to cope with older BIOSes.
 */
static const struct pci_device_id affected_device[] = {
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_NB_F4) },
	{ 0 }
};

static void tweak_runavg_range(struct pci_dev *pdev)
{
	u32 val;

	/*
	 * let this quirk apply only to the current version of the
	 * northbridge, since future versions may change the behavior
	 */
	if (!pci_match_id(affected_device, pdev))
		return;

	pci_bus_read_config_dword(pdev->bus,
		PCI_DEVFN(PCI_SLOT(pdev->devfn), 5),
		REG_TDP_RUNNING_AVERAGE, &val);
	if ((val & 0xf) != 0xe)
		return;

	val &= ~0xf;
	val |=  0x9;
	pci_bus_write_config_dword(pdev->bus,
		PCI_DEVFN(PCI_SLOT(pdev->devfn), 5),
		REG_TDP_RUNNING_AVERAGE, val);
}

#ifdef CONFIG_PM
static int fam15h_power_resume(struct pci_dev *pdev)
{
	tweak_runavg_range(pdev);
	return 0;
}
#else
#define fam15h_power_resume NULL
#endif

static int fam15h_power_init_data(struct pci_dev *f4,
				  struct fam15h_power_data *data)
{
	u32 val;
	u64 tmp;
	int ret;

	pci_read_config_dword(f4, REG_PROCESSOR_TDP, &val);
	data->base_tdp = val >> 16;
	tmp = val & 0xffff;

	pci_bus_read_config_dword(f4->bus, PCI_DEVFN(PCI_SLOT(f4->devfn), 5),
				  REG_TDP_LIMIT3, &val);

	data->tdp_to_watts = ((val & 0x3ff) << 6) | ((val >> 10) & 0x3f);
	tmp *= data->tdp_to_watts;

	/* result not allowed to be >= 256W */
	if ((tmp >> 16) >= 256)
		dev_warn(&f4->dev,
			 "Bogus value for ProcessorPwrWatts (processor_pwr_watts>=%u)\n",
			 (unsigned int) (tmp >> 16));

	/* convert to microWatt */
	data->processor_pwr_watts = (tmp * 15625) >> 10;

	ret = fam15h_power_init_attrs(f4, data);
	if (ret)
		return ret;


	/* CPUID Fn8000_0007:EDX[12] indicates to support accumulated power */
	if (!boot_cpu_has(X86_FEATURE_ACC_POWER))
		return 0;

	/*
	 * determine the ratio of the compute unit power accumulator
	 * sample period to the PTSC counter period by executing CPUID
	 * Fn8000_0007:ECX
	 */
	data->cpu_pwr_sample_ratio = cpuid_ecx(0x80000007);

	if (rdmsrl_safe(MSR_F15H_CU_MAX_PWR_ACCUMULATOR, &tmp)) {
		pr_err("Failed to read max compute unit power accumulator MSR\n");
		return -ENODEV;
	}

	data->max_cu_acc_power = tmp;

	/*
	 * Milliseconds are a reasonable interval for the measurement.
	 * But it shouldn't set too long here, because several seconds
	 * would cause the read function to hang. So set default
	 * interval as 10 ms.
	 */
	data->power_period = 10;

	return read_registers(data);
}

static int fam15h_power_probe(struct pci_dev *pdev,
			      const struct pci_device_id *id)
{
	struct fam15h_power_data *data;
	struct device *dev = &pdev->dev;
	struct device *hwmon_dev;
	int ret;

	/*
	 * though we ignore every other northbridge, we still have to
	 * do the tweaking on _each_ node in MCM processors as the counters
	 * are working hand-in-hand
	 */
	tweak_runavg_range(pdev);

	if (!should_load_on_this_node(pdev))
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(struct fam15h_power_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = fam15h_power_init_data(pdev, data);
	if (ret)
		return ret;

	data->pdev = pdev;

	data->groups[0] = &data->group;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, "fam15h_power",
							   data,
							   &data->groups[0]);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct pci_device_id fam15h_power_id_table[] = {
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_NB_F4) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_M30H_NB_F4) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_M60H_NB_F4) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_M70H_NB_F4) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_16H_NB_F4) },
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_16H_M30H_NB_F4) },
	{}
};
MODULE_DEVICE_TABLE(pci, fam15h_power_id_table);

static struct pci_driver fam15h_power_driver = {
	.name = "fam15h_power",
	.id_table = fam15h_power_id_table,
	.probe = fam15h_power_probe,
	.resume = fam15h_power_resume,
};

module_pci_driver(fam15h_power_driver);
