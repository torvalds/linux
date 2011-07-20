/*
 * tps65910-regulator.c -- support regulators in tps65910x family chips
 *
 *
 * Copyright (C) 2010 Mistral Solutions Pvt Ltd <www.mistralsolutions.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/i2c/tps65910.h>

#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif

/*
 * The TPS65910x family chips include power management, a GPIO
 * RTC. These chips are often used in AM35xx-based systems.
 *
 * This driver implements software-based resource control for various
 * voltage regulators.  This is usually augmented with state machine
 * based control.
 */


struct tps65910reg_info {
	/* tps65910 resource ID, for resource control state machine */
	u8                      id;
	/* voltage in mV = table[VSEL]; table_len must be a power-of-two */
	u8                      table_len;
	const u16               *table;

	/* regulator specific turn-on delay */
	u32                     delay;
	/* chip constraints on regulator behavior */
	u16                     min_mV;
	u16                     max_mV;
	/* used by regulator core */
	struct regulator_desc   desc;
};


/* Supported voltage values for regulators */

/* TPS65910  VIO */
static const u16 VIO_VSEL_table[] = {
	1500, 1800, 2500, 3300,
};

/* TPS65910 VDD1 and VDD2 */
/* value round off 12.5 is made as 12 */
static const u16 VDD1_VSEL_table[] = {
	   0,  600,  600,  600,  612,  625,  637,  650,
	 662,  675,  687,  700,  712,  725,  737,  750,
	 762,  775,  787,  800,  812,  825,  837,  850,
	 862,  875,  887,  900,  912,  925,  937,  950,
	 962,  975,  987, 1000, 1012, 1025, 1037, 1050,
	1062, 1075, 1087, 1100, 1112, 1125, 1137, 1150,
	1162, 1175, 1187, 1200, 1212, 1225, 1237, 1250,
	1262, 1275, 1287, 1300, 1312, 1325, 1337, 1350,
	1362, 1375, 1387, 1400, 1412, 1425, 1437, 1450,
	1462, 1475, 1487, 1500,
};

static const u16 VDD2_VSEL_table[] = {
	   0,  600,  600,  600,  612,  625,  637,  650,
	 662,  675,  687,  700,  712,  725,  737,  750,
	 762,  775,  787,  800,  812,  825,  837,  850,
	 862,  875,  887,  900,  912,  925,  937,  950,
	 962,  975,  987, 1000, 1012, 1025, 1037, 1050,
	1062, 1075, 1087, 1100, 1112, 1125, 1137, 1150,
	1162, 1175, 1187, 1200, 1212, 1225, 1237, 1250,
	1262, 1275, 1287, 1300, 1312, 1325, 1337, 1350,
	1362, 1375, 1387, 1400, 1412, 1425, 1437, 1450,
	1462, 1475, 1487, 1500,
};

/* TPS65910 VDD3 */
static const u16 VDD3_VSEL_table[] = {
	5000,
};

/* VDIG1 */
static const u16 VDIG1_VSEL_table[] = {
	1200, 1500, 1800, 2700,
};

/* VDIG2 */
static const u16 VDIG2_VSEL_table[] = {
	1000, 1100, 1200, 1800,
};

/* VAUX33 */
static const u16 VAUX33_VSEL_table[] = {
	1800, 2000, 2800, 3300,
};

/* VMMC */
static const u16 VMMC_VSEL_table[] = {
	1800, 2800, 3000, 3300,
};

/* VAUX1 */
static const u16 VAUX1_VSEL_table[] = {
	1800, 2000, 2800, 3300,
};

/* VAUX2 */
static const u16 VAUX2_VSEL_table[] = {
	1800, 2800, 2900, 3300,
};

/* VDAC */
static const u16 VDAC_VSEL_table[] = {
	1800, 2600, 2800, 2850,
};


/* VPLL */
static const u16 VPLL_VSEL_table[] = {
	1000, 1100, 1800, 2500,
};

/* VRTC, supports only enable/disable */
static const u16 VRTC_VSEL_table[] = {
	1800,
};

static inline int
tps65910reg_read(struct tps65910reg_info *info, unsigned slave_addr,
		u8 offset)
{
	u8 value;
	int status;
	status = tps65910_i2c_read_u8(slave_addr, &value, offset);

	return (status < 0) ? status : value;
}

