// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NCT6694 HWMON driver based on USB interface.
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 */

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/hwmon.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/mfd/nct6694.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/*
 * USB command module type for NCT6694 report channel
 * This defines the module type used for communication with the NCT6694
 * report channel over the USB interface.
 */
#define NCT6694_RPT_MOD			0xFF

/* Report channel */
/*
 * The report channel is used to report the status of the hardware monitor
 * devices, such as voltage, temperature, fan speed, and PWM.
 */
#define NCT6694_VIN_IDX(x)		(0x00 + (x))
#define NCT6694_TIN_IDX(x)			\
	({ typeof(x) (_x) = (x);		\
	 ((_x) < 10) ? (0x10 + ((_x) * 2)) :	\
	 (0x30 + (((_x) - 10) * 2)); })
#define NCT6694_FIN_IDX(x)		(0x50 + ((x) * 2))
#define NCT6694_PWM_IDX(x)		(0x70 + (x))
#define NCT6694_VIN_STS(x)		(0x68 + (x))
#define NCT6694_TIN_STS(x)		(0x6A + (x))
#define NCT6694_FIN_STS(x)		(0x6E + (x))

/*
 * USB command module type for NCT6694 HWMON controller.
 * This defines the module type used for communication with the NCT6694
 * HWMON controller over the USB interface.
 */
#define NCT6694_HWMON_MOD		0x00

/* Command 00h - Hardware Monitor Control */
#define NCT6694_HWMON_CONTROL		0x00
#define NCT6694_HWMON_CONTROL_SEL	0x00

/* Command 02h - Alarm Control */
#define NCT6694_HWMON_ALARM		0x02
#define NCT6694_HWMON_ALARM_SEL		0x00

/*
 * USB command module type for NCT6694 PWM controller.
 * This defines the module type used for communication with the NCT6694
 * PWM controller over the USB interface.
 */
#define NCT6694_PWM_MOD			0x01

/* PWM Command - Manual Control */
#define NCT6694_PWM_CONTROL		0x01
#define NCT6694_PWM_CONTROL_SEL		0x00

#define NCT6694_FREQ_FROM_REG(reg)	((reg) * 25000 / 255)
#define NCT6694_FREQ_TO_REG(val)	\
	(DIV_ROUND_CLOSEST(clamp_val((val), 100, 25000) * 255, 25000))

#define NCT6694_LSB_REG_MASK		GENMASK(7, 5)
#define NCT6694_TIN_HYST_MASK		GENMASK(7, 5)

enum nct6694_hwmon_temp_mode {
	NCT6694_HWMON_TWOTIME_IRQ = 0,
	NCT6694_HWMON_ONETIME_IRQ,
	NCT6694_HWMON_REALTIME_IRQ,
	NCT6694_HWMON_COMPARE_IRQ,
};

struct __packed nct6694_hwmon_control {
	u8 vin_en[2];
	u8 tin_en[2];
	u8 fin_en[2];
	u8 pwm_en[2];
	u8 reserved1[40];
	u8 pwm_freq[10];
	u8 reserved2[6];
};

struct __packed nct6694_hwmon_alarm {
	u8 smi_ctrl;
	u8 reserved1[15];
	struct {
		u8 hl;
		u8 ll;
	} vin_limit[16];
	struct {
		u8 hyst;
		s8 hl;
	} tin_cfg[32];
	__be16 fin_ll[10];
	u8 reserved2[4];
};

struct __packed nct6694_pwm_control {
	u8 mal_en[2];
	u8 mal_val[10];
	u8 reserved[12];
};

union __packed nct6694_hwmon_rpt {
	u8 vin;
	struct {
		u8 msb;
		u8 lsb;
	} tin;
	__be16 fin;
	u8 pwm;
	u8 status;
};

union __packed nct6694_hwmon_msg {
	struct nct6694_hwmon_alarm hwmon_alarm;
	struct nct6694_pwm_control pwm_ctrl;
};

