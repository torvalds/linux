// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2017, 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/slab.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <soc/qcom/minidump.h>
#include <soc/qcom/memory_dump.h>
#include <linux/qtee_shmbridge.h>
#include <soc/qcom/secure_buffer.h>
#include <linux/of_device.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of_reserved_mem.h>
#include <linux/qcom_scm.h>
#include <linux/syscore_ops.h>

#define MSM_DUMP_TABLE_VERSION		MSM_DUMP_MAKE_VERSION(2, 0)

#define SCM_CMD_DEBUG_LAR_UNLOCK	0x4

#define CPUSS_REGDUMP			0xEF
#define SPR_DUMP_CPU0			0x1F0
#define SPR_DUMP_CPU1			0x1F1
#define SPR_DUMP_CPU2			0x1F2
#define SPR_DUMP_CPU3			0x1F3
#define SPR_DUMP_CPU4			0x1F4
#define SPR_DUMP_CPU5			0x1F5
#define SPR_DUMP_CPU6			0x1F6
#define SPR_DUMP_CPU7			0x1F7
#define SPR_DATA_HEADER_SIZE	5
#define SPR_DATA_HEADER_TAIL_SIZE	1
#define SPR_INPUT_DATA_TAIL_SIZE	1
#define SPR_INPUT_DATA_SIZE		1
#define SPR_OUTPUT_DATA_SIZE	2
#define MAX_CORE_NUM			8

#define INPUT_DATA_BY_HLOS		0x00C0FFEE
#define FORMAT_VERSION_1		0x1
#define FORMAT_VERSION_2		0x2
#define CORE_REG_NUM_DEFAULT		0x1

#define MAGIC_INDEX			0
#define FORMAT_VERSION_INDEX		1
#define SYS_REG_INPUT_INDEX		2
#define OUTPUT_DUMP_INDEX		3
#define PERCORE_INDEX			4
#define SYSTEM_REGS_INPUT_INDEX	5

#define CMD_REPEAT_READ			(0x2 << 24)
#define CMD_DELAY			(0x1 << 24)
#define CMD_READ			0x0
#define CMD_READ_WORD			0x1
#define CMD_WRITE			0x2
#define CMD_EXTRA			0x3

#define CMD_MASK			0x3
#define OFFSET_MASK			GENMASK(31, 2)
#define EXTRA_CMD_MASK			GENMASK(31, 24)
#define EXTRA_VALUE_MASK		GENMASK(23, 0)
#define MAX_EXTRA_VALUE			0xffffff

struct sprs_dump_data {
	void *dump_vaddr;
	u32 size;
	u32 sprs_data_index;
	u32 used_memory;
};

struct cpuss_regdump_data {
	void *dump_vaddr;
	u32 size;
	u32 core_reg_num;
	u32 core_reg_used_num;
	u32 core_reg_end_index;
	u32 sys_reg_size;
	u32 used_memory;
};

struct cpuss_dump_data {
	struct mutex mutex;
	struct cpuss_regdump_data *cpussregdata;
	struct sprs_dump_data *sprdata[MAX_CORE_NUM];
};

struct reg_dump_data {
	uint32_t magic;
	uint32_t version;
	uint32_t system_regs_input_index;
	uint32_t regdump_output_byte_offset;
};

struct msm_dump_table {
	uint32_t version;
	uint32_t num_entries;
	struct msm_dump_entry entries[MAX_NUM_ENTRIES];
};

struct msm_memory_dump {
	uint64_t table_phys;
	struct msm_dump_table *table;
};

/**
 * Set bit 0 if percore reg dump initialized.
 * Set bit 1 if spr dump initialized.
 */
#define PERCORE_REG_INITIALIZED BIT(0)
#define SPRS_INITIALIZED BIT(1)

static struct msm_memory_dump memdump;
static size_t total_size;

/**
 * reset_sprs_dump_table - reset the sprs dump table
 *
 * This function calculates system_regs_input_index and
 * regdump_output_byte_offset to store into the dump memory.
 * It also updates members of cpudata by the parameter core_reg_num.
 *
 * Returns 0 on success, or -ENOMEM on error of no enough memory.
 */
static int reset_sprs_dump_table(struct device *dev)
{
	int ret = 0;
	struct reg_dump_data *p;
	struct cpuss_dump_data *cpudata = dev_get_drvdata(dev);
	int i = 0;

	if (!cpudata)
		return -EFAULT;

	mutex_lock(&cpudata->mutex);

	for (i = 0; i < MAX_CORE_NUM; i++) {
		if (cpudata->sprdata[i]) {
			cpudata->sprdata[i]->sprs_data_index = 0;
			cpudata->sprdata[i]->used_memory = (SPR_DATA_HEADER_SIZE +
				SPR_INPUT_DATA_TAIL_SIZE) * sizeof(uint32_t);
			memset(cpudata->sprdata[i]->dump_vaddr, 0xDE,
				cpudata->sprdata[i]->size);
			p = (struct reg_dump_data *)cpudata->sprdata[i]->dump_vaddr;
			p->magic = INPUT_DATA_BY_HLOS;
			p->version = FORMAT_VERSION_1;
			p->system_regs_input_index = SYSTEM_REGS_INPUT_INDEX;
			p->regdump_output_byte_offset = (SPR_DATA_HEADER_SIZE +
				SPR_INPUT_DATA_TAIL_SIZE) * sizeof(uint32_t);
			memset((uint32_t *)cpudata->sprdata[i]->dump_vaddr +
				PERCORE_INDEX, 0x0, (SPR_DATA_HEADER_TAIL_SIZE +
				SPR_INPUT_DATA_TAIL_SIZE) * sizeof(uint32_t));
		}
	}

	mutex_unlock(&cpudata->mutex);
	return ret;
}