static inline int
tps65910reg_write(struct tps65910reg_info *info, unsigned slave_addr,
		u8 offset, u8 value)
{
	if (0 == tps65910_i2c_write_u8(slave_addr, value, offset))
		return 0;
	else
		return -1;
}

static u8 tps65910reg_find_offset(u8 regulator_id)
{
	u8 offset = 0;

	switch (regulator_id) {

	case TPS65910_VIO:
		offset =  TPS65910_REG_VIO;
		break;
	case TPS65910_VDD1:
		offset =  TPS65910_REG_VDD1_OP;
		break;
	case TPS65910_VDD2:
		offset =  TPS65910_REG_VDD2_OP;
		break;
	case TPS65910_VDD3:
		offset =  TPS65910_REG_VDD3;
		break;
	case TPS65910_VDIG1:
		offset =  TPS65910_REG_VDIG1;
		break;
	case  TPS65910_VDIG2:
		offset =  TPS65910_REG_VDIG2;
		break;
	case TPS65910_VAUX33:
		offset =  TPS65910_REG_VAUX33;
		break;
	case TPS65910_VMMC:
		offset =  TPS65910_REG_VMMC;
		break;
	case TPS65910_VAUX1:
		offset =  TPS65910_REG_VAUX1;
		break;
	case TPS65910_VAUX2:
		offset =  TPS65910_REG_VAUX2;
		break;
	case TPS65910_VDAC:
		offset =  TPS65910_REG_VDAC;
		break;
	case TPS65910_VPLL:
		offset =  TPS65910_REG_VPLL;
		break;
	}
	return offset;
}

static int tps65910reg_is_enabled(struct regulator_dev *rdev)
{
	int    val;
	u8    offset;

	struct tps65910reg_info *info = rdev_get_drvdata(rdev);

	offset = tps65910reg_find_offset(info->id);

	val = tps65910reg_read(info, TPS65910_I2C_ID0, offset);
	if (val < 0) {
		printk(KERN_ERR "Unable to read TPS65910 Reg at offset 0x%x= \
				\n", offset);
		return -EIO;
	}
	if ((val & TPS65910_REG_OHP) || (val & TPS65910_REG_OLP))
		return 1;
	else
		return 0;
}


static int tps65910reg_enable(struct regulator_dev *rdev)
{
	int	val;
	u8   	offset;
	struct tps65910reg_info *info = rdev_get_drvdata(rdev);

	offset = tps65910reg_find_offset(info->id);

	val = tps65910reg_read(info, TPS65910_I2C_ID0, offset);

	if (val < 0) {

		printk(KERN_ERR "Unable to read TPS65910 Reg at offset = 0x%x \
				\n", offset);
		return -EIO;
	}
	val |= TPS65910_REG_OHP;

	DBG("%s: enable regulator id=%d\n", __FUNCTION__, info->id);

	return tps65910reg_write(info, TPS65910_I2C_ID0, offset, val);
}

static int tps65910reg_disable(struct regulator_dev *rdev)
{
	int	val;
	u8   	offset;

	struct tps65910reg_info *info = rdev_get_drvdata(rdev);

	offset = tps65910reg_find_offset(info->id);

	val = tps65910reg_read(info, TPS65910_I2C_ID0, offset);

	if (val < 0) {

		printk(KERN_ERR "Unable to read TPS65910 Reg at offset = \
			 0x%x\n", offset);
		return -EIO;
	}
	val &= TPS65910_REG_OFF_00;

	DBG("%s: disable regulator id=%d\n", __FUNCTION__, info->id);

	return tps65910reg_write(info, TPS65910_I2C_ID0, offset, val);
}

