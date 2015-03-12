/*
 * Battery driver for Marvell 88PM860x PMIC
 *
 * Copyright (c) 2012 Marvell International Ltd.
 * Author:	Jett Zhou <jtzhou@marvell.com>
 *		Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/power_supply.h>
#include <linux/mfd/88pm860x.h>
#include <linux/delay.h>

/* bit definitions of Status Query Interface 2 */
#define STATUS2_CHG			(1 << 2)
#define STATUS2_BAT			(1 << 3)
#define STATUS2_VBUS			(1 << 4)

/* bit definitions of Measurement Enable 1 Register */
#define MEAS1_TINT			(1 << 3)
#define MEAS1_GP1			(1 << 5)

/* bit definitions of Measurement Enable 3 Register */
#define MEAS3_IBAT			(1 << 0)
#define MEAS3_BAT_DET			(1 << 1)
#define MEAS3_CC			(1 << 2)

/* bit definitions of Measurement Off Time Register */
#define MEAS_OFF_SLEEP_EN		(1 << 1)

/* bit definitions of GPADC Bias Current 2 Register */
#define GPBIAS2_GPADC1_SET		(2 << 4)
/* GPADC1 Bias Current value in uA unit */
#define GPBIAS2_GPADC1_UA		((GPBIAS2_GPADC1_SET >> 4) * 5 + 1)

/* bit definitions of GPADC Misc 1 Register */
#define GPMISC1_GPADC_EN		(1 << 0)

/* bit definitions of Charger Control 6 Register */
#define CC6_BAT_DET_GPADC1		1

/* bit definitions of Coulomb Counter Reading Register */
#define CCNT_AVG_SEL			(4 << 3)

/* bit definitions of RTC miscellaneous Register1 */
#define RTC_SOC_5LSB		(0x1F << 3)

/* bit definitions of RTC Register1 */
#define RTC_SOC_3MSB		(0x7)

/* bit definitions of Power up Log register */
#define BAT_WU_LOG			(1<<6)

/* coulomb counter index */
#define CCNT_POS1			0
#define CCNT_POS2			1
#define CCNT_NEG1			2
#define CCNT_NEG2			3
#define CCNT_SPOS			4
#define CCNT_SNEG			5

/* OCV -- Open Circuit Voltage */
#define OCV_MODE_ACTIVE			0
#define OCV_MODE_SLEEP			1

/* Vbat range of CC for measuring Rbat */
#define LOW_BAT_THRESHOLD		3600
#define VBATT_RESISTOR_MIN		3800
#define VBATT_RESISTOR_MAX		4100

/* TBAT for batt, TINT for chip itself */
#define PM860X_TEMP_TINT		(0)
#define PM860X_TEMP_TBAT		(1)

/*
 * Battery temperature based on NTC resistor, defined
 * corresponding resistor value  -- Ohm / C degeree.
 */
#define TBAT_NEG_25D		127773	/* -25 */
#define TBAT_NEG_10D		54564	/* -10 */
#define TBAT_0D			32330	/* 0 */
#define TBAT_10D		19785	/* 10 */
#define TBAT_20D		12468	/* 20 */
#define TBAT_30D		8072	/* 30 */
#define TBAT_40D		5356	/* 40 */

struct pm860x_battery_info {
	struct pm860x_chip *chip;
	struct i2c_client *i2c;
	struct device *dev;

	struct power_supply battery;
	struct mutex lock;
	int status;
	int irq_cc;
	int irq_batt;
	int max_capacity;
	int resistor;		/* Battery Internal Resistor */
	int last_capacity;
	int start_soc;
	unsigned present:1;
	unsigned temp_type:1;	/* TINT or TBAT */
};

struct ccnt {
	unsigned long long int pos;
	unsigned long long int neg;
	unsigned int spos;
	unsigned int sneg;

	int total_chg;		/* mAh(3.6C) */
	int total_dischg;	/* mAh(3.6C) */
};

/*
 * State of Charge.
 * The first number is mAh(=3.6C), and the second number is percent point.
 */