/**
 * update_reg_dump_table - update the register dump table
 * @core_reg_num: the number of per-core registers
 *
 * This function calculates system_regs_input_index and
 * regdump_output_byte_offset to store into the dump memory.
 * It also updates members of cpudata by the parameter core_reg_num.
 *
 * Returns 0 on success, or -ENOMEM on error of no enough memory.
 */
static int update_reg_dump_table(struct device *dev, u32 core_reg_num)
{
	int ret = 0;
	u32 system_regs_input_index = SYSTEM_REGS_INPUT_INDEX +
			core_reg_num * 2;
	u32 regdump_output_byte_offset = (system_regs_input_index + 1)
			* sizeof(uint32_t);
	struct reg_dump_data *p;
	struct cpuss_dump_data *cpudata = dev_get_drvdata(dev);

	mutex_lock(&cpudata->mutex);

	if (regdump_output_byte_offset >= cpudata->cpussregdata->size ||
			regdump_output_byte_offset / sizeof(uint32_t)
			< system_regs_input_index + 1) {
		ret = -ENOMEM;
		goto err;
	}

	cpudata->cpussregdata->core_reg_num = core_reg_num;
	cpudata->cpussregdata->core_reg_used_num = 0;
	cpudata->cpussregdata->core_reg_end_index = PERCORE_INDEX;
	cpudata->cpussregdata->sys_reg_size = 0;
	cpudata->cpussregdata->used_memory = regdump_output_byte_offset;

	memset(cpudata->cpussregdata->dump_vaddr, 0xDE, cpudata->cpussregdata->size);
	p = (struct reg_dump_data *)cpudata->cpussregdata->dump_vaddr;
	p->magic = INPUT_DATA_BY_HLOS;
	p->version = FORMAT_VERSION_2;
	p->system_regs_input_index = system_regs_input_index;
	p->regdump_output_byte_offset = regdump_output_byte_offset;
	memset((uint32_t *)cpudata->cpussregdata->dump_vaddr + PERCORE_INDEX, 0x0,
			(system_regs_input_index - PERCORE_INDEX + 1)
			* sizeof(uint32_t));

err:
	mutex_unlock(&cpudata->mutex);
	return ret;
}

static ssize_t core_reg_num_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret;
	struct cpuss_dump_data *cpudata = dev_get_drvdata(dev);

	if (!cpudata)
		return -EFAULT;

	mutex_lock(&cpudata->mutex);

	ret = scnprintf(buf, PAGE_SIZE, "%u\n", cpudata->cpussregdata->core_reg_num);

	mutex_unlock(&cpudata->mutex);
	return ret;
}

static ssize_t core_reg_num_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t size)
{
	int ret;
	unsigned int val;
	struct cpuss_dump_data *cpudata = dev_get_drvdata(dev);

	if (kstrtouint(buf, 16, &val))
		return -EINVAL;

	mutex_lock(&cpudata->mutex);

	if (cpudata->cpussregdata->core_reg_used_num || cpudata->cpussregdata->sys_reg_size) {
		dev_err(dev, "Couldn't set core_reg_num, register available in list\n");
		ret = -EPERM;
		goto err;
	}
	if (val == cpudata->cpussregdata->core_reg_num) {
		mutex_unlock(&cpudata->mutex);
		return size;
	}

	mutex_unlock(&cpudata->mutex);

	ret = update_reg_dump_table(dev, val);
	if (ret) {
		dev_err(dev, "Couldn't set core_reg_num, no enough memory\n");
		return ret;
	}

	return size;

err:
	mutex_unlock(&cpudata->mutex);
	return ret;
}
static DEVICE_ATTR_RW(core_reg_num);

/**
 * This function shows configs of per-core and system registers.
 */
