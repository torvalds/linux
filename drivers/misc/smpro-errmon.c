// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ampere Computing SoC's SMpro Error Monitoring Driver
 *
 * Copyright (c) 2022, Ampere Computing LLC
 *
 */

#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* GPI RAS Error Registers */
#define GPI_RAS_ERR		0x7E

/* Core and L2C Error Registers */
#define CORE_CE_ERR_CNT		0x80
#define CORE_CE_ERR_LEN		0x81
#define CORE_CE_ERR_DATA	0x82
#define CORE_UE_ERR_CNT		0x83
#define CORE_UE_ERR_LEN		0x84
#define CORE_UE_ERR_DATA	0x85

/* Memory Error Registers */
#define MEM_CE_ERR_CNT		0x90
#define MEM_CE_ERR_LEN		0x91
#define MEM_CE_ERR_DATA		0x92
#define MEM_UE_ERR_CNT		0x93
#define MEM_UE_ERR_LEN		0x94
#define MEM_UE_ERR_DATA		0x95

/* RAS Error/Warning Registers */
#define ERR_SMPRO_TYPE		0xA0
#define ERR_PMPRO_TYPE		0xA1
#define ERR_SMPRO_INFO_LO	0xA2
#define ERR_SMPRO_INFO_HI	0xA3
#define ERR_SMPRO_DATA_LO	0xA4
#define ERR_SMPRO_DATA_HI	0xA5
#define WARN_SMPRO_INFO_LO	0xAA
#define WARN_SMPRO_INFO_HI	0xAB
#define ERR_PMPRO_INFO_LO	0xA6
#define ERR_PMPRO_INFO_HI	0xA7
#define ERR_PMPRO_DATA_LO	0xA8
#define ERR_PMPRO_DATA_HI	0xA9
#define WARN_PMPRO_INFO_LO	0xAC
#define WARN_PMPRO_INFO_HI	0xAD

/* Boot Stage Register */
#define BOOTSTAGE		0xB0
#define DIMM_SYNDROME_SEL	0xB4
#define DIMM_SYNDROME_ERR	0xB5
#define DIMM_SYNDROME_STAGE	4

/* PCIE Error Registers */
#define PCIE_CE_ERR_CNT		0xC0
#define PCIE_CE_ERR_LEN		0xC1
#define PCIE_CE_ERR_DATA	0xC2
#define PCIE_UE_ERR_CNT		0xC3
#define PCIE_UE_ERR_LEN		0xC4
#define PCIE_UE_ERR_DATA	0xC5

/* Other Error Registers */
#define OTHER_CE_ERR_CNT	0xD0
#define OTHER_CE_ERR_LEN	0xD1
#define OTHER_CE_ERR_DATA	0xD2
#define OTHER_UE_ERR_CNT	0xD8
#define OTHER_UE_ERR_LEN	0xD9
#define OTHER_UE_ERR_DATA	0xDA

/* Event Data Registers */
#define VRD_WARN_FAULT_EVENT_DATA	0x78
#define VRD_HOT_EVENT_DATA		0x79
#define DIMM_HOT_EVENT_DATA		0x7A
#define DIMM_2X_REFRESH_EVENT_DATA	0x96

#define MAX_READ_BLOCK_LENGTH	48

#define RAS_SMPRO_ERR		0
#define RAS_PMPRO_ERR		1

enum RAS_48BYTES_ERR_TYPES {
	CORE_CE_ERR,
	CORE_UE_ERR,
	MEM_CE_ERR,
	MEM_UE_ERR,
	PCIE_CE_ERR,
	PCIE_UE_ERR,
	OTHER_CE_ERR,
	OTHER_UE_ERR,
	NUM_48BYTES_ERR_TYPE,
};

struct smpro_error_hdr {
	u8 count;	/* Number of the RAS errors */
	u8 len;		/* Number of data bytes */
	u8 data;	/* Start of 48-byte data */
	u8 max_cnt;	/* Max num of errors */
};