static int array_soc[][2] = {
	{4170, 100}, {4154, 99}, {4136, 98}, {4122, 97}, {4107, 96},
	{4102, 95}, {4088, 94}, {4081, 93}, {4070, 92}, {4060, 91},
	{4053, 90}, {4044, 89}, {4035, 88}, {4028, 87}, {4019, 86},
	{4013, 85}, {4006, 84}, {3995, 83}, {3987, 82}, {3982, 81},
	{3976, 80}, {3968, 79}, {3962, 78}, {3954, 77}, {3946, 76},
	{3941, 75}, {3934, 74}, {3929, 73}, {3922, 72}, {3916, 71},
	{3910, 70}, {3904, 69}, {3898, 68}, {3892, 67}, {3887, 66},
	{3880, 65}, {3874, 64}, {3868, 63}, {3862, 62}, {3854, 61},
	{3849, 60}, {3843, 59}, {3840, 58}, {3833, 57}, {3829, 56},
	{3824, 55}, {3818, 54}, {3815, 53}, {3810, 52}, {3808, 51},
	{3804, 50}, {3801, 49}, {3798, 48}, {3796, 47}, {3792, 46},
	{3789, 45}, {3785, 44}, {3784, 43}, {3782, 42}, {3780, 41},
	{3777, 40}, {3776, 39}, {3774, 38}, {3772, 37}, {3771, 36},
	{3769, 35}, {3768, 34}, {3764, 33}, {3763, 32}, {3760, 31},
	{3760, 30}, {3754, 29}, {3750, 28}, {3749, 27}, {3744, 26},
	{3740, 25}, {3734, 24}, {3732, 23}, {3728, 22}, {3726, 21},
	{3720, 20}, {3716, 19}, {3709, 18}, {3703, 17}, {3698, 16},
	{3692, 15}, {3683, 14}, {3675, 13}, {3670, 12}, {3665, 11},
	{3661, 10}, {3649, 9}, {3637, 8}, {3622, 7}, {3609, 6},
	{3580, 5}, {3558, 4}, {3540, 3}, {3510, 2}, {3429, 1},
};

static struct ccnt ccnt_data;

/*
 * register 1 bit[7:0] -- bit[11:4] of measured value of voltage
 * register 0 bit[3:0] -- bit[3:0] of measured value of voltage
 */
static int measure_12bit_voltage(struct pm860x_battery_info *info,
				 int offset, int *data)
{
	unsigned char buf[2];
	int ret;

	ret = pm860x_bulk_read(info->i2c, offset, 2, buf);
	if (ret < 0)
		return ret;

	*data = ((buf[0] & 0xff) << 4) | (buf[1] & 0x0f);
	/* V_MEAS(mV) = data * 1.8 * 1000 / (2^12) */
	*data = ((*data & 0xfff) * 9 * 25) >> 9;
	return 0;
}