struct nct6694_hwmon_data {
	struct nct6694 *nct6694;
	struct mutex lock;
	struct nct6694_hwmon_control hwmon_en;
	union nct6694_hwmon_rpt *rpt;
	union nct6694_hwmon_msg *msg;
};

static inline long in_from_reg(u8 reg)
{
	return reg * 16;
}

static inline u8 in_to_reg(long val)
{
	return DIV_ROUND_CLOSEST(val, 16);
}

static inline long temp_from_reg(s8 reg)
{
	return reg * 1000;
}

static inline s8 temp_to_reg(long val)
{
	return DIV_ROUND_CLOSEST(val, 1000);
}

#define NCT6694_HWMON_IN_CONFIG (HWMON_I_INPUT | HWMON_I_ENABLE |	\
				 HWMON_I_MAX | HWMON_I_MIN |		\
				 HWMON_I_ALARM)
#define NCT6694_HWMON_TEMP_CONFIG (HWMON_T_INPUT | HWMON_T_ENABLE |	\
				   HWMON_T_MAX | HWMON_T_MAX_HYST |	\
				   HWMON_T_MAX_ALARM)
#define NCT6694_HWMON_FAN_CONFIG (HWMON_F_INPUT | HWMON_F_ENABLE |	\
				  HWMON_F_MIN | HWMON_F_MIN_ALARM)
#define NCT6694_HWMON_PWM_CONFIG (HWMON_PWM_INPUT | HWMON_PWM_ENABLE |	\
				  HWMON_PWM_FREQ)
static const struct hwmon_channel_info *nct6694_info[] = {
	HWMON_CHANNEL_INFO(in,
			   NCT6694_HWMON_IN_CONFIG,	/* VIN0 */
			   NCT6694_HWMON_IN_CONFIG,	/* VIN1 */
			   NCT6694_HWMON_IN_CONFIG,	/* VIN2 */
			   NCT6694_HWMON_IN_CONFIG,	/* VIN3 */
			   NCT6694_HWMON_IN_CONFIG,	/* VIN5 */
			   NCT6694_HWMON_IN_CONFIG,	/* VIN6 */
			   NCT6694_HWMON_IN_CONFIG,	/* VIN7 */
			   NCT6694_HWMON_IN_CONFIG,	/* VIN14 */
			   NCT6694_HWMON_IN_CONFIG,	/* VIN15 */
			   NCT6694_HWMON_IN_CONFIG,	/* VIN16 */
			   NCT6694_HWMON_IN_CONFIG,	/* VBAT */
			   NCT6694_HWMON_IN_CONFIG,	/* VSB */
			   NCT6694_HWMON_IN_CONFIG,	/* AVSB */
			   NCT6694_HWMON_IN_CONFIG,	/* VCC */
			   NCT6694_HWMON_IN_CONFIG,	/* VHIF */
			   NCT6694_HWMON_IN_CONFIG),	/* VTT */

	HWMON_CHANNEL_INFO(temp,
			   NCT6694_HWMON_TEMP_CONFIG,	/* THR1 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* THR2 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* THR14 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* THR15 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* THR16 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* TDP0 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* TDP1 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* TDP2 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* TDP3 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* TDP4 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* DTIN0 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* DTIN1 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* DTIN2 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* DTIN3 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* DTIN4 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* DTIN5 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* DTIN6 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* DTIN7 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* DTIN8 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* DTIN9 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* DTIN10 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* DTIN11 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* DTIN12 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* DTIN13 */
			   NCT6694_HWMON_TEMP_CONFIG,	/* DTIN14 */
			   NCT6694_HWMON_TEMP_CONFIG),	/* DTIN15 */

	HWMON_CHANNEL_INFO(fan,
			   NCT6694_HWMON_FAN_CONFIG,	/* FIN0 */
			   NCT6694_HWMON_FAN_CONFIG,	/* FIN1 */
			   NCT6694_HWMON_FAN_CONFIG,	/* FIN2 */
			   NCT6694_HWMON_FAN_CONFIG,	/* FIN3 */
			   NCT6694_HWMON_FAN_CONFIG,	/* FIN4 */
			   NCT6694_HWMON_FAN_CONFIG,	/* FIN5 */
			   NCT6694_HWMON_FAN_CONFIG,	/* FIN6 */
			   NCT6694_HWMON_FAN_CONFIG,	/* FIN7 */
			   NCT6694_HWMON_FAN_CONFIG,	/* FIN8 */
			   NCT6694_HWMON_FAN_CONFIG),	/* FIN9 */

	HWMON_CHANNEL_INFO(pwm,
			   NCT6694_HWMON_PWM_CONFIG,	/* PWM0 */
			   NCT6694_HWMON_PWM_CONFIG,	/* PWM1 */
			   NCT6694_HWMON_PWM_CONFIG,	/* PWM2 */
			   NCT6694_HWMON_PWM_CONFIG,	/* PWM3 */
			   NCT6694_HWMON_PWM_CONFIG,	/* PWM4 */
			   NCT6694_HWMON_PWM_CONFIG,	/* PWM5 */
			   NCT6694_HWMON_PWM_CONFIG,	/* PWM6 */
			   NCT6694_HWMON_PWM_CONFIG,	/* PWM7 */
			   NCT6694_HWMON_PWM_CONFIG,	/* PWM8 */
			   NCT6694_HWMON_PWM_CONFIG),	/* PWM9 */
	NULL
};