/*
 * Included Address of registers to get Count, Length of data and Data
 * of the 48 bytes error data
 */
static struct smpro_error_hdr smpro_error_table[] = {
	[CORE_CE_ERR] = {
		.count = CORE_CE_ERR_CNT,
		.len = CORE_CE_ERR_LEN,
		.data = CORE_CE_ERR_DATA,
		.max_cnt = 32
	},
	[CORE_UE_ERR] = {
		.count = CORE_UE_ERR_CNT,
		.len = CORE_UE_ERR_LEN,
		.data = CORE_UE_ERR_DATA,
		.max_cnt = 32
	},
	[MEM_CE_ERR] = {
		.count = MEM_CE_ERR_CNT,
		.len = MEM_CE_ERR_LEN,
		.data = MEM_CE_ERR_DATA,
		.max_cnt = 16
	},
	[MEM_UE_ERR] = {
		.count = MEM_UE_ERR_CNT,
		.len = MEM_UE_ERR_LEN,
		.data = MEM_UE_ERR_DATA,
		.max_cnt = 16
	},
	[PCIE_CE_ERR] = {
		.count = PCIE_CE_ERR_CNT,
		.len = PCIE_CE_ERR_LEN,
		.data = PCIE_CE_ERR_DATA,
		.max_cnt = 96
	},
	[PCIE_UE_ERR] = {
		.count = PCIE_UE_ERR_CNT,
		.len = PCIE_UE_ERR_LEN,
		.data = PCIE_UE_ERR_DATA,
		.max_cnt = 96
	},
	[OTHER_CE_ERR] = {
		.count = OTHER_CE_ERR_CNT,
		.len = OTHER_CE_ERR_LEN,
		.data = OTHER_CE_ERR_DATA,
		.max_cnt = 8
	},
	[OTHER_UE_ERR] = {
		.count = OTHER_UE_ERR_CNT,
		.len = OTHER_UE_ERR_LEN,
		.data = OTHER_UE_ERR_DATA,
		.max_cnt = 8
	},
};

/*
 * List of SCP registers which are used to get
 * one type of RAS Internal errors.
 */
struct smpro_int_error_hdr {
	u8 type;
	u8 info_l;
	u8 info_h;
	u8 data_l;
	u8 data_h;
	u8 warn_l;
	u8 warn_h;
};

static struct smpro_int_error_hdr list_smpro_int_error_hdr[] = {
	[RAS_SMPRO_ERR] = {
		.type = ERR_SMPRO_TYPE,
		.info_l = ERR_SMPRO_INFO_LO,
		.info_h = ERR_SMPRO_INFO_HI,
		.data_l = ERR_SMPRO_DATA_LO,
		.data_h = ERR_SMPRO_DATA_HI,
		.warn_l = WARN_SMPRO_INFO_LO,
		.warn_h = WARN_SMPRO_INFO_HI,
	},
	[RAS_PMPRO_ERR] = {
		.type = ERR_PMPRO_TYPE,
		.info_l = ERR_PMPRO_INFO_LO,
		.info_h = ERR_PMPRO_INFO_HI,
		.data_l = ERR_PMPRO_DATA_LO,
		.data_h = ERR_PMPRO_DATA_HI,
		.warn_l = WARN_PMPRO_INFO_LO,
		.warn_h = WARN_PMPRO_INFO_HI,
	},
};

struct smpro_errmon {
	struct regmap *regmap;
};

enum EVENT_TYPES {
	VRD_WARN_FAULT_EVENT,
	VRD_HOT_EVENT,
	DIMM_HOT_EVENT,
	DIMM_2X_REFRESH_EVENT,
	NUM_EVENTS_TYPE,
};

/* Included Address of event source and data registers */
static u8 smpro_event_table[NUM_EVENTS_TYPE] = {
	VRD_WARN_FAULT_EVENT_DATA,
	VRD_HOT_EVENT_DATA,
	DIMM_HOT_EVENT_DATA,
	DIMM_2X_REFRESH_EVENT_DATA,
};