static int tps65910reg_get_status(struct regulator_dev *rdev)
{
	int     val;
	u8      offset;
	u8 	ret;
	struct tps65910reg_info *info = rdev_get_drvdata(rdev);

	offset = tps65910reg_find_offset(info->id);

	val = tps65910reg_read(info, TPS65910_I2C_ID0, offset);

	if (val < 0) {

		printk(KERN_ERR "Unable to read TPS65910 Reg at offset = \
			0x%x\n", offset);
		return -EIO;
	}
	switch ((val & SUPPLY_STATE_FLAG)) {

	case TPS65910_REG_OFF_00:
	case TPS65910_REG_OFF_10:
		ret =  REGULATOR_STATUS_OFF;
		break;
	case TPS65910_REG_OHP:
	case TPS65910_REG_OLP:
		ret =  REGULATOR_STATUS_ON;
		break;
	default:
		ret =  REGULATOR_STATUS_OFF;
	}
	return ret;
}


static int tps65910reg_set_mode(struct regulator_dev *rdev, unsigned mode)
{
	struct 	tps65910reg_info   *info = rdev_get_drvdata(rdev);
	u8      offset;
	u8 	val;

	offset = tps65910reg_find_offset(info->id);
	val = tps65910reg_read(info, TPS65910_I2C_ID0, offset);

	if (val < 0) {
		printk(KERN_ERR"Unable to read TPS65910 Reg at offset \
			 = 0x%x\n", offset);
		return -EIO;
	}

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		return tps65910reg_write(info, TPS65910_I2C_ID0, offset,
						(val | TPS65910_REG_OHP));
	case REGULATOR_MODE_STANDBY:
		return tps65910reg_write(info, TPS65910_I2C_ID0, offset,
						(val | TPS65910_REG_OLP));
	default:
		return -EINVAL;
	}
}

static
int tps65910_ldo_list_voltage(struct regulator_dev *rdev, unsigned index)
{
	struct tps65910reg_info      *info = rdev_get_drvdata(rdev);
	int    mV = info->table[index];
	return mV * 1000;
}

static int get_voltage_index(int ldo_id, int uv)
{
	int i = 0;
	int size = 0;
	u16 *ptr = NULL;
	
	uv = uv/1000;

	if (((ldo_id == TPS65910_VDD1) || (ldo_id == TPS65910_VDD2))) {
		for (i = 0; i < ARRAY_SIZE(VDD1_VSEL_table); i++) {
			if (VDD1_VSEL_table[i] == uv) {
				DBG("%s: regulator id=%d, volage=%d, get index=%d\n", __FUNCTION__, ldo_id, uv, i);
				return i;
			}
		}
		if (i == ARRAY_SIZE(VDD1_VSEL_table)) {
			DBG("%s: regulator id=%d, can't find volage index\n", __FUNCTION__, ldo_id);
			return -1;
		}
	}

	/* Lookup table to match LDO volatge to Index*/
	switch (ldo_id) {

	case TPS65910_VIO:
		ptr = (u16 *)&VIO_VSEL_table[0];
		size = ARRAY_SIZE(VIO_VSEL_table);
		break;
	case TPS65910_VDIG1:
		ptr = (u16 *)&VDIG1_VSEL_table[0];
		size = ARRAY_SIZE(VDIG1_VSEL_table);
		break;
	case TPS65910_VDIG2:
		ptr = (u16 *)&VDIG2_VSEL_table[0];
		size = ARRAY_SIZE(VDIG2_VSEL_table);
		break;
	case TPS65910_VAUX33:
		ptr = (u16 *)&VAUX33_VSEL_table[0];
		size = ARRAY_SIZE(VAUX33_VSEL_table);
		break;
	case TPS65910_VMMC:
		ptr = (u16 *)&VMMC_VSEL_table[0];
		size = ARRAY_SIZE(VMMC_VSEL_table);
		break;
	case TPS65910_VAUX1:
		ptr = (u16 *)&VAUX1_VSEL_table[0];
		size = ARRAY_SIZE(VAUX1_VSEL_table);
		break;
	case TPS65910_VAUX2:
		ptr = (u16 *)&VAUX2_VSEL_table[0];
		size = ARRAY_SIZE(VAUX2_VSEL_table);
		break;
	case TPS65910_VDAC:
		ptr = (u16 *)&VDAC_VSEL_table[0];
		size = ARRAY_SIZE(VDAC_VSEL_table);
		break;
	case TPS65910_VPLL:
		ptr = (u16 *)&VPLL_VSEL_table[0];
		size = ARRAY_SIZE(VPLL_VSEL_table);
		break;
	default:
		ptr = NULL;
		break;
	}

	if (ptr != NULL) {
		for (i = 0; i < size; i++) {
			if (*ptr++  == uv) {
				DBG("%s: regulator id=%d, volage=%d, get index=%d\n", __FUNCTION__, ldo_id, uv, i);
				return i;
			}
		}
	}
	DBG("%s: regulator id=%d, can't find volage index\n", __FUNCTION__, ldo_id);

	if (ptr == NULL || i == size)
		return -1;
	/* For warning */
	return -1;
}