static int nct6694_in_read(struct device *dev, u32 attr, int channel,
			   long *val)
{
	struct nct6694_hwmon_data *data = dev_get_drvdata(dev);
	struct nct6694_cmd_header cmd_hd;
	unsigned char vin_en;
	int ret;

	guard(mutex)(&data->lock);

	switch (attr) {
	case hwmon_in_enable:
		vin_en = data->hwmon_en.vin_en[(channel / 8)];
		*val = !!(vin_en & BIT(channel % 8));

		return 0;
	case hwmon_in_input:
		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_RPT_MOD,
			.offset = cpu_to_le16(NCT6694_VIN_IDX(channel)),
			.len = cpu_to_le16(sizeof(data->rpt->vin))
		};
		ret = nct6694_read_msg(data->nct6694, &cmd_hd,
				       &data->rpt->vin);
		if (ret)
			return ret;

		*val = in_from_reg(data->rpt->vin);

		return 0;
	case hwmon_in_max:
		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_HWMON_MOD,
			.cmd = NCT6694_HWMON_ALARM,
			.sel = NCT6694_HWMON_ALARM_SEL,
			.len = cpu_to_le16(sizeof(data->msg->hwmon_alarm))
		};
		ret = nct6694_read_msg(data->nct6694, &cmd_hd,
				       &data->msg->hwmon_alarm);
		if (ret)
			return ret;

		*val = in_from_reg(data->msg->hwmon_alarm.vin_limit[channel].hl);

		return 0;
	case hwmon_in_min:
		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_HWMON_MOD,
			.cmd = NCT6694_HWMON_ALARM,
			.sel = NCT6694_HWMON_ALARM_SEL,
			.len = cpu_to_le16(sizeof(data->msg->hwmon_alarm))
		};
		ret = nct6694_read_msg(data->nct6694, &cmd_hd,
				       &data->msg->hwmon_alarm);
		if (ret)
			return ret;

		*val = in_from_reg(data->msg->hwmon_alarm.vin_limit[channel].ll);

		return 0;
	case hwmon_in_alarm:
		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_RPT_MOD,
			.offset = cpu_to_le16(NCT6694_VIN_STS(channel / 8)),
			.len = cpu_to_le16(sizeof(data->rpt->status))
		};
		ret = nct6694_read_msg(data->nct6694, &cmd_hd,
				       &data->rpt->status);
		if (ret)
			return ret;

		*val = !!(data->rpt->status & BIT(channel % 8));

		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int nct6694_temp_read(struct device *dev, u32 attr, int channel,
			     long *val)
{
	struct nct6694_hwmon_data *data = dev_get_drvdata(dev);
	struct nct6694_cmd_header cmd_hd;
	unsigned char temp_en, temp_hyst;
	signed char temp_max;
	int ret, temp_raw;

	guard(mutex)(&data->lock);

	switch (attr) {
	case hwmon_temp_enable:
		temp_en = data->hwmon_en.tin_en[channel / 8];
		*val = !!(temp_en & BIT(channel % 8));

		return 0;
	case hwmon_temp_input:
		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_RPT_MOD,
			.offset = cpu_to_le16(NCT6694_TIN_IDX(channel)),
			.len = cpu_to_le16(sizeof(data->rpt->tin))
		};
		ret = nct6694_read_msg(data->nct6694, &cmd_hd,
				       &data->rpt->tin);
		if (ret)
			return ret;

		temp_raw = data->rpt->tin.msb << 3;
		temp_raw |= FIELD_GET(NCT6694_LSB_REG_MASK, data->rpt->tin.lsb);

		/* Real temperature(milli degrees Celsius) = temp_raw * 1000 * 0.125 */
		*val = sign_extend32(temp_raw, 10) * 125;

		return 0;
	case hwmon_temp_max:
		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_HWMON_MOD,
			.cmd = NCT6694_HWMON_ALARM,
			.sel = NCT6694_HWMON_ALARM_SEL,
			.len = cpu_to_le16(sizeof(data->msg->hwmon_alarm))
		};
		ret = nct6694_read_msg(data->nct6694, &cmd_hd,
				       &data->msg->hwmon_alarm);
		if (ret)
			return ret;

		*val = temp_from_reg(data->msg->hwmon_alarm.tin_cfg[channel].hl);

		return 0;
	case hwmon_temp_max_hyst:
		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_HWMON_MOD,
			.cmd = NCT6694_HWMON_ALARM,
			.sel = NCT6694_HWMON_ALARM_SEL,
			.len = cpu_to_le16(sizeof(data->msg->hwmon_alarm))
		};
		ret = nct6694_read_msg(data->nct6694, &cmd_hd,
				       &data->msg->hwmon_alarm);
		if (ret)
			return ret;

		temp_max = data->msg->hwmon_alarm.tin_cfg[channel].hl;
		temp_hyst = FIELD_GET(NCT6694_TIN_HYST_MASK,
				      data->msg->hwmon_alarm.tin_cfg[channel].hyst);
		*val = temp_from_reg(temp_max - temp_hyst);

		return 0;
	case hwmon_temp_max_alarm:
		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_RPT_MOD,
			.offset = cpu_to_le16(NCT6694_TIN_STS(channel / 8)),
			.len = cpu_to_le16(sizeof(data->rpt->status))
		};
		ret = nct6694_read_msg(data->nct6694, &cmd_hd,
				       &data->rpt->status);
		if (ret)
			return ret;

		*val = !!(data->rpt->status & BIT(channel % 8));

		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int nct6694_fan_read(struct device *dev, u32 attr, int channel,
			    long *val)
{
	struct nct6694_hwmon_data *data = dev_get_drvdata(dev);
	struct nct6694_cmd_header cmd_hd;
	unsigned char fanin_en;
	int ret;

	guard(mutex)(&data->lock);

	switch (attr) {
	case hwmon_fan_enable:
		fanin_en = data->hwmon_en.fin_en[channel / 8];
		*val = !!(fanin_en & BIT(channel % 8));

		return 0;
	case hwmon_fan_input:
		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_RPT_MOD,
			.offset = cpu_to_le16(NCT6694_FIN_IDX(channel)),
			.len = cpu_to_le16(sizeof(data->rpt->fin))
		};
		ret = nct6694_read_msg(data->nct6694, &cmd_hd,
				       &data->rpt->fin);
		if (ret)
			return ret;

		*val = be16_to_cpu(data->rpt->fin);

		return 0;
	case hwmon_fan_min:
		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_HWMON_MOD,
			.cmd = NCT6694_HWMON_ALARM,
			.sel = NCT6694_HWMON_ALARM_SEL,
			.len = cpu_to_le16(sizeof(data->msg->hwmon_alarm))
		};
		ret = nct6694_read_msg(data->nct6694, &cmd_hd,
				       &data->msg->hwmon_alarm);
		if (ret)
			return ret;

		*val = be16_to_cpu(data->msg->hwmon_alarm.fin_ll[channel]);

		return 0;
	case hwmon_fan_min_alarm:
		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_RPT_MOD,
			.offset = cpu_to_le16(NCT6694_FIN_STS(channel / 8)),
			.len = cpu_to_le16(sizeof(data->rpt->status))
		};
		ret = nct6694_read_msg(data->nct6694, &cmd_hd,
				       &data->rpt->status);
		if (ret)
			return ret;

		*val = !!(data->rpt->status & BIT(channel % 8));

		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int nct6694_pwm_read(struct device *dev, u32 attr, int channel,
			    long *val)
{
	struct nct6694_hwmon_data *data = dev_get_drvdata(dev);
	struct nct6694_cmd_header cmd_hd;
	unsigned char pwm_en;
	int ret;

	guard(mutex)(&data->lock);

	switch (attr) {
	case hwmon_pwm_enable:
		pwm_en = data->hwmon_en.pwm_en[channel / 8];
		*val = !!(pwm_en & BIT(channel % 8));

		return 0;
	case hwmon_pwm_input:
		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_RPT_MOD,
			.offset = cpu_to_le16(NCT6694_PWM_IDX(channel)),
			.len = cpu_to_le16(sizeof(data->rpt->pwm))
		};
		ret = nct6694_read_msg(data->nct6694, &cmd_hd,
				       &data->rpt->pwm);
		if (ret)
			return ret;

		*val = data->rpt->pwm;

		return 0;
	case hwmon_pwm_freq:
		*val = NCT6694_FREQ_FROM_REG(data->hwmon_en.pwm_freq[channel]);

		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int nct6694_in_write(struct device *dev, u32 attr, int channel,
			    long val)
{
	struct nct6694_hwmon_data *data = dev_get_drvdata(dev);
	struct nct6694_cmd_header cmd_hd;
	int ret;

	guard(mutex)(&data->lock);

	switch (attr) {
	case hwmon_in_enable:
		if (val == 0)
			data->hwmon_en.vin_en[channel / 8] &= ~BIT(channel % 8);
		else if (val == 1)
			data->hwmon_en.vin_en[channel / 8] |= BIT(channel % 8);
		else
			return -EINVAL;

		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_HWMON_MOD,
			.cmd = NCT6694_HWMON_CONTROL,
			.sel = NCT6694_HWMON_CONTROL_SEL,
			.len = cpu_to_le16(sizeof(data->hwmon_en))
		};

		return nct6694_write_msg(data->nct6694, &cmd_hd,
					 &data->hwmon_en);
	case hwmon_in_max:
		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_HWMON_MOD,
			.cmd = NCT6694_HWMON_ALARM,
			.sel = NCT6694_HWMON_ALARM_SEL,
			.len = cpu_to_le16(sizeof(data->msg->hwmon_alarm))
		};
		ret = nct6694_read_msg(data->nct6694, &cmd_hd,
				       &data->msg->hwmon_alarm);
		if (ret)
			return ret;

		val = clamp_val(val, 0, 2032);
		data->msg->hwmon_alarm.vin_limit[channel].hl = in_to_reg(val);

		return nct6694_write_msg(data->nct6694, &cmd_hd,
					 &data->msg->hwmon_alarm);
	case hwmon_in_min:
		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_HWMON_MOD,
			.cmd = NCT6694_HWMON_ALARM,
			.sel = NCT6694_HWMON_ALARM_SEL,
			.len = cpu_to_le16(sizeof(data->msg->hwmon_alarm))
		};
		ret = nct6694_read_msg(data->nct6694, &cmd_hd,
				       &data->msg->hwmon_alarm);
		if (ret)
			return ret;

		val = clamp_val(val, 0, 2032);
		data->msg->hwmon_alarm.vin_limit[channel].ll = in_to_reg(val);

		return nct6694_write_msg(data->nct6694, &cmd_hd,
					 &data->msg->hwmon_alarm);
	default:
		return -EOPNOTSUPP;
	}
}

