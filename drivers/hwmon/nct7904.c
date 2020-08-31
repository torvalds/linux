// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * nct7904.c - driver for Nuvoton NCT7904D.
 *
 * Copyright (c) 2015 Kontron
 * Author: Vadim V. Vlasov <vvlasov@dev.rtsoft.ru>
 *
 * Copyright (c) 2019 Advantech
 * Author: Amy.Shih <amy.shih@advantech.com.tw>
 *
 * Copyright (c) 2020 Advantech
 * Author: Yuechao Zhao <yuechao.zhao@advantech.com.cn>
 *
 * Supports the following chips:
 *
 * Chip        #vin  #fan  #pwm  #temp  #dts  chip ID
 * nct7904d     20    12    4     5      8    0xc5
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/hwmon.h>
#include <linux/watchdog.h>

#define VENDOR_ID_REG		0x7A	/* Any bank */
#define NUVOTON_ID		0x50
#define CHIP_ID_REG		0x7B	/* Any bank */
#define NCT7904_ID		0xC5
#define DEVICE_ID_REG		0x7C	/* Any bank */

#define BANK_SEL_REG		0xFF
#define BANK_0			0x00
#define BANK_1			0x01
#define BANK_2			0x02
#define BANK_3			0x03
#define BANK_4			0x04
#define BANK_MAX		0x04

#define FANIN_MAX		12	/* Counted from 1 */
#define VSEN_MAX		21	/* VSEN1..14, 3VDD, VBAT, V3VSB,
					   LTD (not a voltage), VSEN17..19 */
#define FANCTL_MAX		4	/* Counted from 1 */
#define TCPU_MAX		8	/* Counted from 1 */
#define TEMP_MAX		4	/* Counted from 1 */
#define SMI_STS_MAX		10	/* Counted from 1 */

#define VT_ADC_CTRL0_REG	0x20	/* Bank 0 */
#define VT_ADC_CTRL1_REG	0x21	/* Bank 0 */
#define VT_ADC_CTRL2_REG	0x22	/* Bank 0 */
#define FANIN_CTRL0_REG		0x24
#define FANIN_CTRL1_REG		0x25
#define DTS_T_CTRL0_REG		0x26
#define DTS_T_CTRL1_REG		0x27
#define VT_ADC_MD_REG		0x2E

#define VSEN1_HV_LL_REG		0x02	/* Bank 1; 2 regs (HV/LV) per sensor */
#define VSEN1_LV_LL_REG		0x03	/* Bank 1; 2 regs (HV/LV) per sensor */
#define VSEN1_HV_HL_REG		0x00	/* Bank 1; 2 regs (HV/LV) per sensor */
#define VSEN1_LV_HL_REG		0x01	/* Bank 1; 2 regs (HV/LV) per sensor */
#define SMI_STS1_REG		0xC1	/* Bank 0; SMI Status Register */
#define SMI_STS3_REG		0xC3	/* Bank 0; SMI Status Register */
#define SMI_STS5_REG		0xC5	/* Bank 0; SMI Status Register */
#define SMI_STS7_REG		0xC7	/* Bank 0; SMI Status Register */
#define SMI_STS8_REG		0xC8	/* Bank 0; SMI Status Register */

#define VSEN1_HV_REG		0x40	/* Bank 0; 2 regs (HV/LV) per sensor */
#define TEMP_CH1_HV_REG		0x42	/* Bank 0; same as VSEN2_HV */
#define LTD_HV_REG		0x62	/* Bank 0; 2 regs in VSEN range */
#define LTD_HV_HL_REG		0x44	/* Bank 1; 1 reg for LTD */
#define LTD_LV_HL_REG		0x45	/* Bank 1; 1 reg for LTD */
#define LTD_HV_LL_REG		0x46	/* Bank 1; 1 reg for LTD */
#define LTD_LV_LL_REG		0x47	/* Bank 1; 1 reg for LTD */
#define TEMP_CH1_CH_REG		0x05	/* Bank 1; 1 reg for LTD */
#define TEMP_CH1_W_REG		0x06	/* Bank 1; 1 reg for LTD */
#define TEMP_CH1_WH_REG		0x07	/* Bank 1; 1 reg for LTD */
#define TEMP_CH1_C_REG		0x04	/* Bank 1; 1 reg per sensor */
#define DTS_T_CPU1_C_REG	0x90	/* Bank 1; 1 reg per sensor */
#define DTS_T_CPU1_CH_REG	0x91	/* Bank 1; 1 reg per sensor */
#define DTS_T_CPU1_W_REG	0x92	/* Bank 1; 1 reg per sensor */
#define DTS_T_CPU1_WH_REG	0x93	/* Bank 1; 1 reg per sensor */
#define FANIN1_HV_REG		0x80	/* Bank 0; 2 regs (HV/LV) per sensor */
#define FANIN1_HV_HL_REG	0x60	/* Bank 1; 2 regs (HV/LV) per sensor */
#define FANIN1_LV_HL_REG	0x61	/* Bank 1; 2 regs (HV/LV) per sensor */
#define T_CPU1_HV_REG		0xA0	/* Bank 0; 2 regs (HV/LV) per sensor */