static int get_index_voltage(int ldo_id, int index)
{
	int vsel;
	int size = 0;
	u16 *ptr = NULL;
	
	/* Get the index of voltage value from Reg and map to table */
	if (ldo_id == TPS65910_VDD1 || ldo_id == TPS65910_VDD2) {
		index &= 0x7F;
		ptr = (u16 *)&VDD1_VSEL_table[0];
		size = ARRAY_SIZE(VDD1_VSEL_table);
		/* For VDD1 and VDD2 */
		if (index >= size) {
			vsel = size - 1;
		} else {
			vsel = index;
		}
	} else {
		vsel = (index & 0xF3);
		vsel = (vsel >> 2);

		/* Lookup table to match LDO volatge to Index*/
		switch (ldo_id) {
		
		case TPS65910_VIO:
			ptr = (u16 *)&VIO_VSEL_table[0];
			size = ARRAY_SIZE(VIO_VSEL_table);
			break;
		case TPS65910_VDIG1:
			ptr = (u16 *)&VDIG1_VSEL_table[0];
			size = ARRAY_SIZE(VDIG1_VSEL_table);
			break;
		case TPS65910_VDIG2:
			ptr = (u16 *)&VDIG2_VSEL_table[0];
			size = ARRAY_SIZE(VDIG2_VSEL_table);
			break;
		case TPS65910_VAUX33:
			ptr = (u16 *)&VAUX33_VSEL_table[0];
			size = ARRAY_SIZE(VAUX33_VSEL_table);
			break;
		case TPS65910_VMMC:
			ptr = (u16 *)&VMMC_VSEL_table[0];
			size = ARRAY_SIZE(VMMC_VSEL_table);
			break;
		case TPS65910_VAUX1:
			ptr = (u16 *)&VAUX1_VSEL_table[0];
			size = ARRAY_SIZE(VAUX1_VSEL_table);
			break;
		case TPS65910_VAUX2:
			ptr = (u16 *)&VAUX2_VSEL_table[0];
			size = ARRAY_SIZE(VAUX2_VSEL_table);
			break;
		case TPS65910_VDAC:
			ptr = (u16 *)&VDAC_VSEL_table[0];
			size = ARRAY_SIZE(VDAC_VSEL_table);
			break;
		case TPS65910_VPLL:
			ptr = (u16 *)&VPLL_VSEL_table[0];
			size = ARRAY_SIZE(VPLL_VSEL_table);
			break;
		default:
			ptr = NULL;
			break;
		}
		
		if (vsel >= size)
			vsel = size - 1;
	}

	if (ptr != NULL) {
		DBG("%s: regulator id=%d index=%d, get volage=%d(mV)\n", __FUNCTION__, ldo_id, index, ptr[vsel]);
		return ptr[vsel] * 1000;
	}
	/* For warning */
	DBG("%s: regulator id=%d index=%d, get volage error\n", __FUNCTION__, ldo_id, index);
	return -1;
}