static int nct6694_temp_write(struct device *dev, u32 attr, int channel,
			      long val)
{
	struct nct6694_hwmon_data *data = dev_get_drvdata(dev);
	struct nct6694_cmd_header cmd_hd;
	unsigned char temp_hyst;
	signed char temp_max;
	int ret;

	guard(mutex)(&data->lock);

	switch (attr) {
	case hwmon_temp_enable:
		if (val == 0)
			data->hwmon_en.tin_en[channel / 8] &= ~BIT(channel % 8);
		else if (val == 1)
			data->hwmon_en.tin_en[channel / 8] |= BIT(channel % 8);
		else
			return -EINVAL;

		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_HWMON_MOD,
			.cmd = NCT6694_HWMON_CONTROL,
			.sel = NCT6694_HWMON_CONTROL_SEL,
			.len = cpu_to_le16(sizeof(data->hwmon_en))
		};

		return nct6694_write_msg(data->nct6694, &cmd_hd,
					 &data->hwmon_en);
	case hwmon_temp_max:
		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_HWMON_MOD,
			.cmd = NCT6694_HWMON_ALARM,
			.sel = NCT6694_HWMON_ALARM_SEL,
			.len = cpu_to_le16(sizeof(data->msg->hwmon_alarm))
		};
		ret = nct6694_read_msg(data->nct6694, &cmd_hd,
				       &data->msg->hwmon_alarm);
		if (ret)
			return ret;

		val = clamp_val(val, -127000, 127000);
		data->msg->hwmon_alarm.tin_cfg[channel].hl = temp_to_reg(val);

		return nct6694_write_msg(data->nct6694, &cmd_hd,
					 &data->msg->hwmon_alarm);
	case hwmon_temp_max_hyst:
		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_HWMON_MOD,
			.cmd = NCT6694_HWMON_ALARM,
			.sel = NCT6694_HWMON_ALARM_SEL,
			.len = cpu_to_le16(sizeof(data->msg->hwmon_alarm))
		};
		ret = nct6694_read_msg(data->nct6694, &cmd_hd,
				       &data->msg->hwmon_alarm);

		val = clamp_val(val, -127000, 127000);
		temp_max = data->msg->hwmon_alarm.tin_cfg[channel].hl;
		temp_hyst = temp_max - temp_to_reg(val);
		temp_hyst = clamp_val(temp_hyst, 0, 7);
		data->msg->hwmon_alarm.tin_cfg[channel].hyst =
			(data->msg->hwmon_alarm.tin_cfg[channel].hyst & ~NCT6694_TIN_HYST_MASK) |
			FIELD_PREP(NCT6694_TIN_HYST_MASK, temp_hyst);

		return nct6694_write_msg(data->nct6694, &cmd_hd,
					 &data->msg->hwmon_alarm);
	default:
		return -EOPNOTSUPP;
	}
}