static int measure_vbatt(struct pm860x_battery_info *info, int state,
			 int *data)
{
	unsigned char buf[5];
	int ret;

	switch (state) {
	case OCV_MODE_ACTIVE:
		ret = measure_12bit_voltage(info, PM8607_VBAT_MEAS1, data);
		if (ret)
			return ret;
		/* V_BATT_MEAS(mV) = value * 3 * 1.8 * 1000 / (2^12) */
		*data *= 3;
		break;
	case OCV_MODE_SLEEP:
		/*
		 * voltage value of VBATT in sleep mode is saved in different
		 * registers.
		 * bit[11:10] -- bit[7:6] of LDO9(0x18)
		 * bit[9:8] -- bit[7:6] of LDO8(0x17)
		 * bit[7:6] -- bit[7:6] of LDO7(0x16)
		 * bit[5:4] -- bit[7:6] of LDO6(0x15)
		 * bit[3:0] -- bit[7:4] of LDO5(0x14)
		 */
		ret = pm860x_bulk_read(info->i2c, PM8607_LDO5, 5, buf);
		if (ret < 0)
			return ret;
		ret = ((buf[4] >> 6) << 10) | ((buf[3] >> 6) << 8)
		    | ((buf[2] >> 6) << 6) | ((buf[1] >> 6) << 4)
		    | (buf[0] >> 4);
		/* V_BATT_MEAS(mV) = data * 3 * 1.8 * 1000 / (2^12) */
		*data = ((*data & 0xff) * 27 * 25) >> 9;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * Return value is signed data.
 * Negative value means discharging, and positive value means charging.
 */
static int measure_current(struct pm860x_battery_info *info, int *data)
{
	unsigned char buf[2];
	short s;
	int ret;

	ret = pm860x_bulk_read(info->i2c, PM8607_IBAT_MEAS1, 2, buf);
	if (ret < 0)
		return ret;

	s = ((buf[0] & 0xff) << 8) | (buf[1] & 0xff);
	/* current(mA) = value * 0.125 */
	*data = s >> 3;
	return 0;
}

static int set_charger_current(struct pm860x_battery_info *info, int data,
			       int *old)
{
	int ret;

	if (data < 50 || data > 1600 || !old)
		return -EINVAL;

	data = ((data - 50) / 50) & 0x1f;
	*old = pm860x_reg_read(info->i2c, PM8607_CHG_CTRL2);
	*old = (*old & 0x1f) * 50 + 50;
	ret = pm860x_set_bits(info->i2c, PM8607_CHG_CTRL2, 0x1f, data);
	if (ret < 0)
		return ret;
	return 0;
}

static int read_ccnt(struct pm860x_battery_info *info, int offset,
		     int *ccnt)
{
	unsigned char buf[2];
	int ret;

	ret = pm860x_set_bits(info->i2c, PM8607_CCNT, 7, offset & 7);
	if (ret < 0)
		goto out;
	ret = pm860x_bulk_read(info->i2c, PM8607_CCNT_MEAS1, 2, buf);
	if (ret < 0)
		goto out;
	*ccnt = ((buf[0] & 0xff) << 8) | (buf[1] & 0xff);
	return 0;
out:
	return ret;
}

static int calc_ccnt(struct pm860x_battery_info *info, struct ccnt *ccnt)
{
	unsigned int sum;
	int ret;
	int data;

	ret = read_ccnt(info, CCNT_POS1, &data);
	if (ret)
		goto out;
	sum = data & 0xffff;
	ret = read_ccnt(info, CCNT_POS2, &data);
	if (ret)
		goto out;
	sum |= (data & 0xffff) << 16;
	ccnt->pos += sum;

	ret = read_ccnt(info, CCNT_NEG1, &data);
	if (ret)
		goto out;
	sum = data & 0xffff;
	ret = read_ccnt(info, CCNT_NEG2, &data);
	if (ret)
		goto out;
	sum |= (data & 0xffff) << 16;
	sum = ~sum + 1;		/* since it's negative */
	ccnt->neg += sum;

	ret = read_ccnt(info, CCNT_SPOS, &data);
	if (ret)
		goto out;
	ccnt->spos += data;
	ret = read_ccnt(info, CCNT_SNEG, &data);
	if (ret)
		goto out;

	/*
	 * charge(mAh)  = count * 1.6984 * 1e(-8)
	 *              = count * 16984 * 1.024 * 1.024 * 1.024 / (2 ^ 40)
	 *              = count * 18236 / (2 ^ 40)
	 */
	ccnt->total_chg = (int) ((ccnt->pos * 18236) >> 40);
	ccnt->total_dischg = (int) ((ccnt->neg * 18236) >> 40);
	return 0;
out:
	return ret;
}

static int clear_ccnt(struct pm860x_battery_info *info, struct ccnt *ccnt)
{
	int data;

	memset(ccnt, 0, sizeof(*ccnt));
	/* read to clear ccnt */
	read_ccnt(info, CCNT_POS1, &data);
	read_ccnt(info, CCNT_POS2, &data);
	read_ccnt(info, CCNT_NEG1, &data);
	read_ccnt(info, CCNT_NEG2, &data);
	read_ccnt(info, CCNT_SPOS, &data);
	read_ccnt(info, CCNT_SNEG, &data);
	return 0;
}

/* Calculate Open Circuit Voltage */
static int calc_ocv(struct pm860x_battery_info *info, int *ocv)
{
	int ret;
	int i;
	int data;
	int vbatt_avg;
	int vbatt_sum;
	int ibatt_avg;
	int ibatt_sum;

	if (!ocv)
		return -EINVAL;

	for (i = 0, ibatt_sum = 0, vbatt_sum = 0; i < 10; i++) {
		ret = measure_vbatt(info, OCV_MODE_ACTIVE, &data);
		if (ret)
			goto out;
		vbatt_sum += data;
		ret = measure_current(info, &data);
		if (ret)
			goto out;
		ibatt_sum += data;
	}
	vbatt_avg = vbatt_sum / 10;
	ibatt_avg = ibatt_sum / 10;

	mutex_lock(&info->lock);
	if (info->present)
		*ocv = vbatt_avg - ibatt_avg * info->resistor / 1000;
	else
		*ocv = vbatt_avg;
	mutex_unlock(&info->lock);
	dev_dbg(info->dev, "VBAT average:%d, OCV:%d\n", vbatt_avg, *ocv);
	return 0;
out:
	return ret;
}

/* Calculate State of Charge (percent points) */
static int calc_soc(struct pm860x_battery_info *info, int state, int *soc)
{
	int i;
	int ocv;
	int count;
	int ret = -EINVAL;

	if (!soc)
		return -EINVAL;

	switch (state) {
	case OCV_MODE_ACTIVE:
		ret = calc_ocv(info, &ocv);
		break;
	case OCV_MODE_SLEEP:
		ret = measure_vbatt(info, OCV_MODE_SLEEP, &ocv);
		break;
	}
	if (ret)
		return ret;

	count = ARRAY_SIZE(array_soc);
	if (ocv < array_soc[count - 1][0]) {
		*soc = 0;
		return 0;
	}

	for (i = 0; i < count; i++) {
		if (ocv >= array_soc[i][0]) {
			*soc = array_soc[i][1];
			break;
		}
	}
	return 0;
}

static irqreturn_t pm860x_coulomb_handler(int irq, void *data)
{
	struct pm860x_battery_info *info = data;

	calc_ccnt(info, &ccnt_data);
	return IRQ_HANDLED;
}

static irqreturn_t pm860x_batt_handler(int irq, void *data)
{
	struct pm860x_battery_info *info = data;
	int ret;

	mutex_lock(&info->lock);
	ret = pm860x_reg_read(info->i2c, PM8607_STATUS_2);
	if (ret & STATUS2_BAT) {
		info->present = 1;
		info->temp_type = PM860X_TEMP_TBAT;
	} else {
		info->present = 0;
		info->temp_type = PM860X_TEMP_TINT;
	}
	mutex_unlock(&info->lock);
	/* clear ccnt since battery is attached or dettached */
	clear_ccnt(info, &ccnt_data);
	return IRQ_HANDLED;
}

static void pm860x_init_battery(struct pm860x_battery_info *info)
{
	unsigned char buf[2];
	int ret;
	int data;
	int bat_remove;
	int soc;

	/* measure enable on GPADC1 */
	data = MEAS1_GP1;
	if (info->temp_type == PM860X_TEMP_TINT)
		data |= MEAS1_TINT;
	ret = pm860x_set_bits(info->i2c, PM8607_MEAS_EN1, data, data);
	if (ret)
		goto out;

	/* measure enable on IBAT, BAT_DET, CC. IBAT is depend on CC. */
	data = MEAS3_IBAT | MEAS3_BAT_DET | MEAS3_CC;
	ret = pm860x_set_bits(info->i2c, PM8607_MEAS_EN3, data, data);
	if (ret)
		goto out;

	/* measure disable CC in sleep time  */
	ret = pm860x_reg_write(info->i2c, PM8607_MEAS_OFF_TIME1, 0x82);
	if (ret)
		goto out;
	ret = pm860x_reg_write(info->i2c, PM8607_MEAS_OFF_TIME2, 0x6c);
	if (ret)
		goto out;

	/* enable GPADC */
	ret = pm860x_set_bits(info->i2c, PM8607_GPADC_MISC1,
			    GPMISC1_GPADC_EN, GPMISC1_GPADC_EN);
	if (ret < 0)
		goto out;

	/* detect battery via GPADC1 */
	ret = pm860x_set_bits(info->i2c, PM8607_CHG_CTRL6,
			    CC6_BAT_DET_GPADC1, CC6_BAT_DET_GPADC1);
	if (ret < 0)
		goto out;

	ret = pm860x_set_bits(info->i2c, PM8607_CCNT, 7 << 3,
			      CCNT_AVG_SEL);
	if (ret < 0)
		goto out;

	/* set GPADC1 bias */
	ret = pm860x_set_bits(info->i2c, PM8607_GP_BIAS2, 0xF << 4,
			      GPBIAS2_GPADC1_SET);
	if (ret < 0)
		goto out;

	/* check whether battery present) */
	mutex_lock(&info->lock);
	ret = pm860x_reg_read(info->i2c, PM8607_STATUS_2);
	if (ret < 0) {
		mutex_unlock(&info->lock);
		goto out;
	}
	if (ret & STATUS2_BAT) {
		info->present = 1;
		info->temp_type = PM860X_TEMP_TBAT;
	} else {
		info->present = 0;
		info->temp_type = PM860X_TEMP_TINT;
	}
	mutex_unlock(&info->lock);

	calc_soc(info, OCV_MODE_ACTIVE, &soc);

	data = pm860x_reg_read(info->i2c, PM8607_POWER_UP_LOG);
	bat_remove = data & BAT_WU_LOG;

	dev_dbg(info->dev, "battery wake up? %s\n",
		bat_remove != 0 ? "yes" : "no");

	/* restore SOC from RTC domain register */
	if (bat_remove == 0) {
		buf[0] = pm860x_reg_read(info->i2c, PM8607_RTC_MISC2);
		buf[1] = pm860x_reg_read(info->i2c, PM8607_RTC1);
		data = ((buf[1] & 0x3) << 5) | ((buf[0] >> 3) & 0x1F);
		if (data > soc + 15)
			info->start_soc = soc;
		else if (data < soc - 15)
			info->start_soc = soc;
		else
			info->start_soc = data;
		dev_dbg(info->dev, "soc_rtc %d, soc_ocv :%d\n", data, soc);
	} else {
		pm860x_set_bits(info->i2c, PM8607_POWER_UP_LOG,
				BAT_WU_LOG, BAT_WU_LOG);
		info->start_soc = soc;
	}
	info->last_capacity = info->start_soc;
	dev_dbg(info->dev, "init soc : %d\n", info->last_capacity);
out:
	return;
}

static void set_temp_threshold(struct pm860x_battery_info *info,
			       int min, int max)
{
	int data;

	/* (tmp << 8) / 1800 */
	if (min <= 0)
		data = 0;
	else
		data = (min << 8) / 1800;
	pm860x_reg_write(info->i2c, PM8607_GPADC1_HIGHTH, data);
	dev_dbg(info->dev, "TEMP_HIGHTH : min: %d, 0x%x\n", min, data);

	if (max <= 0)
		data = 0xff;
	else
		data = (max << 8) / 1800;
	pm860x_reg_write(info->i2c, PM8607_GPADC1_LOWTH, data);
	dev_dbg(info->dev, "TEMP_LOWTH:max : %d, 0x%x\n", max, data);
}

static int measure_temp(struct pm860x_battery_info *info, int *data)
{
	int ret;
	int temp;
	int min;
	int max;

	if (info->temp_type == PM860X_TEMP_TINT) {
		ret = measure_12bit_voltage(info, PM8607_TINT_MEAS1, data);
		if (ret)
			return ret;
		*data = (*data - 884) * 1000 / 3611;
	} else {
		ret = measure_12bit_voltage(info, PM8607_GPADC1_MEAS1, data);
		if (ret)
			return ret;
		/* meausered Vtbat(mV) / Ibias_current(11uA)*/
		*data = (*data * 1000) / GPBIAS2_GPADC1_UA;

		if (*data > TBAT_NEG_25D) {
			temp = -30;	/* over cold , suppose -30 roughly */
			max = TBAT_NEG_10D * GPBIAS2_GPADC1_UA / 1000;
			set_temp_threshold(info, 0, max);
		} else if (*data > TBAT_NEG_10D) {
			temp = -15;	/* -15 degree, code */
			max = TBAT_NEG_10D * GPBIAS2_GPADC1_UA / 1000;
			set_temp_threshold(info, 0, max);
		} else if (*data > TBAT_0D) {
			temp = -5;	/* -5 degree */
			min = TBAT_NEG_10D * GPBIAS2_GPADC1_UA / 1000;
			max = TBAT_40D * GPBIAS2_GPADC1_UA / 1000;
			set_temp_threshold(info, min, max);
		} else if (*data > TBAT_10D) {
			temp = 5;	/* in range of (0, 10) */
			min = TBAT_NEG_10D * GPBIAS2_GPADC1_UA / 1000;
			max = TBAT_40D * GPBIAS2_GPADC1_UA / 1000;
			set_temp_threshold(info, min, max);
		} else if (*data > TBAT_20D) {
			temp = 15;	/* in range of (10, 20) */
			min = TBAT_NEG_10D * GPBIAS2_GPADC1_UA / 1000;
			max = TBAT_40D * GPBIAS2_GPADC1_UA / 1000;
			set_temp_threshold(info, min, max);
		} else if (*data > TBAT_30D) {
			temp = 25;	/* in range of (20, 30) */
			min = TBAT_NEG_10D * GPBIAS2_GPADC1_UA / 1000;
			max = TBAT_40D * GPBIAS2_GPADC1_UA / 1000;
			set_temp_threshold(info, min, max);
		} else if (*data > TBAT_40D) {
			temp = 35;	/* in range of (30, 40) */
			min = TBAT_NEG_10D * GPBIAS2_GPADC1_UA / 1000;
			max = TBAT_40D * GPBIAS2_GPADC1_UA / 1000;
			set_temp_threshold(info, min, max);
		} else {
			min = TBAT_40D * GPBIAS2_GPADC1_UA / 1000;
			set_temp_threshold(info, min, 0);
			temp = 45;	/* over heat ,suppose 45 roughly */
		}

		dev_dbg(info->dev, "temp_C:%d C,temp_mv:%d mv\n", temp, *data);
		*data = temp;
	}
	return 0;
}

static int calc_resistor(struct pm860x_battery_info *info)
{
	int vbatt_sum1;
	int vbatt_sum2;
	int chg_current;
	int ibatt_sum1;
	int ibatt_sum2;
	int data;
	int ret;
	int i;

	ret = measure_current(info, &data);
	/* make sure that charging is launched by data > 0 */
	if (ret || data < 0)
		goto out;

	ret = measure_vbatt(info, OCV_MODE_ACTIVE, &data);
	if (ret)
		goto out;
	/* calculate resistor only in CC charge mode */
	if (data < VBATT_RESISTOR_MIN || data > VBATT_RESISTOR_MAX)
		goto out;

	/* current is saved */
	if (set_charger_current(info, 500, &chg_current))
		goto out;

	/*
	 * set charge current as 500mA, wait about 500ms till charging
	 * process is launched and stable with the newer charging current.
	 */
	msleep(500);

	for (i = 0, vbatt_sum1 = 0, ibatt_sum1 = 0; i < 10; i++) {
		ret = measure_vbatt(info, OCV_MODE_ACTIVE, &data);
		if (ret)
			goto out_meas;
		vbatt_sum1 += data;
		ret = measure_current(info, &data);
		if (ret)
			goto out_meas;

		if (data < 0)
			ibatt_sum1 = ibatt_sum1 - data;	/* discharging */
		else
			ibatt_sum1 = ibatt_sum1 + data;	/* charging */
	}

	if (set_charger_current(info, 100, &ret))
		goto out_meas;
	/*
	 * set charge current as 100mA, wait about 500ms till charging
	 * process is launched and stable with the newer charging current.
	 */
	msleep(500);

	for (i = 0, vbatt_sum2 = 0, ibatt_sum2 = 0; i < 10; i++) {
		ret = measure_vbatt(info, OCV_MODE_ACTIVE, &data);
		if (ret)
			goto out_meas;
		vbatt_sum2 += data;
		ret = measure_current(info, &data);
		if (ret)
			goto out_meas;

		if (data < 0)
			ibatt_sum2 = ibatt_sum2 - data;	/* discharging */
		else
			ibatt_sum2 = ibatt_sum2 + data;	/* charging */
	}

	/* restore current setting */
	if (set_charger_current(info, chg_current, &ret))
		goto out_meas;

	if ((vbatt_sum1 > vbatt_sum2) && (ibatt_sum1 > ibatt_sum2) &&
			(ibatt_sum2 > 0)) {
		/* calculate resistor in discharging case */
		data = 1000 * (vbatt_sum1 - vbatt_sum2)
		    / (ibatt_sum1 - ibatt_sum2);
		if ((data - info->resistor > 0) &&
				(data - info->resistor < info->resistor))
			info->resistor = data;
		if ((info->resistor - data > 0) &&
				(info->resistor - data < data))
			info->resistor = data;
	}
	return 0;

out_meas:
	set_charger_current(info, chg_current, &ret);
out:
	return -EINVAL;
}

static int calc_capacity(struct pm860x_battery_info *info, int *cap)
{
	int ret;
	int data;
	int ibat;
	int cap_ocv = 0;
	int cap_cc = 0;

	ret = calc_ccnt(info, &ccnt_data);
	if (ret)
		goto out;
soc:
	data = info->max_capacity * info->start_soc / 100;
	if (ccnt_data.total_dischg - ccnt_data.total_chg <= data) {
		cap_cc =
		    data + ccnt_data.total_chg - ccnt_data.total_dischg;
	} else {
		clear_ccnt(info, &ccnt_data);
		calc_soc(info, OCV_MODE_ACTIVE, &info->start_soc);
		dev_dbg(info->dev, "restart soc = %d !\n",
			info->start_soc);
		goto soc;
	}

	cap_cc = cap_cc * 100 / info->max_capacity;
	if (cap_cc < 0)
		cap_cc = 0;
	else if (cap_cc > 100)
		cap_cc = 100;

	dev_dbg(info->dev, "%s, last cap : %d", __func__,
		info->last_capacity);

	ret = measure_current(info, &ibat);
	if (ret)
		goto out;
	/* Calculate the capacity when discharging(ibat < 0) */
	if (ibat < 0) {
		ret = calc_soc(info, OCV_MODE_ACTIVE, &cap_ocv);
		if (ret)
			cap_ocv = info->last_capacity;
		ret = measure_vbatt(info, OCV_MODE_ACTIVE, &data);
		if (ret)
			goto out;
		if (data <= LOW_BAT_THRESHOLD) {
			/* choose the lower capacity value to report
			 * between vbat and CC when vbat < 3.6v;
			 * than 3.6v;
			 */
			*cap = min(cap_ocv, cap_cc);
		} else {
			/* when detect vbat > 3.6v, but cap_cc < 15,and
			 * cap_ocv is 10% larger than cap_cc, we can think
			 * CC have some accumulation error, switch to OCV
			 * to estimate capacity;
			 * */
			if (cap_cc < 15 && cap_ocv - cap_cc > 10)
				*cap = cap_ocv;
			else
				*cap = cap_cc;
		}
		/* when discharging, make sure current capacity
		 * is lower than last*/
		if (*cap > info->last_capacity)
			*cap = info->last_capacity;
	} else {
		*cap = cap_cc;
	}
	info->last_capacity = *cap;

	dev_dbg(info->dev, "%s, cap_ocv:%d cap_cc:%d, cap:%d\n",
		(ibat < 0) ? "discharging" : "charging",
		 cap_ocv, cap_cc, *cap);
	/*
	 * store the current capacity to RTC domain register,
	 * after next power up , it will be restored.
	 */
	pm860x_set_bits(info->i2c, PM8607_RTC_MISC2, RTC_SOC_5LSB,
			(*cap & 0x1F) << 3);
	pm860x_set_bits(info->i2c, PM8607_RTC1, RTC_SOC_3MSB,
			((*cap >> 5) & 0x3));
	return 0;
out:
	return ret;
}

static void pm860x_external_power_changed(struct power_supply *psy)
{
	struct pm860x_battery_info *info;

	info = container_of(psy, struct pm860x_battery_info, battery);
	calc_resistor(info);
}

static int pm860x_batt_get_prop(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct pm860x_battery_info *info = dev_get_drvdata(psy->dev->parent);
	int data;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = info->present;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = calc_capacity(info, &data);
		if (ret)
			return ret;
		if (data < 0)
			data = 0;
		else if (data > 100)
			data = 100;
		/* return 100 if battery is not attached */
		if (!info->present)
			data = 100;
		val->intval = data;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		/* return real vbatt Voltage */
		ret = measure_vbatt(info, OCV_MODE_ACTIVE, &data);
		if (ret)
			return ret;
		val->intval = data * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		/* return Open Circuit Voltage (not measured voltage) */
		ret = calc_ocv(info, &data);
		if (ret)
			return ret;
		val->intval = data * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = measure_current(info, &data);
		if (ret)
			return ret;
		val->intval = data;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		if (info->present) {
			ret = measure_temp(info, &data);
			if (ret)
				return ret;
			data *= 10;
		} else {
			/* Fake Temp 25C Without Battery */
			data = 250;
		}
		val->intval = data;
		break;
	default:
		return -ENODEV;
	}
	return 0;
}

static int pm860x_batt_set_prop(struct power_supply *psy,
				       enum power_supply_property psp,
				       const union power_supply_propval *val)
{
	struct pm860x_battery_info *info = dev_get_drvdata(psy->dev->parent);

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		clear_ccnt(info, &ccnt_data);
		info->start_soc = 100;
		dev_dbg(info->dev, "chg done, update soc = %d\n",
			info->start_soc);
		break;
	default:
		return -EPERM;
	}

	return 0;
}