static ssize_t smpro_event_data_read(struct device *dev,
				     struct device_attribute *da, char *buf,
				     int channel)
{
	struct smpro_errmon *errmon = dev_get_drvdata(dev);
	s32 event_data;
	int ret;

	ret = regmap_read(errmon->regmap, smpro_event_table[channel], &event_data);
	if (ret)
		return ret;
	/* Clear event after read */
	if (event_data != 0)
		regmap_write(errmon->regmap, smpro_event_table[channel], event_data);

	return sysfs_emit(buf, "%04x\n", event_data);
}

static ssize_t smpro_overflow_data_read(struct device *dev, struct device_attribute *da,
					char *buf, int channel)
{
	struct smpro_errmon *errmon = dev_get_drvdata(dev);
	struct smpro_error_hdr *err_info;
	s32 err_count;
	int ret;

	err_info = &smpro_error_table[channel];

	ret = regmap_read(errmon->regmap, err_info->count, &err_count);
	if (ret)
		return ret;

	/* Bit 8 indicates the overflow status */
	return sysfs_emit(buf, "%d\n", (err_count & BIT(8)) ? 1 : 0);
}

static ssize_t smpro_error_data_read(struct device *dev, struct device_attribute *da,
				     char *buf, int channel)
{
	struct smpro_errmon *errmon = dev_get_drvdata(dev);
	unsigned char err_data[MAX_READ_BLOCK_LENGTH];
	struct smpro_error_hdr *err_info;
	s32 err_count, err_length;
	int ret;

	err_info = &smpro_error_table[channel];

	ret = regmap_read(errmon->regmap, err_info->count, &err_count);
	/* Error count is the low byte */
	err_count &= 0xff;
	if (ret || !err_count || err_count > err_info->max_cnt)
		return ret;

	ret = regmap_read(errmon->regmap, err_info->len, &err_length);
	if (ret || err_length <= 0)
		return ret;

	if (err_length > MAX_READ_BLOCK_LENGTH)
		err_length = MAX_READ_BLOCK_LENGTH;

	memset(err_data, 0x00, MAX_READ_BLOCK_LENGTH);
	ret = regmap_noinc_read(errmon->regmap, err_info->data, err_data, err_length);
	if (ret < 0)
		return ret;

	/* clear the error */
	ret = regmap_write(errmon->regmap, err_info->count, 0x100);
	if (ret)
		return ret;
	/*
	 * The output of Core/Memory/PCIe/Others UE/CE errors follows the format
	 * specified in section 5.8.1 CE/UE Error Data record in
	 * Altra SOC BMC Interface specification.
	 */
	return sysfs_emit(buf, "%*phN\n", MAX_READ_BLOCK_LENGTH, err_data);
}

/*
 * Output format:
 * <4-byte hex value of error info><4-byte hex value of error extensive data>
 * Where:
 *   + error info : The error information
 *   + error data : Extensive data (32 bits)
 * Reference to section 5.10 RAS Internal Error Register Definition in
 * Altra SOC BMC Interface specification
 */