static ssize_t register_config_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	char local_buf[64];
	int len = 0, count = 0;
	int index, system_index_start, index_end;
	uint32_t register_offset, val;
	uint32_t *p, cmd;
	struct cpuss_dump_data *cpudata = dev_get_drvdata(dev);

	buf[0] = '\0';

	if (!cpudata)
		return -EFAULT;

	mutex_lock(&cpudata->mutex);

	p = (uint32_t *)cpudata->cpussregdata->dump_vaddr;

	/* print per-core & system registers */
	len = scnprintf(local_buf, 64, "per-core registers:\n");
	strlcat(buf, local_buf, PAGE_SIZE);
	count += len;

	system_index_start = *(p + SYS_REG_INPUT_INDEX);
	index_end = system_index_start +
			cpudata->cpussregdata->sys_reg_size / sizeof(uint32_t) + 1;
	for (index = PERCORE_INDEX; index < index_end;) {
		if (index == system_index_start) {
			len = scnprintf(local_buf, 64, "system registers:\n");
			if ((count + len) > PAGE_SIZE) {
				dev_err(dev, "Couldn't write complete config\n");
				break;
			}

			strlcat(buf, local_buf, PAGE_SIZE);
			count += len;
		}

		register_offset = *(p + index);
		if (register_offset == 0) {
			index++;
			continue;
		}

		cmd = register_offset & CMD_MASK;
		register_offset &= OFFSET_MASK;

		switch (cmd) {
		case CMD_READ:
			val = *(p + index + 1);
			len = scnprintf(local_buf, 64,
			"0x%x, 0x%x, r\n",
			register_offset, val);
			index += 2;
		break;
		case CMD_READ_WORD:
			len = scnprintf(local_buf, 64,
			"0x%x, 0x%x, r\n",
			register_offset, 0x4);
			index++;
		break;
		case CMD_WRITE:
			val = *(p + index + 1);
			len = scnprintf(local_buf, 64,
			"0x%x, 0x%x, w\n",
			register_offset, val);
			index += 2;
		break;
		case CMD_EXTRA:
			val = *(p + index + 1);
			cmd = val & EXTRA_CMD_MASK;
			val &= EXTRA_VALUE_MASK;
			if (cmd == CMD_DELAY)
				len = scnprintf(local_buf, 64,
				"0x%x, 0x%x, d\n",
				register_offset, val);
			else
				len = scnprintf(local_buf, 64,
				"0x%x, 0x%x, R\n",
				register_offset, val);
			index += 2;
		break;
		}

		if ((count + len) > PAGE_SIZE) {
			dev_err(dev, "Couldn't write complete config\n");
			break;
		}

		strlcat(buf, local_buf, PAGE_SIZE);
		count += len;
	}

	mutex_unlock(&cpudata->mutex);
	return count;
}

static int config_cpuss_register(struct device *dev,
		uint32_t *p, uint32_t index, char cmd,
		uint32_t register_offset, uint32_t val)
{
	int ret = 0;

	switch (cmd) {
	case 'r':
		if (val > 4) {
			*(p + index) = register_offset;
			*(p + index + 1) = val;
		} else {
			*(p + index) = register_offset | CMD_READ_WORD;
		}
	break;
	case 'R':
		if (val > MAX_EXTRA_VALUE) {
			dev_err(dev, "repeat read time exceeded the limit\n");
			ret = -EINVAL;
			return ret;
		}
		*(p + index) = register_offset | CMD_EXTRA;
		*(p + index + 1) = val | CMD_REPEAT_READ;
	break;
	case 'd':
		if (val > MAX_EXTRA_VALUE) {
			dev_err(dev, "sleep time exceeded the limit\n");
			ret = -EINVAL;
			return ret;
		}
		*(p + index) = CMD_EXTRA;
		*(p + index + 1) = val | CMD_DELAY;
	break;
	case 'w':
		*(p + index) = register_offset | CMD_WRITE;
		*(p + index + 1) = val;
	break;
	default:
		dev_err(dev, "Don't support this command\n");
		ret = -EINVAL;
	}
	return ret;
}
/**
 * This function sets configs of per-core or system registers.
 */
