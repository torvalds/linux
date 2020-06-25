// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2020 Advanced Micro Devices, Inc.
 */
#include <asm/cpu_device_id.h>

#include <linux/bits.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/hwmon.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/processor.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/topology.h>
#include <linux/types.h>

#define DRVNAME			"amd_energy"

#define ENERGY_PWR_UNIT_MSR	0xC0010299
#define ENERGY_CORE_MSR		0xC001029A
#define ENERGY_PKG_MSR		0xC001029B

#define AMD_ENERGY_UNIT_MASK	0x01F00
#define AMD_ENERGY_MASK		0xFFFFFFFF

struct sensor_accumulator {
	u64 energy_ctr;
	u64 prev_value;
	char label[10];
};

struct amd_energy_data {
	struct hwmon_channel_info energy_info;
	const struct hwmon_channel_info *info[2];
	struct hwmon_chip_info chip;
	struct task_struct *wrap_accumulate;
	/* Lock around the accumulator */
	struct mutex lock;
	/* An accumulator for each core and socket */
	struct sensor_accumulator *accums;
	/* Energy Status Units */
	u64 energy_units;
	int nr_cpus;
	int nr_socks;
	int core_id;
};

static int amd_energy_read_labels(struct device *dev,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel,
				  const char **str)
{
	struct amd_energy_data *data = dev_get_drvdata(dev);

	*str = data->accums[channel].label;
	return 0;
}

static void get_energy_units(struct amd_energy_data *data)
{
	u64 rapl_units;

	rdmsrl_safe(ENERGY_PWR_UNIT_MSR, &rapl_units);
	data->energy_units = (rapl_units & AMD_ENERGY_UNIT_MASK) >> 8;
}

static void accumulate_socket_delta(struct amd_energy_data *data,
				    int sock, int cpu)
{
	struct sensor_accumulator *s_accum;
	u64 input;

	mutex_lock(&data->lock);
	rdmsrl_safe_on_cpu(cpu, ENERGY_PKG_MSR, &input);
	input &= AMD_ENERGY_MASK;

	s_accum = &data->accums[data->nr_cpus + sock];
	if (input >= s_accum->prev_value)
		s_accum->energy_ctr +=
			input - s_accum->prev_value;
	else
		s_accum->energy_ctr += UINT_MAX -
			s_accum->prev_value + input;

	s_accum->prev_value = input;
	mutex_unlock(&data->lock);
}

static void accumulate_core_delta(struct amd_energy_data *data)
{
	struct sensor_accumulator *c_accum;
	u64 input;
	int cpu;

	mutex_lock(&data->lock);
	if (data->core_id >= data->nr_cpus)
		data->core_id = 0;

	cpu = data->core_id;

	if (!cpu_online(cpu))
		goto out;

	rdmsrl_safe_on_cpu(cpu, ENERGY_CORE_MSR, &input);
	input &= AMD_ENERGY_MASK;

	c_accum = &data->accums[cpu];

	if (input >= c_accum->prev_value)
		c_accum->energy_ctr +=
			input - c_accum->prev_value;
	else
		c_accum->energy_ctr += UINT_MAX -
			c_accum->prev_value + input;

	c_accum->prev_value = input;

out:
	data->core_id++;
	mutex_unlock(&data->lock);
}

static void read_accumulate(struct amd_energy_data *data)
{
	int sock;

	for (sock = 0; sock < data->nr_socks; sock++) {
		int cpu;

		cpu = cpumask_first_and(cpu_online_mask,
					cpumask_of_node(sock));

		accumulate_socket_delta(data, sock, cpu);
	}

	accumulate_core_delta(data);
}

static void amd_add_delta(struct amd_energy_data *data, int ch,
			  int cpu, long *val, bool is_core)
{
	struct sensor_accumulator *s_accum, *c_accum;
	u64 input;

	mutex_lock(&data->lock);
	if (!is_core) {
		rdmsrl_safe_on_cpu(cpu, ENERGY_PKG_MSR, &input);
		input &= AMD_ENERGY_MASK;

		s_accum = &data->accums[ch];
		if (input >= s_accum->prev_value)
			input += s_accum->energy_ctr -
				  s_accum->prev_value;
		else
			input += UINT_MAX - s_accum->prev_value +
				  s_accum->energy_ctr;
	} else {
		rdmsrl_safe_on_cpu(cpu, ENERGY_CORE_MSR, &input);
		input &= AMD_ENERGY_MASK;

		c_accum = &data->accums[ch];
		if (input >= c_accum->prev_value)
			input += c_accum->energy_ctr -
				 c_accum->prev_value;
		else
			input += UINT_MAX - c_accum->prev_value +
				 c_accum->energy_ctr;
	}

	/* Energy consumed = (1/(2^ESU) * RAW * 1000000UL) μJoules */
	*val = div64_ul(input * 1000000UL, BIT(data->energy_units));

	mutex_unlock(&data->lock);
}

static int amd_energy_read(struct device *dev,
			   enum hwmon_sensor_types type,
			   u32 attr, int channel, long *val)
{
	struct amd_energy_data *data = dev_get_drvdata(dev);
	int cpu;

	if (channel >= data->nr_cpus) {
		cpu = cpumask_first_and(cpu_online_mask,
					cpumask_of_node
					(channel - data->nr_cpus));
		amd_add_delta(data, channel, cpu, val, false);
	} else {
		cpu = channel;
		if (!cpu_online(cpu))
			return -ENODEV;

		amd_add_delta(data, channel, cpu, val, true);
	}

	return 0;
}