static ssize_t smpro_internal_err_read(struct device *dev, struct device_attribute *da,
				       char *buf, int channel)
{
	struct smpro_errmon *errmon = dev_get_drvdata(dev);
	struct smpro_int_error_hdr *err_info;
	unsigned int err[4] = { 0 };
	unsigned int err_type;
	unsigned int val;
	int ret;

	/* read error status */
	ret = regmap_read(errmon->regmap, GPI_RAS_ERR, &val);
	if (ret)
		return ret;

	if ((channel == RAS_SMPRO_ERR && !(val & BIT(0))) ||
	    (channel == RAS_PMPRO_ERR && !(val & BIT(1))))
		return 0;

	err_info = &list_smpro_int_error_hdr[channel];
	ret = regmap_read(errmon->regmap, err_info->type, &val);
	if (ret)
		return ret;

	err_type = (val & BIT(1)) ? BIT(1) :
		   (val & BIT(2)) ? BIT(2) : 0;

	if (!err_type)
		return 0;

	ret = regmap_read(errmon->regmap, err_info->info_l, err + 1);
	if (ret)
		return ret;

	ret = regmap_read(errmon->regmap, err_info->info_h, err);
	if (ret)
		return ret;

	if (err_type & BIT(2)) {
		/* Error with data type */
		ret = regmap_read(errmon->regmap, err_info->data_l, err + 3);
		if (ret)
			return ret;

		ret = regmap_read(errmon->regmap, err_info->data_h, err + 2);
		if (ret)
			return ret;
	}

	/* clear the read errors */
	ret = regmap_write(errmon->regmap, err_info->type, err_type);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%*phN\n", (int)sizeof(err), err);
}

/*
 * Output format:
 * <4-byte hex value of warining info>
 * Reference to section 5.10 RAS Internal Error Register Definition in
 * Altra SOC BMC Interface specification
 */
static ssize_t smpro_internal_warn_read(struct device *dev, struct device_attribute *da,
					char *buf, int channel)
{
	struct smpro_errmon *errmon = dev_get_drvdata(dev);
	struct smpro_int_error_hdr *err_info;
	unsigned int warn[2] = { 0 };
	unsigned int val;
	int ret;

	/* read error status */
	ret = regmap_read(errmon->regmap, GPI_RAS_ERR, &val);
	if (ret)
		return ret;

	if ((channel == RAS_SMPRO_ERR && !(val & BIT(0))) ||
	    (channel == RAS_PMPRO_ERR && !(val & BIT(1))))
		return 0;

	err_info = &list_smpro_int_error_hdr[channel];
	ret = regmap_read(errmon->regmap, err_info->type, &val);
	if (ret)
		return ret;

	if (!(val & BIT(0)))
		return 0;

	ret = regmap_read(errmon->regmap, err_info->warn_l, warn + 1);
	if (ret)
		return ret;

	ret = regmap_read(errmon->regmap, err_info->warn_h, warn);
	if (ret)
		return ret;

	/* clear the warning */
	ret = regmap_write(errmon->regmap, err_info->type, BIT(0));
	if (ret)
		return ret;

	return sysfs_emit(buf, "%*phN\n", (int)sizeof(warn), warn);
}