static enum power_supply_property pm860x_batt_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_TEMP,
};

static int pm860x_battery_probe(struct platform_device *pdev)
{
	struct pm860x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct pm860x_battery_info *info;
	struct pm860x_power_pdata *pdata;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->irq_cc = platform_get_irq(pdev, 0);
	if (info->irq_cc <= 0) {
		dev_err(&pdev->dev, "No IRQ resource!\n");
		return -EINVAL;
	}

	info->irq_batt = platform_get_irq(pdev, 1);
	if (info->irq_batt <= 0) {
		dev_err(&pdev->dev, "No IRQ resource!\n");
		return -EINVAL;
	}

	info->chip = chip;
	info->i2c =
	    (chip->id == CHIP_PM8607) ? chip->client : chip->companion;
	info->dev = &pdev->dev;
	info->status = POWER_SUPPLY_STATUS_UNKNOWN;
	pdata = pdev->dev.platform_data;

	mutex_init(&info->lock);
	platform_set_drvdata(pdev, info);

	pm860x_init_battery(info);

	info->battery.name = "battery-monitor";
	info->battery.type = POWER_SUPPLY_TYPE_BATTERY;
	info->battery.properties = pm860x_batt_props;
	info->battery.num_properties = ARRAY_SIZE(pm860x_batt_props);
	info->battery.get_property = pm860x_batt_get_prop;
	info->battery.set_property = pm860x_batt_set_prop;
	info->battery.external_power_changed = pm860x_external_power_changed;

	if (pdata && pdata->max_capacity)
		info->max_capacity = pdata->max_capacity;
	else
		info->max_capacity = 1500;	/* set default capacity */
	if (pdata && pdata->resistor)
		info->resistor = pdata->resistor;
	else
		info->resistor = 300;	/* set default internal resistor */

	ret = power_supply_register(&pdev->dev, &info->battery, NULL);
	if (ret)
		return ret;
	info->battery.dev->parent = &pdev->dev;

	ret = request_threaded_irq(info->irq_cc, NULL,
				pm860x_coulomb_handler, IRQF_ONESHOT,
				"coulomb", info);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to request IRQ: #%d: %d\n",
			info->irq_cc, ret);
		goto out_reg;
	}

	ret = request_threaded_irq(info->irq_batt, NULL, pm860x_batt_handler,
				IRQF_ONESHOT, "battery", info);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to request IRQ: #%d: %d\n",
			info->irq_batt, ret);
		goto out_coulomb;
	}


	return 0;