static ssize_t register_config_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t size)
{
	int ret;
	uint32_t register_offset, val, reserve_size = 4, per_core = 0;
	int nval;
	char cmd;
	uint32_t num_cores;
	u32 extra_memory;
	u32 used_memory;
	u32 system_reg_end_index;
	uint32_t *p;
	struct cpuss_dump_data *cpudata = dev_get_drvdata(dev);

	nval = sscanf(buf, "%x %x %c %u", &register_offset,
				&val, &cmd, &per_core);
	if (nval < 2)
		return -EINVAL;
	if (nval == 2)
		cmd = 'r';
	if (per_core > 1)
		return -EINVAL;
	if (register_offset & 0x3) {
		dev_err(dev, "Invalid address, must be 4 byte aligned\n");
		return -EINVAL;
	}

	if (cmd == 'r' || cmd == 'R') {
		if (val == 0) {
			dev_err(dev, "Invalid length of 0\n");
			return -EINVAL;
		}
		if (cmd == 'r' && val & 0x3) {
			dev_err(dev, "Invalid length, must be 4 byte aligned\n");
			return -EINVAL;
		}
		if (cmd == 'R')
			reserve_size = val * 4;
		else
			reserve_size = val;
	}

	mutex_lock(&cpudata->mutex);

	p = (uint32_t *)cpudata->cpussregdata->dump_vaddr;
	if (per_core) { /* per-core register */
		if (cpudata->cpussregdata->core_reg_used_num ==
				cpudata->cpussregdata->core_reg_num) {
			dev_err(dev, "Couldn't add per-core config, out of range\n");
			ret = -EINVAL;
			goto err;
		}

		num_cores = num_possible_cpus();
		extra_memory = reserve_size * num_cores;
		used_memory = cpudata->cpussregdata->used_memory + extra_memory;
		if (extra_memory / num_cores < reserve_size ||
			used_memory > cpudata->cpussregdata->size ||
			used_memory < cpudata->cpussregdata->used_memory) {
			dev_err(dev, "Couldn't add per-core reg config, no enough memory\n");
			ret = -ENOMEM;
			goto err;
		}

		ret = config_cpuss_register(dev, p, cpudata->cpussregdata->core_reg_end_index,
				cmd, register_offset, val);
		if (ret)
			goto err;

		if (cmd == 'r' && val == 4)
			cpudata->cpussregdata->core_reg_end_index++;
		else
			cpudata->cpussregdata->core_reg_end_index += 2;

		cpudata->cpussregdata->core_reg_used_num++;
		cpudata->cpussregdata->used_memory = used_memory;
	} else { /* system register */
		system_reg_end_index = *(p + SYS_REG_INPUT_INDEX) +
				cpudata->cpussregdata->sys_reg_size / sizeof(uint32_t);

		if (cmd == 'r' && reserve_size == 4)
			extra_memory = sizeof(uint32_t) + reserve_size;
		else
			extra_memory = sizeof(uint32_t) * 2 + reserve_size;

		used_memory = cpudata->cpussregdata->used_memory + extra_memory;
		if (extra_memory < reserve_size ||
				used_memory > cpudata->cpussregdata->size ||
				used_memory < cpudata->cpussregdata->used_memory) {
			dev_err(dev, "Couldn't add system reg config, no enough memory\n");
			ret = -ENOMEM;
			goto err;
		}

		ret = config_cpuss_register(dev, p, system_reg_end_index,
				cmd, register_offset, val);
		if (ret)
			goto err;

		if (cmd == 'r' && val == 4) {
			system_reg_end_index++;
			cpudata->cpussregdata->sys_reg_size += sizeof(uint32_t);
		} else {
			system_reg_end_index += 2;
			cpudata->cpussregdata->sys_reg_size += sizeof(uint32_t) * 2;
		}

		cpudata->cpussregdata->used_memory = used_memory;
		*(p + system_reg_end_index) = 0x0;
		*(p + OUTPUT_DUMP_INDEX) = (system_reg_end_index + 1)
				* sizeof(uint32_t);
	}

	ret = size;

err:
	mutex_unlock(&cpudata->mutex);
	return ret;
}
static DEVICE_ATTR_RW(register_config);

static ssize_t format_version_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret;
	struct reg_dump_data *p;
	struct cpuss_dump_data *cpudata = dev_get_drvdata(dev);

	if (!cpudata)
		return -EFAULT;

	mutex_lock(&cpudata->mutex);
	p = (struct reg_dump_data *)cpudata->cpussregdata->dump_vaddr;
	ret = scnprintf(buf, PAGE_SIZE, "%u\n", p->version);

	mutex_unlock(&cpudata->mutex);
	return ret;
}
static DEVICE_ATTR_RO(format_version);
/**
 * This function resets the register dump table.
 */
static ssize_t register_reset_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t size)
{
	unsigned int val;

	if (kstrtouint(buf, 16, &val))
		return -EINVAL;
	if (val != 1)
		return -EINVAL;

	update_reg_dump_table(dev, CORE_REG_NUM_DEFAULT);

	return size;
}
static DEVICE_ATTR_WO(register_reset);

/**
 * This function shows configs of per-core spr dump.
 */
static ssize_t spr_config_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	char local_buf[64];
	int len = 0, count = 0;
	struct cpuss_dump_data *cpudata = dev_get_drvdata(dev);
	int i = 0, index = 0;
	uint32_t *p;

	buf[0] = '\0';

	if (!cpudata)
		return -EFAULT;

	mutex_lock(&cpudata->mutex);

	len = scnprintf(local_buf, 64, "spr data list below:\n");
	strlcat(buf, local_buf, PAGE_SIZE);
	count += len;

	for (i = 0; i < MAX_CORE_NUM; i++) {
		if (count > PAGE_SIZE) {
			dev_err(dev, "Couldn't write complete config\n");
			break;
		}
		if (!cpudata->sprdata[i]) {
			dev_err(dev, "SPR data pinter for CPU%d is empty\n", i);
			continue;
		}
		p = (uint32_t *)cpudata->sprdata[i]->dump_vaddr;
		len = scnprintf(local_buf, 64, "spr data for CPU[%d] below:\n", i);
		strlcat(buf, local_buf, PAGE_SIZE);
		count += len;
		index = 0;
		while (index < cpudata->sprdata[i]->sprs_data_index) {
			if (count > PAGE_SIZE) {
				dev_err(dev, "Couldn't write complete config\n");
				break;
			}
			len = scnprintf(local_buf, 64, "%d\n", *(p + SPR_DATA_HEADER_SIZE + index));
			strlcat(buf, local_buf, PAGE_SIZE);
			count += len;
			index++;
		}
	}

	mutex_unlock(&cpudata->mutex);
	return count;
}