#define ERROR_OVERFLOW_RO(_error, _index) \
	static ssize_t overflow_##_error##_show(struct device *dev,            \
						struct device_attribute *da,   \
						char *buf)                     \
	{                                                                      \
		return smpro_overflow_data_read(dev, da, buf, _index);         \
	}                                                                      \
	static DEVICE_ATTR_RO(overflow_##_error)

ERROR_OVERFLOW_RO(core_ce, CORE_CE_ERR);
ERROR_OVERFLOW_RO(core_ue, CORE_UE_ERR);
ERROR_OVERFLOW_RO(mem_ce, MEM_CE_ERR);
ERROR_OVERFLOW_RO(mem_ue, MEM_UE_ERR);
ERROR_OVERFLOW_RO(pcie_ce, PCIE_CE_ERR);
ERROR_OVERFLOW_RO(pcie_ue, PCIE_UE_ERR);
ERROR_OVERFLOW_RO(other_ce, OTHER_CE_ERR);
ERROR_OVERFLOW_RO(other_ue, OTHER_UE_ERR);

#define ERROR_RO(_error, _index) \
	static ssize_t error_##_error##_show(struct device *dev,            \
					     struct device_attribute *da,   \
					     char *buf)                     \
	{                                                                   \
		return smpro_error_data_read(dev, da, buf, _index);         \
	}                                                                   \
	static DEVICE_ATTR_RO(error_##_error)

ERROR_RO(core_ce, CORE_CE_ERR);
ERROR_RO(core_ue, CORE_UE_ERR);
ERROR_RO(mem_ce, MEM_CE_ERR);
ERROR_RO(mem_ue, MEM_UE_ERR);
ERROR_RO(pcie_ce, PCIE_CE_ERR);
ERROR_RO(pcie_ue, PCIE_UE_ERR);
ERROR_RO(other_ce, OTHER_CE_ERR);
ERROR_RO(other_ue, OTHER_UE_ERR);

static ssize_t error_smpro_show(struct device *dev, struct device_attribute *da, char *buf)
{
	return smpro_internal_err_read(dev, da, buf, RAS_SMPRO_ERR);
}
static DEVICE_ATTR_RO(error_smpro);

static ssize_t error_pmpro_show(struct device *dev, struct device_attribute *da, char *buf)
{
	return smpro_internal_err_read(dev, da, buf, RAS_PMPRO_ERR);
}
static DEVICE_ATTR_RO(error_pmpro);

static ssize_t warn_smpro_show(struct device *dev, struct device_attribute *da, char *buf)
{
	return smpro_internal_warn_read(dev, da, buf, RAS_SMPRO_ERR);
}
static DEVICE_ATTR_RO(warn_smpro);

static ssize_t warn_pmpro_show(struct device *dev, struct device_attribute *da, char *buf)
{
	return smpro_internal_warn_read(dev, da, buf, RAS_PMPRO_ERR);
}
static DEVICE_ATTR_RO(warn_pmpro);

#define EVENT_RO(_event, _index) \
	static ssize_t event_##_event##_show(struct device *dev,            \
					     struct device_attribute *da,   \
					     char *buf)                     \
	{                                                                   \
		return smpro_event_data_read(dev, da, buf, _index);         \
	}                                                                   \
	static DEVICE_ATTR_RO(event_##_event)

EVENT_RO(vrd_warn_fault, VRD_WARN_FAULT_EVENT);
EVENT_RO(vrd_hot, VRD_HOT_EVENT);
EVENT_RO(dimm_hot, DIMM_HOT_EVENT);
EVENT_RO(dimm_2x_refresh, DIMM_2X_REFRESH_EVENT);

static ssize_t smpro_dimm_syndrome_read(struct device *dev, struct device_attribute *da,
					char *buf, unsigned int slot)
{
	struct smpro_errmon *errmon = dev_get_drvdata(dev);
	unsigned int data;
	int ret;

	ret = regmap_read(errmon->regmap, BOOTSTAGE, &data);
	if (ret)
		return ret;

	/* check for valid stage */
	data = (data >> 8) & 0xff;
	if (data != DIMM_SYNDROME_STAGE)
		return ret;

	/* Write the slot ID to retrieve Error Syndrome */
	ret = regmap_write(errmon->regmap, DIMM_SYNDROME_SEL, slot);
	if (ret)
		return ret;

	/* Read the Syndrome error */
	ret = regmap_read(errmon->regmap, DIMM_SYNDROME_ERR, &data);
	if (ret || !data)
		return ret;

	return sysfs_emit(buf, "%04x\n", data);
}

#define EVENT_DIMM_SYNDROME(_slot) \
	static ssize_t event_dimm##_slot##_syndrome_show(struct device *dev,          \
							 struct device_attribute *da, \
							 char *buf)                   \
	{                                                                             \
		return smpro_dimm_syndrome_read(dev, da, buf, _slot);                 \
	}                                                                             \
	static DEVICE_ATTR_RO(event_dimm##_slot##_syndrome)

EVENT_DIMM_SYNDROME(0);
EVENT_DIMM_SYNDROME(1);
EVENT_DIMM_SYNDROME(2);
EVENT_DIMM_SYNDROME(3);
EVENT_DIMM_SYNDROME(4);
EVENT_DIMM_SYNDROME(5);
EVENT_DIMM_SYNDROME(6);
EVENT_DIMM_SYNDROME(7);
EVENT_DIMM_SYNDROME(8);
EVENT_DIMM_SYNDROME(9);
EVENT_DIMM_SYNDROME(10);
EVENT_DIMM_SYNDROME(11);
EVENT_DIMM_SYNDROME(12);
EVENT_DIMM_SYNDROME(13);
EVENT_DIMM_SYNDROME(14);
EVENT_DIMM_SYNDROME(15);

static struct attribute *smpro_errmon_attrs[] = {
	&dev_attr_overflow_core_ce.attr,
	&dev_attr_overflow_core_ue.attr,
	&dev_attr_overflow_mem_ce.attr,
	&dev_attr_overflow_mem_ue.attr,
	&dev_attr_overflow_pcie_ce.attr,
	&dev_attr_overflow_pcie_ue.attr,
	&dev_attr_overflow_other_ce.attr,
	&dev_attr_overflow_other_ue.attr,
	&dev_attr_error_core_ce.attr,
	&dev_attr_error_core_ue.attr,
	&dev_attr_error_mem_ce.attr,
	&dev_attr_error_mem_ue.attr,
	&dev_attr_error_pcie_ce.attr,
	&dev_attr_error_pcie_ue.attr,
	&dev_attr_error_other_ce.attr,
	&dev_attr_error_other_ue.attr,
	&dev_attr_error_smpro.attr,
	&dev_attr_error_pmpro.attr,
	&dev_attr_warn_smpro.attr,
	&dev_attr_warn_pmpro.attr,
	&dev_attr_event_vrd_warn_fault.attr,
	&dev_attr_event_vrd_hot.attr,
	&dev_attr_event_dimm_hot.attr,
	&dev_attr_event_dimm_2x_refresh.attr,
	&dev_attr_event_dimm0_syndrome.attr,
	&dev_attr_event_dimm1_syndrome.attr,
	&dev_attr_event_dimm2_syndrome.attr,
	&dev_attr_event_dimm3_syndrome.attr,
	&dev_attr_event_dimm4_syndrome.attr,
	&dev_attr_event_dimm5_syndrome.attr,
	&dev_attr_event_dimm6_syndrome.attr,
	&dev_attr_event_dimm7_syndrome.attr,
	&dev_attr_event_dimm8_syndrome.attr,
	&dev_attr_event_dimm9_syndrome.attr,
	&dev_attr_event_dimm10_syndrome.attr,
	&dev_attr_event_dimm11_syndrome.attr,
	&dev_attr_event_dimm12_syndrome.attr,
	&dev_attr_event_dimm13_syndrome.attr,
	&dev_attr_event_dimm14_syndrome.attr,
	&dev_attr_event_dimm15_syndrome.attr,
	NULL
};

ATTRIBUTE_GROUPS(smpro_errmon);

static int smpro_errmon_probe(struct platform_device *pdev)
{
	struct smpro_errmon *errmon;

	errmon = devm_kzalloc(&pdev->dev, sizeof(struct smpro_errmon), GFP_KERNEL);
	if (!errmon)
		return -ENOMEM;

	platform_set_drvdata(pdev, errmon);

	errmon->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!errmon->regmap)
		return -ENODEV;

	return 0;
}

static struct platform_driver smpro_errmon_driver = {
	.probe          = smpro_errmon_probe,
	.driver = {
		.name   = "smpro-errmon",
		.dev_groups = smpro_errmon_groups,
	},
};

module_platform_driver(smpro_errmon_driver);

MODULE_AUTHOR("Tung Nguyen <tung.nguyen@amperecomputing.com>");
MODULE_AUTHOR("Thinh Pham <thinh.pham@amperecomputing.com>");
MODULE_AUTHOR("Hoang Nguyen <hnguyen@amperecomputing.com>");
MODULE_AUTHOR("Thu Nguyen <thu@os.amperecomputing.com>");
MODULE_AUTHOR("Quan Nguyen <quan@os.amperecomputing.com>");
MODULE_DESCRIPTION("Ampere Altra SMpro driver");
MODULE_LICENSE("GPL");