#define PRTS_REG		0x03	/* Bank 2 */
#define PFE_REG			0x00	/* Bank 2; PECI Function Enable */
#define TSI_CTRL_REG		0x50	/* Bank 2; TSI Control Register */
#define FANCTL1_FMR_REG		0x00	/* Bank 3; 1 reg per channel */
#define FANCTL1_OUT_REG		0x10	/* Bank 3; 1 reg per channel */

#define WDT_LOCK_REG		0xE0	/* W/O Lock Watchdog Register */
#define WDT_EN_REG		0xE1	/* R/O Watchdog Enable Register */
#define WDT_STS_REG		0xE2	/* R/O Watchdog Status Register */
#define WDT_TIMER_REG		0xE3	/* R/W Watchdog Timer Register */
#define WDT_SOFT_EN		0x55	/* Enable soft watchdog timer */
#define WDT_SOFT_DIS		0xAA	/* Disable soft watchdog timer */

#define VOLT_MONITOR_MODE	0x0
#define THERMAL_DIODE_MODE	0x1
#define THERMISTOR_MODE		0x3

#define ENABLE_TSI	BIT(1)

#define WATCHDOG_TIMEOUT	1	/* 1 minute default timeout */

/*The timeout range is 1-255 minutes*/
#define MIN_TIMEOUT		(1 * 60)
#define MAX_TIMEOUT		(255 * 60)

static int timeout;
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in minutes. 1 <= timeout <= 255, default="
			__MODULE_STRING(WATCHDOG_TIMEOUT) ".");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
			__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static const unsigned short normal_i2c[] = {
	0x2d, 0x2e, I2C_CLIENT_END
};

struct nct7904_data {
	struct i2c_client *client;
	struct watchdog_device wdt;
	struct mutex bank_lock;
	int bank_sel;
	u32 fanin_mask;
	u32 vsen_mask;
	u32 tcpu_mask;
	u8 fan_mode[FANCTL_MAX];
	u8 enable_dts;
	u8 has_dts;
	u8 temp_mode; /* 0: TR mode, 1: TD mode */
	u8 fan_alarm[2];
	u8 vsen_alarm[3];
};

/* Access functions */
static int nct7904_bank_lock(struct nct7904_data *data, unsigned int bank)
{
	int ret;

	mutex_lock(&data->bank_lock);
	if (data->bank_sel == bank)
		return 0;
	ret = i2c_smbus_write_byte_data(data->client, BANK_SEL_REG, bank);
	if (ret == 0)
		data->bank_sel = bank;
	else
		data->bank_sel = -1;
	return ret;
}

static inline void nct7904_bank_release(struct nct7904_data *data)
{
	mutex_unlock(&data->bank_lock);
}

/* Read 1-byte register. Returns unsigned reg or -ERRNO on error. */
static int nct7904_read_reg(struct nct7904_data *data,
			    unsigned int bank, unsigned int reg)
{
	struct i2c_client *client = data->client;
	int ret;

	ret = nct7904_bank_lock(data, bank);
	if (ret == 0)
		ret = i2c_smbus_read_byte_data(client, reg);

	nct7904_bank_release(data);
	return ret;
}

/*
 * Read 2-byte register. Returns register in big-endian format or
 * -ERRNO on error.
 */
static int nct7904_read_reg16(struct nct7904_data *data,
			      unsigned int bank, unsigned int reg)
{
	struct i2c_client *client = data->client;
	int ret, hi;

	ret = nct7904_bank_lock(data, bank);
	if (ret == 0) {
		ret = i2c_smbus_read_byte_data(client, reg);
		if (ret >= 0) {
			hi = ret;
			ret = i2c_smbus_read_byte_data(client, reg + 1);
			if (ret >= 0)
				ret |= hi << 8;
		}
	}

	nct7904_bank_release(data);
	return ret;
}

/* Write 1-byte register. Returns 0 or -ERRNO on error. */
static int nct7904_write_reg(struct nct7904_data *data,
			     unsigned int bank, unsigned int reg, u8 val)
{
	struct i2c_client *client = data->client;
	int ret;

	ret = nct7904_bank_lock(data, bank);
	if (ret == 0)
		ret = i2c_smbus_write_byte_data(client, reg, val);

	nct7904_bank_release(data);
	return ret;
}