out_coulomb:
	free_irq(info->irq_cc, info);
out_reg:
	power_supply_unregister(&info->battery);
	return ret;
}

static int pm860x_battery_remove(struct platform_device *pdev)
{
	struct pm860x_battery_info *info = platform_get_drvdata(pdev);

	free_irq(info->irq_batt, info);
	free_irq(info->irq_cc, info);
	power_supply_unregister(&info->battery);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int pm860x_battery_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pm860x_chip *chip = dev_get_drvdata(pdev->dev.parent);

	if (device_may_wakeup(dev))
		chip->wakeup_flag |= 1 << PM8607_IRQ_CC;
	return 0;
}

static int pm860x_battery_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pm860x_chip *chip = dev_get_drvdata(pdev->dev.parent);

	if (device_may_wakeup(dev))
		chip->wakeup_flag &= ~(1 << PM8607_IRQ_CC);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(pm860x_battery_pm_ops,
			pm860x_battery_suspend, pm860x_battery_resume);

static struct platform_driver pm860x_battery_driver = {
	.driver = {
		   .name = "88pm860x-battery",
		   .pm = &pm860x_battery_pm_ops,
	},
	.probe = pm860x_battery_probe,
	.remove = pm860x_battery_remove,
};
module_platform_driver(pm860x_battery_driver);

MODULE_DESCRIPTION("Marvell 88PM860x Battery driver");
MODULE_LICENSE("GPL");