/**
 * This function sets configs for sprs dump.
 */
static ssize_t spr_config_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t size)
{
	int ret = 0;
	uint32_t spr_data, cpu_num;
	uint32_t index;
	int nval;
	uint32_t *p;
	u32 reserved = 0;
	struct cpuss_dump_data *cpudata = dev_get_drvdata(dev);

	nval = sscanf(buf, "%d %d", &spr_data, &cpu_num);
	if (nval != 2)
		return -EINVAL;
	if (!cpudata)
		return -EFAULT;

	if (cpu_num >= MAX_CORE_NUM) {
		dev_err(dev, "Input the wrong CPU number\n");
		return -EINVAL;
	}
	reserved = (SPR_INPUT_DATA_SIZE + SPR_OUTPUT_DATA_SIZE) * sizeof(uint32_t);

	mutex_lock(&cpudata->mutex);
	if (cpudata->sprdata[cpu_num]) {
		p = (uint32_t *)cpudata->sprdata[cpu_num]->dump_vaddr;
		index = cpudata->sprdata[cpu_num]->sprs_data_index;

		if (cpudata->sprdata[cpu_num]->size >
				cpudata->sprdata[cpu_num]->used_memory + reserved) {
			p = (uint32_t *)cpudata->sprdata[cpu_num]->dump_vaddr;
			*(p + OUTPUT_DUMP_INDEX) = (SPR_DATA_HEADER_SIZE +
				index + SPR_INPUT_DATA_TAIL_SIZE + 1) * sizeof(uint32_t);
			*(p + SPR_DATA_HEADER_SIZE + index) = spr_data;
			*(p + SPR_DATA_HEADER_SIZE + index + 1) = 0;
			cpudata->sprdata[cpu_num]->sprs_data_index++;
			cpudata->sprdata[cpu_num]->used_memory =
				cpudata->sprdata[cpu_num]->used_memory + reserved;
		} else {
			dev_err(dev, "Couldn't add SPR config, no enough memory\n");
			ret = -ENOMEM;
			goto err;
		}
	}
	ret = size;

err:
	mutex_unlock(&cpudata->mutex);
	return ret;
}
static DEVICE_ATTR_RW(spr_config);

/**
 * This function resets the sprs dump table.
 */
static ssize_t sprs_register_reset_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t size)
{
	unsigned int val;

	if (kstrtouint(buf, 16, &val))
		return -EINVAL;
	if (val != 1)
		return -EINVAL;

	reset_sprs_dump_table(dev);

	return size;
}
static DEVICE_ATTR_WO(sprs_register_reset);

static ssize_t sprs_format_version_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	char local_buf[64];
	int len = 0, count = 0, i = 0;
	struct reg_dump_data *p;
	struct cpuss_dump_data *cpudata = dev_get_drvdata(dev);

	buf[0] = '\0';

	if (!cpudata)
		return -EFAULT;

	mutex_lock(&cpudata->mutex);

	for (i = 0; i < MAX_CORE_NUM; i++) {
		if (cpudata->sprdata[i]) {
			p = (struct reg_dump_data *)cpudata->sprdata[i]->dump_vaddr;
			len = scnprintf(local_buf, 64,
				"SPR data format version for cpu%d is %d\n", i, p->version);
			strlcat(buf, local_buf, PAGE_SIZE);
			count += len;
		}
	}

	mutex_unlock(&cpudata->mutex);
	return count;
}
static DEVICE_ATTR_RO(sprs_format_version);


static const struct device_attribute *register_dump_attrs[] = {
	&dev_attr_core_reg_num,
	&dev_attr_register_config,
	&dev_attr_register_reset,
	&dev_attr_format_version,
	NULL,
};

static const struct device_attribute *spr_dump_attrs[] = {
	&dev_attr_spr_config,
	&dev_attr_sprs_register_reset,
	&dev_attr_sprs_format_version,
	NULL,
};

static int memory_dump_create_files(struct device *dev,
			const struct device_attribute **attrs)
{
	int ret = 0;
	int i, j;

	for (i = 0; attrs[i] != NULL; i++) {
		ret = device_create_file(dev, attrs[i]);
		if (ret) {
			dev_err(dev, "Couldn't create sysfs attribute: %s\n",
				attrs[i]->attr.name);
			for (j = 0; j < i; j++)
				device_remove_file(dev, attrs[j]);
			break;
		}
	}
	return ret;
}