static int nct7904_read_fan(struct device *dev, u32 attr, int channel,
			    long *val)
{
	struct nct7904_data *data = dev_get_drvdata(dev);
	unsigned int cnt, rpm;
	int ret;

	switch (attr) {
	case hwmon_fan_input:
		ret = nct7904_read_reg16(data, BANK_0,
					 FANIN1_HV_REG + channel * 2);
		if (ret < 0)
			return ret;
		cnt = ((ret & 0xff00) >> 3) | (ret & 0x1f);
		if (cnt == 0 || cnt == 0x1fff)
			rpm = 0;
		else
			rpm = 1350000 / cnt;
		*val = rpm;
		return 0;
	case hwmon_fan_min:
		ret = nct7904_read_reg16(data, BANK_1,
					 FANIN1_HV_HL_REG + channel * 2);
		if (ret < 0)
			return ret;
		cnt = ((ret & 0xff00) >> 3) | (ret & 0x1f);
		if (cnt == 0 || cnt == 0x1fff)
			rpm = 0;
		else
			rpm = 1350000 / cnt;
		*val = rpm;
		return 0;
	case hwmon_fan_alarm:
		ret = nct7904_read_reg(data, BANK_0,
				       SMI_STS5_REG + (channel >> 3));
		if (ret < 0)
			return ret;
		if (!data->fan_alarm[channel >> 3])
			data->fan_alarm[channel >> 3] = ret & 0xff;
		else
			/* If there is new alarm showing up */
			data->fan_alarm[channel >> 3] |= (ret & 0xff);
		*val = (data->fan_alarm[channel >> 3] >> (channel & 0x07)) & 1;
		/* Needs to clean the alarm if alarm existing */
		if (*val)
			data->fan_alarm[channel >> 3] ^= 1 << (channel & 0x07);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t nct7904_fan_is_visible(const void *_data, u32 attr, int channel)
{
	const struct nct7904_data *data = _data;

	switch (attr) {
	case hwmon_fan_input:
	case hwmon_fan_alarm:
		if (data->fanin_mask & (1 << channel))
			return 0444;
		break;
	case hwmon_fan_min:
		if (data->fanin_mask & (1 << channel))
			return 0644;
		break;
	default:
		break;
	}

	return 0;
}

static u8 nct7904_chan_to_index[] = {
	0,	/* Not used */
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
	18, 19, 20, 16
};

static int nct7904_read_in(struct device *dev, u32 attr, int channel,
			   long *val)
{
	struct nct7904_data *data = dev_get_drvdata(dev);
	int ret, volt, index;

	index = nct7904_chan_to_index[channel];

	switch (attr) {
	case hwmon_in_input:
		ret = nct7904_read_reg16(data, BANK_0,
					 VSEN1_HV_REG + index * 2);
		if (ret < 0)
			return ret;
		volt = ((ret & 0xff00) >> 5) | (ret & 0x7);
		if (index < 14)
			volt *= 2; /* 0.002V scale */
		else
			volt *= 6; /* 0.006V scale */
		*val = volt;
		return 0;
	case hwmon_in_min:
		ret = nct7904_read_reg16(data, BANK_1,
					 VSEN1_HV_LL_REG + index * 4);
		if (ret < 0)
			return ret;
		volt = ((ret & 0xff00) >> 5) | (ret & 0x7);
		if (index < 14)
			volt *= 2; /* 0.002V scale */
		else
			volt *= 6; /* 0.006V scale */
		*val = volt;
		return 0;
	case hwmon_in_max:
		ret = nct7904_read_reg16(data, BANK_1,
					 VSEN1_HV_HL_REG + index * 4);
		if (ret < 0)
			return ret;
		volt = ((ret & 0xff00) >> 5) | (ret & 0x7);
		if (index < 14)
			volt *= 2; /* 0.002V scale */
		else
			volt *= 6; /* 0.006V scale */
		*val = volt;
		return 0;
	case hwmon_in_alarm:
		ret = nct7904_read_reg(data, BANK_0,
				       SMI_STS1_REG + (index >> 3));
		if (ret < 0)
			return ret;
		if (!data->vsen_alarm[index >> 3])
			data->vsen_alarm[index >> 3] = ret & 0xff;
		else
			/* If there is new alarm showing up */
			data->vsen_alarm[index >> 3] |= (ret & 0xff);
		*val = (data->vsen_alarm[index >> 3] >> (index & 0x07)) & 1;
		/* Needs to clean the alarm if alarm existing */
		if (*val)
			data->vsen_alarm[index >> 3] ^= 1 << (index & 0x07);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t nct7904_in_is_visible(const void *_data, u32 attr, int channel)
{
	const struct nct7904_data *data = _data;
	int index = nct7904_chan_to_index[channel];

	switch (attr) {
	case hwmon_in_input:
	case hwmon_in_alarm:
		if (channel > 0 && (data->vsen_mask & BIT(index)))
			return 0444;
		break;
	case hwmon_in_min:
	case hwmon_in_max:
		if (channel > 0 && (data->vsen_mask & BIT(index)))
			return 0644;
		break;
	default:
		break;
	}

	return 0;
}

static int nct7904_read_temp(struct device *dev, u32 attr, int channel,
			     long *val)
{
	struct nct7904_data *data = dev_get_drvdata(dev);
	int ret, temp;
	unsigned int reg1, reg2, reg3;
	s8 temps;

	switch (attr) {
	case hwmon_temp_input:
		if (channel == 4)
			ret = nct7904_read_reg16(data, BANK_0, LTD_HV_REG);
		else if (channel < 5)
			ret = nct7904_read_reg16(data, BANK_0,
						 TEMP_CH1_HV_REG + channel * 4);
		else
			ret = nct7904_read_reg16(data, BANK_0,
						 T_CPU1_HV_REG + (channel - 5)
						 * 2);
		if (ret < 0)
			return ret;
		temp = ((ret & 0xff00) >> 5) | (ret & 0x7);
		*val = sign_extend32(temp, 10) * 125;
		return 0;
	case hwmon_temp_alarm:
		if (channel == 4) {
			ret = nct7904_read_reg(data, BANK_0,
					       SMI_STS3_REG);
			if (ret < 0)
				return ret;
			*val = (ret >> 1) & 1;
		} else if (channel < 4) {
			ret = nct7904_read_reg(data, BANK_0,
					       SMI_STS1_REG);
			if (ret < 0)
				return ret;
			*val = (ret >> (((channel * 2) + 1) & 0x07)) & 1;
		} else {
			if ((channel - 5) < 4) {
				ret = nct7904_read_reg(data, BANK_0,
						       SMI_STS7_REG +
						       ((channel - 5) >> 3));
				if (ret < 0)
					return ret;
				*val = (ret >> ((channel - 5) & 0x07)) & 1;
			} else {
				ret = nct7904_read_reg(data, BANK_0,
						       SMI_STS8_REG +
						       ((channel - 5) >> 3));
				if (ret < 0)
					return ret;
				*val = (ret >> (((channel - 5) & 0x07) - 4))
							& 1;
			}
		}
		return 0;
	case hwmon_temp_type:
		if (channel < 5) {
			if ((data->tcpu_mask >> channel) & 0x01) {
				if ((data->temp_mode >> channel) & 0x01)
					*val = 3; /* TD */
				else
					*val = 4; /* TR */
			} else {
				*val = 0;
			}
		} else {
			if ((data->has_dts >> (channel - 5)) & 0x01) {
				if (data->enable_dts & ENABLE_TSI)
					*val = 5; /* TSI */
				else
					*val = 6; /* PECI */
			} else {
				*val = 0;
			}
		}
		return 0;
	case hwmon_temp_max:
		reg1 = LTD_HV_LL_REG;
		reg2 = TEMP_CH1_W_REG;
		reg3 = DTS_T_CPU1_W_REG;
		break;
	case hwmon_temp_max_hyst:
		reg1 = LTD_LV_LL_REG;
		reg2 = TEMP_CH1_WH_REG;
		reg3 = DTS_T_CPU1_WH_REG;
		break;
	case hwmon_temp_crit:
		reg1 = LTD_HV_HL_REG;
		reg2 = TEMP_CH1_C_REG;
		reg3 = DTS_T_CPU1_C_REG;
		break;
	case hwmon_temp_crit_hyst:
		reg1 = LTD_LV_HL_REG;
		reg2 = TEMP_CH1_CH_REG;
		reg3 = DTS_T_CPU1_CH_REG;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (channel == 4)
		ret = nct7904_read_reg(data, BANK_1, reg1);
	else if (channel < 5)
		ret = nct7904_read_reg(data, BANK_1,
				       reg2 + channel * 8);
	else
		ret = nct7904_read_reg(data, BANK_1,
				       reg3 + (channel - 5) * 4);

	if (ret < 0)
		return ret;
	temps = ret;
	*val = temps * 1000;
	return 0;
}

static umode_t nct7904_temp_is_visible(const void *_data, u32 attr, int channel)
{
	const struct nct7904_data *data = _data;

	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_alarm:
	case hwmon_temp_type:
		if (channel < 5) {
			if (data->tcpu_mask & BIT(channel))
				return 0444;
		} else {
			if (data->has_dts & BIT(channel - 5))
				return 0444;
		}
		break;
	case hwmon_temp_max:
	case hwmon_temp_max_hyst:
	case hwmon_temp_crit:
	case hwmon_temp_crit_hyst:
		if (channel < 5) {
			if (data->tcpu_mask & BIT(channel))
				return 0644;
		} else {
			if (data->has_dts & BIT(channel - 5))
				return 0644;
		}
		break;
	default:
		break;
	}

	return 0;
}

static int nct7904_read_pwm(struct device *dev, u32 attr, int channel,
			    long *val)
{
	struct nct7904_data *data = dev_get_drvdata(dev);
	int ret;

	switch (attr) {
	case hwmon_pwm_input:
		ret = nct7904_read_reg(data, BANK_3, FANCTL1_OUT_REG + channel);
		if (ret < 0)
			return ret;
		*val = ret;
		return 0;
	case hwmon_pwm_enable:
		ret = nct7904_read_reg(data, BANK_3, FANCTL1_FMR_REG + channel);
		if (ret < 0)
			return ret;

		*val = ret ? 2 : 1;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int nct7904_write_temp(struct device *dev, u32 attr, int channel,
			      long val)
{
	struct nct7904_data *data = dev_get_drvdata(dev);
	int ret;
	unsigned int reg1, reg2, reg3;

	val = clamp_val(val / 1000, -128, 127);

	switch (attr) {
	case hwmon_temp_max:
		reg1 = LTD_HV_LL_REG;
		reg2 = TEMP_CH1_W_REG;
		reg3 = DTS_T_CPU1_W_REG;
		break;
	case hwmon_temp_max_hyst:
		reg1 = LTD_LV_LL_REG;
		reg2 = TEMP_CH1_WH_REG;
		reg3 = DTS_T_CPU1_WH_REG;
		break;
	case hwmon_temp_crit:
		reg1 = LTD_HV_HL_REG;
		reg2 = TEMP_CH1_C_REG;
		reg3 = DTS_T_CPU1_C_REG;
		break;
	case hwmon_temp_crit_hyst:
		reg1 = LTD_LV_HL_REG;
		reg2 = TEMP_CH1_CH_REG;
		reg3 = DTS_T_CPU1_CH_REG;
		break;
	default:
		return -EOPNOTSUPP;
	}
	if (channel == 4)
		ret = nct7904_write_reg(data, BANK_1, reg1, val);
	else if (channel < 5)
		ret = nct7904_write_reg(data, BANK_1,
					reg2 + channel * 8, val);
	else
		ret = nct7904_write_reg(data, BANK_1,
					reg3 + (channel - 5) * 4, val);

	return ret;
}

static int nct7904_write_fan(struct device *dev, u32 attr, int channel,
			     long val)
{
	struct nct7904_data *data = dev_get_drvdata(dev);
	int ret;
	u8 tmp;

	switch (attr) {
	case hwmon_fan_min:
		if (val <= 0)
			return -EINVAL;

		val = clamp_val(DIV_ROUND_CLOSEST(1350000, val), 1, 0x1fff);
		tmp = (val >> 5) & 0xff;
		ret = nct7904_write_reg(data, BANK_1,
					FANIN1_HV_HL_REG + channel * 2, tmp);
		if (ret < 0)
			return ret;
		tmp = val & 0x1f;
		ret = nct7904_write_reg(data, BANK_1,
					FANIN1_LV_HL_REG + channel * 2, tmp);
		return ret;
	default:
		return -EOPNOTSUPP;
	}
}

static int nct7904_write_in(struct device *dev, u32 attr, int channel,
			    long val)
{
	struct nct7904_data *data = dev_get_drvdata(dev);
	int ret, index, tmp;

	index = nct7904_chan_to_index[channel];

	if (index < 14)
		val = val / 2; /* 0.002V scale */
	else
		val = val / 6; /* 0.006V scale */

	val = clamp_val(val, 0, 0x7ff);

	switch (attr) {
	case hwmon_in_min:
		tmp = nct7904_read_reg(data, BANK_1,
				       VSEN1_LV_LL_REG + index * 4);
		if (tmp < 0)
			return tmp;
		tmp &= ~0x7;
		tmp |= val & 0x7;
		ret = nct7904_write_reg(data, BANK_1,
					VSEN1_LV_LL_REG + index * 4, tmp);
		if (ret < 0)
			return ret;
		tmp = nct7904_read_reg(data, BANK_1,
				       VSEN1_HV_LL_REG + index * 4);
		if (tmp < 0)
			return tmp;
		tmp = (val >> 3) & 0xff;
		ret = nct7904_write_reg(data, BANK_1,
					VSEN1_HV_LL_REG + index * 4, tmp);
		return ret;
	case hwmon_in_max:
		tmp = nct7904_read_reg(data, BANK_1,
				       VSEN1_LV_HL_REG + index * 4);
		if (tmp < 0)
			return tmp;
		tmp &= ~0x7;
		tmp |= val & 0x7;
		ret = nct7904_write_reg(data, BANK_1,
					VSEN1_LV_HL_REG + index * 4, tmp);
		if (ret < 0)
			return ret;
		tmp = nct7904_read_reg(data, BANK_1,
				       VSEN1_HV_HL_REG + index * 4);
		if (tmp < 0)
			return tmp;
		tmp = (val >> 3) & 0xff;
		ret = nct7904_write_reg(data, BANK_1,
					VSEN1_HV_HL_REG + index * 4, tmp);
		return ret;
	default:
		return -EOPNOTSUPP;
	}
}

static int nct7904_write_pwm(struct device *dev, u32 attr, int channel,
			     long val)
{
	struct nct7904_data *data = dev_get_drvdata(dev);
	int ret;

	switch (attr) {
	case hwmon_pwm_input:
		if (val < 0 || val > 255)
			return -EINVAL;
		ret = nct7904_write_reg(data, BANK_3, FANCTL1_OUT_REG + channel,
					val);
		return ret;
	case hwmon_pwm_enable:
		if (val < 1 || val > 2 ||
		    (val == 2 && !data->fan_mode[channel]))
			return -EINVAL;
		ret = nct7904_write_reg(data, BANK_3, FANCTL1_FMR_REG + channel,
					val == 2 ? data->fan_mode[channel] : 0);
		return ret;
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t nct7904_pwm_is_visible(const void *_data, u32 attr, int channel)
{
	switch (attr) {
	case hwmon_pwm_input:
	case hwmon_pwm_enable:
		return 0644;
	default:
		return 0;
	}
}

static int nct7904_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_in:
		return nct7904_read_in(dev, attr, channel, val);
	case hwmon_fan:
		return nct7904_read_fan(dev, attr, channel, val);
	case hwmon_pwm:
		return nct7904_read_pwm(dev, attr, channel, val);
	case hwmon_temp:
		return nct7904_read_temp(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int nct7904_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_in:
		return nct7904_write_in(dev, attr, channel, val);
	case hwmon_fan:
		return nct7904_write_fan(dev, attr, channel, val);
	case hwmon_pwm:
		return nct7904_write_pwm(dev, attr, channel, val);
	case hwmon_temp:
		return nct7904_write_temp(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t nct7904_is_visible(const void *data,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	switch (type) {
	case hwmon_in:
		return nct7904_in_is_visible(data, attr, channel);
	case hwmon_fan:
		return nct7904_fan_is_visible(data, attr, channel);
	case hwmon_pwm:
		return nct7904_pwm_is_visible(data, attr, channel);
	case hwmon_temp:
		return nct7904_temp_is_visible(data, attr, channel);
	default:
		return 0;
	}
}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int nct7904_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;

	if (!i2c_check_functionality(adapter,
				     I2C_FUNC_SMBUS_READ_BYTE |
				     I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return -ENODEV;

	/* Determine the chip type. */
	if (i2c_smbus_read_byte_data(client, VENDOR_ID_REG) != NUVOTON_ID ||
	    i2c_smbus_read_byte_data(client, CHIP_ID_REG) != NCT7904_ID ||
	    (i2c_smbus_read_byte_data(client, DEVICE_ID_REG) & 0xf0) != 0x50 ||
	    (i2c_smbus_read_byte_data(client, BANK_SEL_REG) & 0xf8) != 0x00)
		return -ENODEV;

	strlcpy(info->type, "nct7904", I2C_NAME_SIZE);

	return 0;
}

static const struct hwmon_channel_info *nct7904_info[] = {
	HWMON_CHANNEL_INFO(in,
			   /* dummy, skipped in is_visible */
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_MAX |
			   HWMON_I_ALARM),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_ALARM),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_MAX |
			   HWMON_T_MAX_HYST | HWMON_T_TYPE | HWMON_T_CRIT |
			   HWMON_T_CRIT_HYST,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_MAX |
			   HWMON_T_MAX_HYST | HWMON_T_TYPE | HWMON_T_CRIT |
			   HWMON_T_CRIT_HYST,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_MAX |
			   HWMON_T_MAX_HYST | HWMON_T_TYPE | HWMON_T_CRIT |
			   HWMON_T_CRIT_HYST,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_MAX |
			   HWMON_T_MAX_HYST | HWMON_T_TYPE | HWMON_T_CRIT |
			   HWMON_T_CRIT_HYST,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_MAX |
			   HWMON_T_MAX_HYST | HWMON_T_TYPE | HWMON_T_CRIT |
			   HWMON_T_CRIT_HYST,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_MAX |
			   HWMON_T_MAX_HYST | HWMON_T_TYPE | HWMON_T_CRIT |
			   HWMON_T_CRIT_HYST,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_MAX |
			   HWMON_T_MAX_HYST | HWMON_T_TYPE | HWMON_T_CRIT |
			   HWMON_T_CRIT_HYST,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_MAX |
			   HWMON_T_MAX_HYST | HWMON_T_TYPE | HWMON_T_CRIT |
			   HWMON_T_CRIT_HYST,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_MAX |
			   HWMON_T_MAX_HYST | HWMON_T_TYPE | HWMON_T_CRIT |
			   HWMON_T_CRIT_HYST,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_MAX |
			   HWMON_T_MAX_HYST | HWMON_T_TYPE | HWMON_T_CRIT |
			   HWMON_T_CRIT_HYST,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_MAX |
			   HWMON_T_MAX_HYST | HWMON_T_TYPE | HWMON_T_CRIT |
			   HWMON_T_CRIT_HYST,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_MAX |
			   HWMON_T_MAX_HYST | HWMON_T_TYPE | HWMON_T_CRIT |
			   HWMON_T_CRIT_HYST,
			   HWMON_T_INPUT | HWMON_T_ALARM | HWMON_T_MAX |
			   HWMON_T_MAX_HYST | HWMON_T_TYPE | HWMON_T_CRIT |
			   HWMON_T_CRIT_HYST),
	NULL
};

static const struct hwmon_ops nct7904_hwmon_ops = {
	.is_visible = nct7904_is_visible,
	.read = nct7904_read,
	.write = nct7904_write,
};

static const struct hwmon_chip_info nct7904_chip_info = {
	.ops = &nct7904_hwmon_ops,
	.info = nct7904_info,
};

/*
 * Watchdog Function
 */
static int nct7904_wdt_start(struct watchdog_device *wdt)
{
	struct nct7904_data *data = watchdog_get_drvdata(wdt);

	/* Enable soft watchdog timer */
	return nct7904_write_reg(data, BANK_0, WDT_LOCK_REG, WDT_SOFT_EN);
}

static int nct7904_wdt_stop(struct watchdog_device *wdt)
{
	struct nct7904_data *data = watchdog_get_drvdata(wdt);

	return nct7904_write_reg(data, BANK_0, WDT_LOCK_REG, WDT_SOFT_DIS);
}

static int nct7904_wdt_set_timeout(struct watchdog_device *wdt,
				   unsigned int timeout)
{
	struct nct7904_data *data = watchdog_get_drvdata(wdt);
	/*
	 * The NCT7904 is very special in watchdog function.
	 * Its minimum unit is minutes. And wdt->timeout needs
	 * to match the actual timeout selected. So, this needs
	 * to be: wdt->timeout = timeout / 60 * 60.
	 * For example, if the user configures a timeout of
	 * 119 seconds, the actual timeout will be 60 seconds.
	 * So, wdt->timeout must then be set to 60 seconds.
	 */
	wdt->timeout = timeout / 60 * 60;

	return nct7904_write_reg(data, BANK_0, WDT_TIMER_REG,
				 wdt->timeout / 60);
}

static int nct7904_wdt_ping(struct watchdog_device *wdt)
{
	/*
	 * Note:
	 * NCT7904 does not support refreshing WDT_TIMER_REG register when
	 * the watchdog is active. Please disable watchdog before feeding
	 * the watchdog and enable it again.
	 */
	struct nct7904_data *data = watchdog_get_drvdata(wdt);
	int ret;

	/* Disable soft watchdog timer */
	ret = nct7904_write_reg(data, BANK_0, WDT_LOCK_REG, WDT_SOFT_DIS);
	if (ret < 0)
		return ret;

	/* feed watchdog */
	ret = nct7904_write_reg(data, BANK_0, WDT_TIMER_REG, wdt->timeout / 60);
	if (ret < 0)
		return ret;

	/* Enable soft watchdog timer */
	return nct7904_write_reg(data, BANK_0, WDT_LOCK_REG, WDT_SOFT_EN);
}

static unsigned int nct7904_wdt_get_timeleft(struct watchdog_device *wdt)
{
	struct nct7904_data *data = watchdog_get_drvdata(wdt);
	int ret;

	ret = nct7904_read_reg(data, BANK_0, WDT_TIMER_REG);
	if (ret < 0)
		return 0;

	return ret * 60;
}

static const struct watchdog_info nct7904_wdt_info = {
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING |
				WDIOF_MAGICCLOSE,
	.identity	= "nct7904 watchdog",
};

static const struct watchdog_ops nct7904_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= nct7904_wdt_start,
	.stop		= nct7904_wdt_stop,
	.ping		= nct7904_wdt_ping,
	.set_timeout	= nct7904_wdt_set_timeout,
	.get_timeleft	= nct7904_wdt_get_timeleft,
};

static int nct7904_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct nct7904_data *data;
	struct device *hwmon_dev;
	struct device *dev = &client->dev;
	int ret, i;
	u32 mask;
	u8 val, bit;

	data = devm_kzalloc(dev, sizeof(struct nct7904_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->bank_lock);
	data->bank_sel = -1;

	/* Setup sensor groups. */
	/* FANIN attributes */
	ret = nct7904_read_reg16(data, BANK_0, FANIN_CTRL0_REG);
	if (ret < 0)
		return ret;
	data->fanin_mask = (ret >> 8) | ((ret & 0xff) << 8);

	/*
	 * VSEN attributes
	 *
	 * Note: voltage sensors overlap with external temperature
	 * sensors. So, if we ever decide to support the latter
	 * we will have to adjust 'vsen_mask' accordingly.
	 */
	mask = 0;
	ret = nct7904_read_reg16(data, BANK_0, VT_ADC_CTRL0_REG);
	if (ret >= 0)
		mask = (ret >> 8) | ((ret & 0xff) << 8);
	ret = nct7904_read_reg(data, BANK_0, VT_ADC_CTRL2_REG);
	if (ret >= 0)
		mask |= (ret << 16);
	data->vsen_mask = mask;

	/* CPU_TEMP attributes */
	ret = nct7904_read_reg(data, BANK_0, VT_ADC_CTRL0_REG);
	if (ret < 0)
		return ret;

	if ((ret & 0x6) == 0x6)
		data->tcpu_mask |= 1; /* TR1 */
	if ((ret & 0x18) == 0x18)
		data->tcpu_mask |= 2; /* TR2 */
	if ((ret & 0x20) == 0x20)
		data->tcpu_mask |= 4; /* TR3 */
	if ((ret & 0x80) == 0x80)
		data->tcpu_mask |= 8; /* TR4 */

	/* LTD */
	ret = nct7904_read_reg(data, BANK_0, VT_ADC_CTRL2_REG);
	if (ret < 0)
		return ret;
	if ((ret & 0x02) == 0x02)
		data->tcpu_mask |= 0x10;

	/* Multi-Function detecting for Volt and TR/TD */
	ret = nct7904_read_reg(data, BANK_0, VT_ADC_MD_REG);
	if (ret < 0)
		return ret;

	data->temp_mode = 0;
	for (i = 0; i < 4; i++) {
		val = (ret >> (i * 2)) & 0x03;
		bit = (1 << i);
		if (val == VOLT_MONITOR_MODE) {
			data->tcpu_mask &= ~bit;
		} else if (val == THERMAL_DIODE_MODE && i < 2) {
			data->temp_mode |= bit;
			data->vsen_mask &= ~(0x06 << (i * 2));
		} else if (val == THERMISTOR_MODE) {
			data->vsen_mask &= ~(0x02 << (i * 2));
		} else {
			/* Reserved */
			data->tcpu_mask &= ~bit;
			data->vsen_mask &= ~(0x06 << (i * 2));
		}
	}

	/* PECI */
	ret = nct7904_read_reg(data, BANK_2, PFE_REG);
	if (ret < 0)
		return ret;
	if (ret & 0x80) {
		data->enable_dts = 1; /* Enable DTS & PECI */
	} else {
		ret = nct7904_read_reg(data, BANK_2, TSI_CTRL_REG);
		if (ret < 0)
			return ret;
		if (ret & 0x80)
			data->enable_dts = 0x3; /* Enable DTS & TSI */
	}

	/* Check DTS enable status */
	if (data->enable_dts) {
		ret = nct7904_read_reg(data, BANK_0, DTS_T_CTRL0_REG);
		if (ret < 0)
			return ret;
		data->has_dts = ret & 0xF;
		if (data->enable_dts & ENABLE_TSI) {
			ret = nct7904_read_reg(data, BANK_0, DTS_T_CTRL1_REG);
			if (ret < 0)
				return ret;
			data->has_dts |= (ret & 0xF) << 4;
		}
	}

	for (i = 0; i < FANCTL_MAX; i++) {
		ret = nct7904_read_reg(data, BANK_3, FANCTL1_FMR_REG + i);
		if (ret < 0)
			return ret;
		data->fan_mode[i] = ret;
	}

	/* Read all of SMI status register to clear alarms */
	for (i = 0; i < SMI_STS_MAX; i++) {
		ret = nct7904_read_reg(data, BANK_0, SMI_STS1_REG + i);
		if (ret < 0)
			return ret;
	}

	hwmon_dev =
		devm_hwmon_device_register_with_info(dev, client->name, data,
						     &nct7904_chip_info, NULL);
	ret = PTR_ERR_OR_ZERO(hwmon_dev);
	if (ret)
		return ret;

	/* Watchdog initialization */
	data->wdt.ops = &nct7904_wdt_ops;
	data->wdt.info = &nct7904_wdt_info;

	data->wdt.timeout = WATCHDOG_TIMEOUT * 60; /* Set default timeout */
	data->wdt.min_timeout = MIN_TIMEOUT;
	data->wdt.max_timeout = MAX_TIMEOUT;
	data->wdt.parent = &client->dev;

	watchdog_init_timeout(&data->wdt, timeout * 60, &client->dev);
	watchdog_set_nowayout(&data->wdt, nowayout);
	watchdog_set_drvdata(&data->wdt, data);

	watchdog_stop_on_unregister(&data->wdt);

	return devm_watchdog_register_device(dev, &data->wdt);
}

static const struct i2c_device_id nct7904_id[] = {
	{"nct7904", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, nct7904_id);

static struct i2c_driver nct7904_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = "nct7904",
	},
	.probe = nct7904_probe,
	.id_table = nct7904_id,
	.detect = nct7904_detect,
	.address_list = normal_i2c,
};

module_i2c_driver(nct7904_driver);

MODULE_AUTHOR("Vadim V. Vlasov <vvlasov@dev.rtsoft.ru>");
MODULE_DESCRIPTION("Hwmon driver for NUVOTON NCT7904");
MODULE_LICENSE("GPL");