static int
tps65910_ldo_set_voltage(struct regulator_dev *rdev, int min_uV, int max_uV)
{
	struct tps65910reg_info      *info = rdev_get_drvdata(rdev);
	int vsel;
	u8 	offset;
	u8 	val;
	u8 	index;

	if (info == NULL)
		return 0;
	
	DBG("%s: regulator(%s) id=%d, set volage=(min_uV:%d, max_uV:%d)\n", __FUNCTION__, info->desc.name, info->id, min_uV, max_uV);
	if (rdev->constraints) {
		if (min_uV < rdev->constraints->min_uV || min_uV > rdev->constraints->max_uV)
			return -EINVAL;
		if (max_uV < rdev->constraints->min_uV || max_uV > rdev->constraints->max_uV)
			return -EINVAL;
	}

	for (vsel = 0; vsel < info->table_len; vsel++) {

		int mV = info->table[vsel];
		int uV;

		uV = mV * 1000;

		/* Break at the first in-range value */
		if (min_uV <= uV && uV <= max_uV) {
			offset = tps65910reg_find_offset(info->id);
			val = tps65910reg_read(info, TPS65910_I2C_ID0, offset);
			if (val < 0) {
				printk(KERN_ERR"Unable to read TPS65910 Reg at offset = 0x%x\n",
						offset);
				return -EIO;
			}

			index = get_voltage_index(info->id, uV);
			/* For VDD1 and VDD2 */
			if (info->id == TPS65910_VDD1 || info->id == TPS65910_VDD2) {
				val = index;
				return tps65910reg_write(info, TPS65910_I2C_ID0, offset, val);
			}
			
			val &= 0xF3;
			val = (index << 2);
			val |= 0x01;
			if (index < 0) {
				printk(KERN_ERR "Invaild voltage for LDO \n");
				return -EINVAL;
			}
			
			return tps65910reg_write(info, TPS65910_I2C_ID0, offset, val);
		}
	}
	
	return -EINVAL;
}

static int tps65910_ldo_get_voltage(struct regulator_dev *rdev)
{
	struct tps65910reg_info      *info = rdev_get_drvdata(rdev);
	int vsel;
	u8 	offset;

	offset = tps65910reg_find_offset(info->id);
	vsel = tps65910reg_read(info, TPS65910_I2C_ID0, offset);
	if (vsel < 0) {
		printk(KERN_ERR"Unable to read TPS65910 Reg at offset = \
			0x%x\n", offset);
		return -EIO;
	}
	
	/* Get the index of voltage value from Reg and map to table */
	return get_index_voltage(info->id, vsel);
}


static struct regulator_ops tps65910_ldo_ops = {
	.list_voltage   = tps65910_ldo_list_voltage,
	.set_voltage    = tps65910_ldo_set_voltage,
	.get_voltage    = tps65910_ldo_get_voltage,
	.enable         = tps65910reg_enable,
	.disable        = tps65910reg_disable,
	.is_enabled     = tps65910reg_is_enabled,
	.set_mode       = tps65910reg_set_mode,
	.get_status     = tps65910reg_get_status,
};

static
int tps65910_fixed_list_voltage(struct regulator_dev *rdev, unsigned index)
{
	struct tps65910reg_info      *info = rdev_get_drvdata(rdev);

	return info->min_mV * 1000;
}

static int tps65910_fixed_get_voltage(struct regulator_dev *rdev)
{
	struct tps65910reg_info      *info = rdev_get_drvdata(rdev);

	return info->min_mV * 1000;
}

static struct regulator_ops tps65910_fixed_ops = {
	.list_voltage   = tps65910_fixed_list_voltage,
	.get_voltage    = tps65910_fixed_get_voltage,
	.enable         = tps65910reg_enable,
	.disable        = tps65910reg_disable,
	.is_enabled     = tps65910reg_is_enabled,
	.set_mode       = tps65910reg_set_mode,
	.get_status     = tps65910reg_get_status,
};