static void cpuss_create_nodes(struct platform_device *pdev,
			int initialized)
{
	if (initialized & PERCORE_REG_INITIALIZED) {
		if (memory_dump_create_files(&pdev->dev, register_dump_attrs))
			dev_err(&pdev->dev, "Fail to create files for cpuss register dump\n");
	}
	if (initialized & SPRS_INITIALIZED) {
		if (memory_dump_create_files(&pdev->dev, spr_dump_attrs))
			dev_err(&pdev->dev, "Fail to create files for spr dump\n");
	}
}

uint32_t msm_dump_table_version(void)
{
	return MSM_DUMP_TABLE_VERSION;
}
EXPORT_SYMBOL(msm_dump_table_version);

static int msm_dump_table_register(struct msm_dump_entry *entry)
{
	struct msm_dump_entry *e;
	struct msm_dump_table *table = memdump.table;

	if (!table || table->num_entries >= MAX_NUM_ENTRIES)
		return -EINVAL;

	e = &table->entries[table->num_entries];
	e->id = entry->id;
	e->type = MSM_DUMP_TYPE_TABLE;
	e->addr = entry->addr;
	table->num_entries++;

	return 0;
}

static struct msm_dump_table *msm_dump_get_table(enum msm_dump_table_ids id)
{
	struct msm_dump_table *table = memdump.table;
	int i;
	unsigned long offset;

	if (!table) {
		pr_err("mem dump base table does not exist\n");
		return ERR_PTR(-EINVAL);
	}

	for (i = 0; i < MAX_NUM_ENTRIES; i++) {
		if (table->entries[i].id == id)
			break;
	}
	if (i == MAX_NUM_ENTRIES || !table->entries[i].addr) {
		pr_err("mem dump base table entry %d invalid\n", id);
		return ERR_PTR(-EINVAL);
	}

	offset = table->entries[i].addr - memdump.table_phys;
	/* Get the apps table pointer */
	table = (void *)memdump.table + offset;

	return table;
}

static int msm_dump_data_add_minidump(struct msm_dump_entry *entry)
{
	struct msm_dump_data *data;
	struct md_region md_entry;

	data = (struct msm_dump_data *)(phys_to_virt(entry->addr));

	if (!data->addr || !data->len)
		return -EINVAL;

	if (!strcmp(data->name, "")) {
		pr_debug("Entry name is NULL, Use ID %d for minidump\n",
			entry->id);
		snprintf(md_entry.name, sizeof(md_entry.name), "KMDT0x%X",
			entry->id);
	} else {
		strscpy(md_entry.name, data->name, sizeof(md_entry.name));
	}

	md_entry.phys_addr = data->addr;
	md_entry.virt_addr = (uintptr_t)phys_to_virt(data->addr);
	md_entry.size = data->len;
	md_entry.id = entry->id;

	return msm_minidump_add_region(&md_entry);
}

static int register_dump_table_entry(enum msm_dump_table_ids id,
					struct msm_dump_entry *entry)
{
	struct msm_dump_entry *e;
	struct msm_dump_table *table;

	table = msm_dump_get_table(id);
	if (IS_ERR(table))
		return PTR_ERR(table);

	if (!table || table->num_entries >= MAX_NUM_ENTRIES)
		return -EINVAL;

	e = &table->entries[table->num_entries];
	e->id = entry->id;
	e->type = MSM_DUMP_TYPE_DATA;
	e->addr = entry->addr;
	table->num_entries++;

	return 0;
}

/**
 * msm_dump_data_register - register to dump data and minidump framework
 * @id: ID of the dump table.
 * @entry: dump entry to be registered
 * This api will register the entry passed to dump table and minidump table
 */
int msm_dump_data_register(enum msm_dump_table_ids id,
			   struct msm_dump_entry *entry)
{
	int ret;

	ret = register_dump_table_entry(id, entry);
	if (!ret)
		if (msm_dump_data_add_minidump(entry) < 0)
			pr_err("Failed to add entry in Minidump table\n");

	return ret;
}
EXPORT_SYMBOL(msm_dump_data_register);

/**
 * msm_dump_data_register_nominidump - register to dump data framework
 * @id: ID of the dump table.
 * @entry: dump entry to be registered
 * This api will register the entry passed to dump table only
 */
int msm_dump_data_register_nominidump(enum msm_dump_table_ids id,
			   struct msm_dump_entry *entry)
{
	return register_dump_table_entry(id, entry);
}
EXPORT_SYMBOL(msm_dump_data_register_nominidump);

#define MSM_DUMP_TOTAL_SIZE_OFFSET	0x724
static int init_memdump_imem_area(size_t size)
{
	struct device_node *np;
	void __iomem *imem_base;

	np = of_find_compatible_node(NULL, NULL,
				     "qcom,msm-imem-mem_dump_table");
	if (!np) {
		pr_err("mem dump base table DT node does not exist\n");
		return -ENODEV;
	}

	imem_base = of_iomap(np, 0);
	if (!imem_base) {
		pr_err("mem dump base table imem offset mapping failed\n");
		return -ENOMEM;
	}

	memcpy_toio(imem_base, &memdump.table_phys,
			sizeof(memdump.table_phys));
	memcpy_toio(imem_base + MSM_DUMP_TOTAL_SIZE_OFFSET,
			&size, sizeof(size_t));

	/* Ensure write to imem_base is complete before unmapping */
	mb();
	pr_info("MSM Memory Dump base table set up in IMEM\n");

	iounmap(imem_base);
	return 0;
}