static int nct6694_fan_write(struct device *dev, u32 attr, int channel,
			     long val)
{
	struct nct6694_hwmon_data *data = dev_get_drvdata(dev);
	struct nct6694_cmd_header cmd_hd;
	int ret;

	guard(mutex)(&data->lock);

	switch (attr) {
	case hwmon_fan_enable:
		if (val == 0)
			data->hwmon_en.fin_en[channel / 8] &= ~BIT(channel % 8);
		else if (val == 1)
			data->hwmon_en.fin_en[channel / 8] |= BIT(channel % 8);
		else
			return -EINVAL;

		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_HWMON_MOD,
			.cmd = NCT6694_HWMON_CONTROL,
			.sel = NCT6694_HWMON_CONTROL_SEL,
			.len = cpu_to_le16(sizeof(data->hwmon_en))
		};

		return nct6694_write_msg(data->nct6694, &cmd_hd,
					 &data->hwmon_en);
	case hwmon_fan_min:
		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_HWMON_MOD,
			.cmd = NCT6694_HWMON_ALARM,
			.sel = NCT6694_HWMON_ALARM_SEL,
			.len = cpu_to_le16(sizeof(data->msg->hwmon_alarm))
		};
		ret = nct6694_read_msg(data->nct6694, &cmd_hd,
				       &data->msg->hwmon_alarm);
		if (ret)
			return ret;

		val = clamp_val(val, 1, 65535);
		data->msg->hwmon_alarm.fin_ll[channel] = cpu_to_be16(val);

		return nct6694_write_msg(data->nct6694, &cmd_hd,
					 &data->msg->hwmon_alarm);
	default:
		return -EOPNOTSUPP;
	}
}