#define TPS65910_ADJUSTABLE_LDO(label, num, min_mVolts, max_mVolts,\
				 turnon_delay) { \
	.id = num, \
	.table_len = ARRAY_SIZE(label##_VSEL_table), \
	.table = label##_VSEL_table, \
	.min_mV = min_mVolts, \
	.max_mV = max_mVolts, \
	.delay = turnon_delay, \
	.desc = { \
		.name = #label, \
		.id = TPS65910_##label, \
		.n_voltages = ARRAY_SIZE(label##_VSEL_table), \
		.ops = &tps65910_ldo_ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
	}, \
}

#define TPS65910_FIXED_LDO(label, num, mVolts, turnon_delay) { \
	.id = num, \
	.min_mV = mVolts, \
	.delay = turnon_delay, \
	.desc = { \
		.name = #label, \
		.id = TPS65910_##label, \
		.n_voltages = 1, \
		.ops = &tps65910_fixed_ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
	}, \
}

/*
 * We list regulators here if systems need some level of
 * software control over them after boot.
 */
static struct tps65910reg_info tps65910_regs[] = {
		
	TPS65910_ADJUSTABLE_LDO(VDD1, TPS65910_VDD1, 950, 1400, 4),
	TPS65910_FIXED_LDO(VDD2, TPS65910_VDD2, 1200, 4),
	TPS65910_FIXED_LDO(VIO, TPS65910_VIO, 3300, 1),
	
	TPS65910_FIXED_LDO(VDD3, TPS65910_VDD3, 5000, 0),
	
	TPS65910_FIXED_LDO(VDIG1, TPS65910_VDIG1, 2700, 7),
	TPS65910_FIXED_LDO(VDIG2, TPS65910_VDIG2, 1200, 2),
	TPS65910_FIXED_LDO(VAUX33, TPS65910_VAUX33, 3300, 2),
	TPS65910_FIXED_LDO(VMMC, TPS65910_VMMC, 3000, 7),
	TPS65910_FIXED_LDO(VAUX1, TPS65910_VAUX1, 2800, 7),
	TPS65910_FIXED_LDO(VAUX2, TPS65910_VAUX1, 2900, 6),
	TPS65910_FIXED_LDO(VDAC, TPS65910_VDAC, 1800, 6),
	TPS65910_FIXED_LDO(VPLL, TPS65910_VPLL, 2500, 2),
};

static int tps65910_regulator_probe(struct platform_device *pdev)
{
	int                             i;
	struct tps65910reg_info         *info;
	struct regulator_init_data      *initdata;
	struct regulation_constraints   *c;
	struct regulator_dev            *rdev;

	for (i = 0, info = NULL; i < ARRAY_SIZE(tps65910_regs); i++) {
		if (tps65910_regs[i].desc.id != pdev->id)
			continue;
		info = tps65910_regs + i;
		break;
	}
	if (!info)
		return -ENODEV;

	DBG("%s: reguloter(%s) id=%d\n", __FUNCTION__, info->desc.name, info->id);

	initdata = pdev->dev.platform_data;
	if (!initdata)
		return -EINVAL;

	/* Constrain board-specific capabilities according to what
	 * this driver and the chip itself can actually do.
	 */
	c = &initdata->constraints;
	c->valid_modes_mask &= REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY;
	c->valid_ops_mask &= REGULATOR_CHANGE_VOLTAGE
		| REGULATOR_CHANGE_MODE
		| REGULATOR_CHANGE_STATUS;

	switch (pdev->id) {
	case TPS65910_REG_VIO:
	case TPS65910_REG_VDD1:
	case TPS65910_REG_VDD2:
	case TPS65910_REG_VDIG2:
	case TPS65910_REG_VPLL:
		c->always_on = true;
		break;
	default:
		break;
	}

	rdev = regulator_register(&info->desc, &pdev->dev, initdata, info);

	if (IS_ERR(rdev)) {
		dev_err(&pdev->dev, "can't register %s, %ld\n",
				info->desc.name, PTR_ERR(rdev));
		return PTR_ERR(rdev);
	}
	//	cwz add for init status, for regulator disable
	rdev->use_count = 1;
	
	platform_set_drvdata(pdev, rdev);

	DBG("%s: reguloter register OK.\n", __FUNCTION__);
	return 0;
}

static int __devexit tps65910_regulator_remove(struct platform_device *pdev)
{
	regulator_unregister(platform_get_drvdata(pdev));
	return 0;
}


MODULE_ALIAS("platform:tps65910_regulator");
static struct platform_driver tps65910_regulator_driver = {
	.probe          = tps65910_regulator_probe,
	.remove         = tps65910_regulator_remove,
	.driver.name    = "tps65910_regulator",
	.driver.owner   = THIS_MODULE,
};

static int __init tps65910_regulator_init(void)
{
	return platform_driver_register(&tps65910_regulator_driver);
}
rootfs_initcall(tps65910_regulator_init);

static void __exit tps65910_regulator_exit(void)
{
	platform_driver_unregister(&tps65910_regulator_driver);
}
module_exit(tps65910_regulator_exit)

MODULE_AUTHOR("cwz <cwz@rock-chips.com>");
MODULE_DESCRIPTION("TPS65910 voltage regulator driver");
MODULE_LICENSE("GPL");