static umode_t amd_energy_is_visible(const void *_data,
				     enum hwmon_sensor_types type,
				     u32 attr, int channel)
{
	return 0444;
}

static int energy_accumulator(void *p)
{
	struct amd_energy_data *data = (struct amd_energy_data *)p;

	while (!kthread_should_stop()) {
		/*
		 * Ignoring the conditions such as
		 * cpu being offline or rdmsr failure
		 */
		read_accumulate(data);

		set_current_state(TASK_INTERRUPTIBLE);
		if (kthread_should_stop())
			break;

		/*
		 * On a 240W system, with default resolution the
		 * Socket Energy status register may wrap around in
		 * 2^32*15.3 e-6/240 = 273.8041 secs (~4.5 mins)
		 *
		 * let us accumulate for every 100secs
		 */
		schedule_timeout(msecs_to_jiffies(100000));
	}
	return 0;
}

static const struct hwmon_ops amd_energy_ops = {
	.is_visible = amd_energy_is_visible,
	.read = amd_energy_read,
	.read_string = amd_energy_read_labels,
};

static int amd_create_sensor(struct device *dev,
			     struct amd_energy_data *data,
			     u8 type, u32 config)
{
	struct hwmon_channel_info *info = &data->energy_info;
	struct sensor_accumulator *accums;
	int i, num_siblings, cpus, sockets;
	u32 *s_config;

	/* Identify the number of siblings per core */
	num_siblings = ((cpuid_ebx(0x8000001e) >> 8) & 0xff) + 1;

	sockets = num_possible_nodes();

	/*
	 * Energy counter register is accessed at core level.
	 * Hence, filterout the siblings.
	 */
	cpus = num_present_cpus() / num_siblings;

	s_config = devm_kcalloc(dev, cpus + sockets,
				sizeof(u32), GFP_KERNEL);
	if (!s_config)
		return -ENOMEM;

	accums = devm_kcalloc(dev, cpus + sockets,
			      sizeof(struct sensor_accumulator),
			      GFP_KERNEL);
	if (!accums)
		return -ENOMEM;

	info->type = type;
	info->config = s_config;

	data->nr_cpus = cpus;
	data->nr_socks = sockets;
	data->accums = accums;

	for (i = 0; i < cpus + sockets; i++) {
		s_config[i] = config;
		if (i < cpus)
			scnprintf(accums[i].label, 10,
				  "Ecore%03u", i);
		else
			scnprintf(accums[i].label, 10,
				  "Esocket%u", (i - cpus));
	}

	return 0;
}

static int amd_energy_probe(struct platform_device *pdev)
{
	struct device *hwmon_dev;
	struct amd_energy_data *data;
	struct device *dev = &pdev->dev;

	data = devm_kzalloc(dev,
			    sizeof(struct amd_energy_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->chip.ops = &amd_energy_ops;
	data->chip.info = data->info;

	dev_set_drvdata(dev, data);
	/* Populate per-core energy reporting */
	data->info[0] = &data->energy_info;
	amd_create_sensor(dev, data, hwmon_energy,
			  HWMON_E_INPUT | HWMON_E_LABEL);

	mutex_init(&data->lock);
	get_energy_units(data);

	hwmon_dev = devm_hwmon_device_register_with_info(dev, DRVNAME,
							 data,
							 &data->chip,
							 NULL);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	data->wrap_accumulate = kthread_run(energy_accumulator, data,
					    "%s", dev_name(hwmon_dev));
	if (IS_ERR(data->wrap_accumulate))
		return PTR_ERR(data->wrap_accumulate);

	return PTR_ERR_OR_ZERO(data->wrap_accumulate);
}

static int amd_energy_remove(struct platform_device *pdev)
{
	struct amd_energy_data *data = dev_get_drvdata(&pdev->dev);

	if (data && data->wrap_accumulate)
		kthread_stop(data->wrap_accumulate);

	return 0;
}

static const struct platform_device_id amd_energy_ids[] = {
	{ .name = DRVNAME, },
	{}
};
MODULE_DEVICE_TABLE(platform, amd_energy_ids);

static struct platform_driver amd_energy_driver = {
	.probe = amd_energy_probe,
	.remove	= amd_energy_remove,
	.id_table = amd_energy_ids,
	.driver = {
		.name = DRVNAME,
	},
};

static struct platform_device *amd_energy_platdev;

static const struct x86_cpu_id cpu_ids[] __initconst = {
	X86_MATCH_VENDOR_FAM(AMD, 0x17, NULL),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, cpu_ids);

static int __init amd_energy_init(void)
{
	int ret;

	if (!x86_match_cpu(cpu_ids))
		return -ENODEV;

	ret = platform_driver_register(&amd_energy_driver);
	if (ret)
		return ret;

	amd_energy_platdev = platform_device_alloc(DRVNAME, 0);
	if (!amd_energy_platdev) {
		platform_driver_unregister(&amd_energy_driver);
		return -ENOMEM;
	}

	ret = platform_device_add(amd_energy_platdev);
	if (ret) {
		platform_device_put(amd_energy_platdev);
		platform_driver_unregister(&amd_energy_driver);
		return ret;
	}

	return ret;
}

static void __exit amd_energy_exit(void)
{
	platform_device_unregister(amd_energy_platdev);
	platform_driver_unregister(&amd_energy_driver);
}

module_init(amd_energy_init);
module_exit(amd_energy_exit);

MODULE_DESCRIPTION("Driver for AMD Energy reporting from RAPL MSR via HWMON interface");
MODULE_AUTHOR("Naveen Krishna Chatradhi <nchatrad@amd.com>");
MODULE_LICENSE("GPL");