static int nct6694_pwm_write(struct device *dev, u32 attr, int channel,
			     long val)
{
	struct nct6694_hwmon_data *data = dev_get_drvdata(dev);
	struct nct6694_cmd_header cmd_hd;
	int ret;

	guard(mutex)(&data->lock);

	switch (attr) {
	case hwmon_pwm_enable:
		if (val == 0)
			data->hwmon_en.pwm_en[channel / 8] &= ~BIT(channel % 8);
		else if (val == 1)
			data->hwmon_en.pwm_en[channel / 8] |= BIT(channel % 8);
		else
			return -EINVAL;

		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_HWMON_MOD,
			.cmd = NCT6694_HWMON_CONTROL,
			.sel = NCT6694_HWMON_CONTROL_SEL,
			.len = cpu_to_le16(sizeof(data->hwmon_en))
		};

		return nct6694_write_msg(data->nct6694, &cmd_hd,
					 &data->hwmon_en);
	case hwmon_pwm_input:
		if (val < 0 || val > 255)
			return -EINVAL;

		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_PWM_MOD,
			.cmd = NCT6694_PWM_CONTROL,
			.sel = NCT6694_PWM_CONTROL_SEL,
			.len = cpu_to_le16(sizeof(data->msg->pwm_ctrl))
		};

		ret = nct6694_read_msg(data->nct6694, &cmd_hd,
				       &data->msg->pwm_ctrl);
		if (ret)
			return ret;

		data->msg->pwm_ctrl.mal_val[channel] = val;

		return nct6694_write_msg(data->nct6694, &cmd_hd,
					 &data->msg->pwm_ctrl);
	case hwmon_pwm_freq:
		cmd_hd = (struct nct6694_cmd_header) {
			.mod = NCT6694_HWMON_MOD,
			.cmd = NCT6694_HWMON_CONTROL,
			.sel = NCT6694_HWMON_CONTROL_SEL,
			.len = cpu_to_le16(sizeof(data->hwmon_en))
		};

		data->hwmon_en.pwm_freq[channel] = NCT6694_FREQ_TO_REG(val);

		return nct6694_write_msg(data->nct6694, &cmd_hd,
					 &data->hwmon_en);
	default:
		return -EOPNOTSUPP;
	}
}