#ifdef CONFIG_HIBERNATION
static void memory_dump_syscore_resume(void)
{
	if (init_memdump_imem_area(total_size))
		pr_err("memdump failed to restore\n");
}

static struct syscore_ops memory_dump_syscore_ops = {
	.resume = memory_dump_syscore_resume,
};
#endif

static int init_memory_dump(void *dump_vaddr, phys_addr_t phys_addr)
{
	struct msm_dump_table *table;
	struct msm_dump_entry entry;
	int ret;

	memdump.table = dump_vaddr;
	memdump.table->version = MSM_DUMP_TABLE_VERSION;
	memdump.table_phys = phys_addr;
	dump_vaddr +=  sizeof(*table);
	phys_addr += sizeof(*table);
	table = dump_vaddr;
	table->version = MSM_DUMP_TABLE_VERSION;
	entry.id = MSM_DUMP_TABLE_APPS;
	entry.addr = phys_addr;
	ret = msm_dump_table_register(&entry);
	if (ret) {
		pr_err("mem dump apps data table register failed\n");
		return ret;
	}
	pr_info("MSM Memory Dump apps data table set up\n");

#ifdef CONFIG_HIBERNATION
	register_syscore_ops(&memory_dump_syscore_ops);
#endif

	return 0;
}

static int mem_dump_reserve_mem(struct device *dev)
{
	struct device_node *mem_node;
	int ret;

	mem_node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (mem_node) {
		ret = of_reserved_mem_device_init_by_idx(dev,
				dev->of_node, 0);
		of_node_put(dev->of_node);
		if (ret) {
			dev_err(dev,
				"Failed to initialize reserved mem, ret %d\n",
				ret);
			return ret;
		}
	}
	return 0;
}

static int cpuss_regdump_init(struct device *dev,
		void *dump_vaddr, u32 size)
{
	struct cpuss_dump_data *cpudata = dev_get_drvdata(dev);

	cpudata->cpussregdata = devm_kzalloc(dev,
		sizeof(struct cpuss_regdump_data), GFP_KERNEL);

	if (cpudata->cpussregdata) {
		cpudata->cpussregdata->dump_vaddr = dump_vaddr;
		cpudata->cpussregdata->size = size;
		return 0;
	}
	return -ENOMEM;
}

static int sprs_dump_init(struct device *dev,
		void *dump_vaddr, u32 size, u32 id)
{
	struct cpuss_dump_data *cpudata = dev_get_drvdata(dev);
	int core_num = 0;

	core_num = id - SPR_DUMP_CPU0;

	cpudata->sprdata[core_num] = devm_kzalloc(dev,
		sizeof(struct sprs_dump_data), GFP_KERNEL);
	if (cpudata->sprdata[core_num]) {
		cpudata->sprdata[core_num]->dump_vaddr = dump_vaddr;
		cpudata->sprdata[core_num]->size = size;
		return 0;
	}
	return -ENOMEM;
}

static int cpuss_dump_init(struct platform_device *pdev,
		void *dump_vaddr, u32 size, u32 id)
{
	struct cpuss_dump_data *cpudata = dev_get_drvdata(&pdev->dev);
	static int initialized;

	if (!cpudata) {
		cpudata = devm_kzalloc(&pdev->dev,
				sizeof(struct cpuss_dump_data), GFP_KERNEL);
		if (cpudata) {
			mutex_init(&cpudata->mutex);
			platform_set_drvdata(pdev, cpudata);
		} else
			return initialized;
	}

	if (id == CPUSS_REGDUMP) {
		if (!cpuss_regdump_init(&pdev->dev, dump_vaddr, size))
			initialized |= PERCORE_REG_INITIALIZED;
	} else {
		if (!sprs_dump_init(&pdev->dev, dump_vaddr, size, id))
			initialized |= SPRS_INITIALIZED;
	}

	return initialized;
}

#define MSM_DUMP_DATA_SIZE sizeof(struct msm_dump_data)
static int mem_dump_alloc(struct platform_device *pdev)
{
	struct device_node *child_node;
	const struct device_node *node = pdev->dev.of_node;
	struct msm_dump_data *dump_data;
	struct msm_dump_entry dump_entry;
	struct md_region md_entry;
	u32 size, id = 0;
	int ret, no_of_nodes;
	dma_addr_t dma_handle;
	phys_addr_t phys_addr, mini_phys_addr;
	struct sg_table mem_dump_sgt;
	void *dump_vaddr, *mini_dump_vaddr;
	uint32_t ns_vmids[] = {VMID_HLOS};
	uint32_t ns_vm_perms[] = {PERM_READ | PERM_WRITE};
	u64 shm_bridge_handle;
	int initialized = 0;

	if (mem_dump_reserve_mem(&pdev->dev) != 0)
		return -ENOMEM;
	total_size = size = ret = no_of_nodes = 0;
	/* For dump table registration with IMEM */
	total_size = sizeof(struct msm_dump_table) * 2;
	for_each_available_child_of_node(node, child_node) {
		ret = of_property_read_u32(child_node, "qcom,dump-size", &size);
		if (ret) {
			dev_err(&pdev->dev, "Unable to find size for %s\n",
					child_node->name);
			continue;
		}

		total_size += size;
		no_of_nodes++;
	}

	total_size += (MSM_DUMP_DATA_SIZE * no_of_nodes);
	total_size = ALIGN(total_size, SZ_4K);
	dump_vaddr = dmam_alloc_coherent(&pdev->dev, total_size,
						&dma_handle, GFP_KERNEL);
	if (!dump_vaddr)
		return -ENOMEM;

	dma_get_sgtable(&pdev->dev, &mem_dump_sgt, dump_vaddr,
						dma_handle, total_size);
	phys_addr = page_to_phys(sg_page(mem_dump_sgt.sgl));
	sg_free_table(&mem_dump_sgt);

	memset(dump_vaddr, 0x0, total_size);
	ret = qtee_shmbridge_register(phys_addr, total_size, ns_vmids,
			ns_vm_perms, 1, PERM_READ|PERM_WRITE, &shm_bridge_handle);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create shm bridge.ret=%d\n", ret);
		return ret;
	}

	ret = init_memory_dump(dump_vaddr, phys_addr);
	if (ret) {
		dev_err(&pdev->dev, "Memory Dump table set up is failed\n");
		qtee_shmbridge_deregister(shm_bridge_handle);
		return ret;
	}

	ret = qcom_scm_assign_dump_table_region(1, phys_addr, total_size);
	if (ret) {
		ret = init_memdump_imem_area(total_size);
		if (ret) {
			qtee_shmbridge_deregister(shm_bridge_handle);
			return ret;
		}
	}

	mini_dump_vaddr = dump_vaddr;
	mini_phys_addr = phys_addr;
	dump_vaddr += (sizeof(struct msm_dump_table) * 2);
	phys_addr += (sizeof(struct msm_dump_table) * 2);
	for_each_available_child_of_node(node, child_node) {
		ret = of_property_read_u32(child_node, "qcom,dump-size", &size);
		if (ret)
			continue;

		ret = of_property_read_u32(child_node, "qcom,dump-id", &id);
		if (ret) {
			dev_err(&pdev->dev, "Unable to find id for %s\n",
					child_node->name);
			continue;
		}

		dump_data = dump_vaddr;
		dump_data->addr = phys_addr + MSM_DUMP_DATA_SIZE;
		dump_data->len = size;
		dump_entry.id = id;
		strscpy(dump_data->name, child_node->name,
					sizeof(dump_data->name));
		dump_entry.addr = phys_addr;
		ret = msm_dump_data_register_nominidump(MSM_DUMP_TABLE_APPS,
					&dump_entry);
		if (ret)
			dev_err(&pdev->dev, "Data dump setup failed, id = %d\n",
				id);

		if ((id == CPUSS_REGDUMP) ||
				((id >= SPR_DUMP_CPU0) && (id <= SPR_DUMP_CPU7)))
			initialized = cpuss_dump_init(pdev,
				(dump_vaddr + MSM_DUMP_DATA_SIZE), size, id);

		dump_vaddr += (size + MSM_DUMP_DATA_SIZE);
		phys_addr += (size  + MSM_DUMP_DATA_SIZE);
	}

	md_entry.phys_addr = mini_phys_addr;
	md_entry.virt_addr = (u64)mini_dump_vaddr;
	md_entry.size = total_size;
	strscpy(md_entry.name, "MEMDUMP", sizeof(md_entry.name));
	if (msm_minidump_add_region(&md_entry) < 0)
		dev_err(&pdev->dev, "Mini dump entry failed id = %d\n", id);

	cpuss_create_nodes(pdev, initialized);

	if (initialized & SPRS_INITIALIZED)
		reset_sprs_dump_table(&pdev->dev);

	return ret;
}

static int mem_dump_probe(struct platform_device *pdev)
{
	int ret;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret < 0)
		return ret;

	ret = mem_dump_alloc(pdev);
	return ret;
}

static const struct of_device_id mem_dump_match_table[] = {
	{.compatible = "qcom,mem-dump",},
	{}
};

static struct platform_driver mem_dump_driver = {
	.probe = mem_dump_probe,
	.driver = {
		.name = "msm_mem_dump",
		.of_match_table = mem_dump_match_table,
	},
};

module_platform_driver(mem_dump_driver);

MODULE_DESCRIPTION("Memory Dump V2 Driver");
MODULE_LICENSE("GPL v2");