static int nct6694_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_in:
		/* in mV */
		return nct6694_in_read(dev, attr, channel, val);
	case hwmon_temp:
		/* in mC */
		return nct6694_temp_read(dev, attr, channel, val);
	case hwmon_fan:
		/* in RPM */
		return nct6694_fan_read(dev, attr, channel, val);
	case hwmon_pwm:
		/* in value 0~255 */
		return nct6694_pwm_read(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int nct6694_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_in:
		return nct6694_in_write(dev, attr, channel, val);
	case hwmon_temp:
		return nct6694_temp_write(dev, attr, channel, val);
	case hwmon_fan:
		return nct6694_fan_write(dev, attr, channel, val);
	case hwmon_pwm:
		return nct6694_pwm_write(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t nct6694_is_visible(const void *data,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	switch (type) {
	case hwmon_in:
		switch (attr) {
		case hwmon_in_enable:
		case hwmon_in_max:
		case hwmon_in_min:
			return 0644;
		case hwmon_in_alarm:
		case hwmon_in_input:
			return 0444;
		default:
			return 0;
		}
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_enable:
		case hwmon_temp_max:
		case hwmon_temp_max_hyst:
			return 0644;
		case hwmon_temp_input:
		case hwmon_temp_max_alarm:
			return 0444;
		default:
			return 0;
		}
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_enable:
		case hwmon_fan_min:
			return 0644;
		case hwmon_fan_input:
		case hwmon_fan_min_alarm:
			return 0444;
		default:
			return 0;
		}
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_enable:
		case hwmon_pwm_freq:
		case hwmon_pwm_input:
			return 0644;
		default:
			return 0;
		}
	default:
		return 0;
	}
}

static const struct hwmon_ops nct6694_hwmon_ops = {
	.is_visible = nct6694_is_visible,
	.read = nct6694_read,
	.write = nct6694_write,
};

static const struct hwmon_chip_info nct6694_chip_info = {
	.ops = &nct6694_hwmon_ops,
	.info = nct6694_info,
};

static int nct6694_hwmon_init(struct nct6694_hwmon_data *data)
{
	struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_HWMON_MOD,
		.cmd = NCT6694_HWMON_CONTROL,
		.sel = NCT6694_HWMON_CONTROL_SEL,
		.len = cpu_to_le16(sizeof(data->hwmon_en))
	};
	int ret;

	/*
	 * Record each Hardware Monitor Channel enable status
	 * and PWM frequency register
	 */
	ret = nct6694_read_msg(data->nct6694, &cmd_hd,
			       &data->hwmon_en);
	if (ret)
		return ret;

	cmd_hd = (struct nct6694_cmd_header) {
		.mod = NCT6694_HWMON_MOD,
		.cmd = NCT6694_HWMON_ALARM,
		.sel = NCT6694_HWMON_ALARM_SEL,
		.len = cpu_to_le16(sizeof(data->msg->hwmon_alarm))
	};

	/* Select hwmon device alarm mode */
	ret = nct6694_read_msg(data->nct6694, &cmd_hd,
			       &data->msg->hwmon_alarm);
	if (ret)
		return ret;

	data->msg->hwmon_alarm.smi_ctrl = NCT6694_HWMON_REALTIME_IRQ;

	return nct6694_write_msg(data->nct6694, &cmd_hd,
				 &data->msg->hwmon_alarm);
}

static int nct6694_hwmon_probe(struct platform_device *pdev)
{
	struct nct6694_hwmon_data *data;
	struct nct6694 *nct6694 = dev_get_drvdata(pdev->dev.parent);
	struct device *hwmon_dev;
	int ret;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->rpt = devm_kzalloc(&pdev->dev, sizeof(union nct6694_hwmon_rpt),
				 GFP_KERNEL);
	if (!data->rpt)
		return -ENOMEM;

	data->msg = devm_kzalloc(&pdev->dev, sizeof(union nct6694_hwmon_msg),
				 GFP_KERNEL);
	if (!data->msg)
		return -ENOMEM;

	data->nct6694 = nct6694;
	ret = devm_mutex_init(&pdev->dev, &data->lock);
	if (ret)
		return ret;

	ret = nct6694_hwmon_init(data);
	if (ret)
		return ret;

	/* Register hwmon device to HWMON framework */
	hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev,
							 "nct6694", data,
							 &nct6694_chip_info,
							 NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static struct platform_driver nct6694_hwmon_driver = {
	.driver = {
		.name	= "nct6694-hwmon",
	},
	.probe		= nct6694_hwmon_probe,
};

module_platform_driver(nct6694_hwmon_driver);

MODULE_DESCRIPTION("USB-HWMON driver for NCT6694");
MODULE_AUTHOR("Ming Yu <tmyu0@nuvoton.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:nct6694-hwmon");
