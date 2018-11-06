/*
 * TI BQ257000 charger driver
 *
 * Copyright (C) 2016 Rockchip Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/power/bq25700-charge.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/usb/phy.h>
#include <linux/power/rk_usbbc.h>
#include <linux/extcon.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

static int dbg_enable;
module_param_named(dbg_level, dbg_enable, int, 0644);

#define DBG(args...) \
	do { \
		if (dbg_enable) { \
			pr_info(args); \
		} \
	} while (0)

#define bq25700_info(fmt, args...) pr_info("bq25700: "fmt, ##args)

#define BQ25700_MANUFACTURER		"Texas Instruments"
#define BQ25700_ID			0x59
#define BQ25703_ID			0x58

#define DEFAULT_INPUTVOL		((5000 - 1280) * 1000)
#define MAX_INPUTVOLTAGE		24000000
#define MAX_INPUTCURRENT		6350000
#define MAX_CHARGEVOLTAGE		16800000
#define MAX_CHARGECURRETNT		8128000
#define MAX_OTGVOLTAGE			20800000
#define MAX_OTGCURRENT			6350000

enum bq25700_fields {
	EN_LWPWR, WDTWR_ADJ, IDPM_AUTO_DISABLE,
	EN_OOA, PWM_FREQ, EN_LEARN, IADP_GAIN, IBAT_GAIN,
	EN_LDO, EN_IDPM, CHRG_INHIBIT,/*reg12h*/
	CHARGE_CURRENT,/*reg14h*/
	MAX_CHARGE_VOLTAGE,/*reg15h*/

	AC_STAT, ICO_DONE, IN_VINDPM, IN_IINDPM, IN_FCHRG, IN_PCHRG, IN_OTG,
	F_ACOV, F_BATOC, F_ACOC, SYSOVP_STAT, F_LATCHOFF, F_OTG_OVP, F_OTG_OCP,
	/*reg20h*/
	STAT_COMP, STAT_ICRIT, STAT_INOM, STAT_IDCHG, STAT_VSYS, STAT_BAT_REMOV,
	STAT_ADP_REMOV,/*reg21h*/
	INPUT_CURRENT_DPM,/*reg22h*/
	OUTPUT_INPUT_VOL, OUTPUT_SYS_POWER,/*reg23h*/
	OUTPUT_DSG_CUR,	OUTPUT_CHG_CUR,/*reg24h*/
	OUTPUT_INPUT_CUR, OUTPUT_CMPIN_VOL,/*reg25h*/
	OUTPUT_SYS_VOL, OUTPUT_BAT_VOL,/*reg26h*/

	EN_IBAT, EN_PROCHOT_LPWR, EN_PSYS, RSNS_RAC, RSNS_RSR,
	PSYS_RATIO, CMP_REF,	CMP_POL, CMP_DEG, FORCE_LATCHOFF,
	EN_SHIP_DCHG, AUTO_WAKEUP_EN, /*reg30h*/
	PKPWR_TOVLD_REG, EN_PKPWR_IDPM, EN_PKPWR_VSYS, PKPWER_OVLD_STAT,
	PKPWR_RELAX_STAT, PKPWER_TMAX,	EN_EXTILIM, EN_ICHG_IDCHG, Q2_OCP,
	ACX_OCP, EN_ACOC, ACOC_VTH, EN_BATOC, BATCOC_VTH,/*reg31h*/
	EN_HIZ, RESET_REG, RESET_VINDPM, EN_OTG, EN_ICO_MODE, BATFETOFF_HIZ,
	PSYS_OTG_IDCHG,/*reg32h*/
	ILIM2_VTH, ICRIT_DEG, VSYS_VTH, EN_PROCHOT_EXT, PROCHOT_WIDTH,
	PROCHOT_CLEAR, INOM_DEG,/*reg33h*/
	IDCHG_VTH, IDCHG_DEG, PROCHOT_PROFILE_COMP, PROCHOT_PROFILE_ICRIT,
	PROCHOT_PROFILE_INOM, PROCHOT_PROFILE_IDCHG,
	PROCHOT_PROFILE_VSYS, PROCHOT_PROFILE_BATPRES, PROCHOT_PROFILE_ACOK,
	/*reg34h*/
	ADC_CONV, ADC_START, ADC_FULLSCALE, EN_ADC_CMPIN, EN_ADC_VBUS,
	EN_ADC_PSYS, EN_ADC_IIN, EN_ADC_IDCHG, EN_ADC_ICHG, EN_ADC_VSYS,
	EN_ADC_VBAT,/*reg35h*/

	OTG_VOLTAGE,/*reg3bh*/
	OTG_CURRENT,/*reg3ch*/
	INPUT_VOLTAGE,/*reg3dh*/
	MIN_SYS_VOTAGE,/*reg3eh*/
	INPUT_CURRENT,/*reg3fh*/

	MANUFACTURE_ID,/*regfeh*/
	DEVICE_ID,/*regffh*/

	F_MAX_FIELDS
};

enum charger_t {
	USB_TYPE_UNKNOWN_CHARGER,
	USB_TYPE_NONE_CHARGER,
	USB_TYPE_USB_CHARGER,
	USB_TYPE_AC_CHARGER,
	USB_TYPE_CDP_CHARGER,
	DC_TYPE_DC_CHARGER,
	DC_TYPE_NONE_CHARGER,
};

enum usb_status_t {
	USB_STATUS_NONE,
	USB_STATUS_USB,
	USB_STATUS_AC,
	USB_STATUS_PD,
	USB_STATUS_OTG,
};

enum tpyec_port_t {
	USB_TYPEC_0,
	USB_TYPEC_1,
};

/* initial field values, converted to register values */
struct bq25700_init_data {
	u32 ichg;	/* charge current		*/
	u32 max_chg_vol;	/*max charge voltage*/
	u32 input_voltage;	/*input voltage*/
	u32 input_current;	/*input current*/
	u32 input_current_sdp;
	u32 input_current_dcp;
	u32 input_current_cdp;
	u32 sys_min_voltage;	/*mininum system voltage*/
	u32 otg_voltage;	/*OTG voltage*/
	u32 otg_current;	/*OTG current*/
};

struct bq25700_state {
	u8 ac_stat;
	u8 ico_done;
	u8 in_vindpm;
	u8 in_iindpm;
	u8 in_fchrg;
	u8 in_pchrg;
	u8 in_otg;
	u8 fault_acov;
	u8 fault_batoc;
	u8 fault_acoc;
	u8 sysovp_stat;
	u8 fault_latchoff;
	u8 fault_otg_ovp;
	u8 fault_otg_ocp;
};

struct bq25700_device {
	struct i2c_client			*client;
	struct device				*dev;
	struct power_supply			*supply_charger;
	char				model_name[I2C_NAME_SIZE];
	unsigned int			irq;
	bool				first_time;
	bool				charger_health_valid;
	bool				battery_health_valid;
	bool				battery_status_valid;

	struct workqueue_struct		*usb_charger_wq;
	struct workqueue_struct		*dc_charger_wq;
	struct workqueue_struct		*finish_sig_wq;
	struct delayed_work		usb_work;
	struct delayed_work		pd_work;
	struct delayed_work		host_work;
	struct delayed_work		discnt_work;
	struct delayed_work		usb_work1;
	struct delayed_work		pd_work1;
	struct delayed_work		host_work1;
	struct delayed_work		discnt_work1;
	struct delayed_work		irq_work;
	struct notifier_block		cable_cg_nb;
	struct notifier_block		cable_pd_nb;
	struct notifier_block		cable_host_nb;
	struct notifier_block		cable_discnt_nb;
	struct notifier_block		cable_cg_nb1;
	struct notifier_block		cable_pd_nb1;
	struct notifier_block		cable_host_nb1;
	struct notifier_block		cable_discnt_nb1;
	struct extcon_dev		*cable_edev;
	struct extcon_dev		*cable_edev_1;
	int				typec0_status;
	int				typec1_status;
	struct gpio_desc		*typec0_enable_io;
	struct gpio_desc		*typec1_enable_io;
	struct gpio_desc		*typec0_discharge_io;
	struct gpio_desc		*typec1_discharge_io;
	struct gpio_desc		*otg_mode_en_io;

	struct regmap			*regmap;
	struct regmap_field		*rmap_fields[F_MAX_FIELDS];
	int				chip_id;
	struct bq25700_init_data	init_data;
	struct bq25700_state		state;
	int				pd_charge_only;
	unsigned int			bc_event;
	bool				usb_bc;
};

static const struct reg_field bq25700_reg_fields[] = {
	/*REG12*/
	[EN_LWPWR] = REG_FIELD(0x12, 15, 15),
	[WDTWR_ADJ] = REG_FIELD(0x12, 13, 14),
	[IDPM_AUTO_DISABLE] = REG_FIELD(0x12, 12, 12),
	[EN_OOA] = REG_FIELD(0x12, 10, 10),
	[PWM_FREQ] = REG_FIELD(0x12, 9, 9),
	[EN_LEARN] = REG_FIELD(0x12, 5, 5),
	[IADP_GAIN] = REG_FIELD(0x12, 4, 4),
	[IBAT_GAIN] = REG_FIELD(0x12, 3, 3),
	[EN_LDO] = REG_FIELD(0x12, 2, 2),
	[EN_IDPM] = REG_FIELD(0x12, 1, 1),
	[CHRG_INHIBIT] = REG_FIELD(0x12, 0, 0),
	/*REG0x14*/
	[CHARGE_CURRENT] = REG_FIELD(0x14, 6, 12),
	/*REG0x15*/
	[MAX_CHARGE_VOLTAGE] = REG_FIELD(0x15, 4, 14),
	/*REG20*/
	[AC_STAT] = REG_FIELD(0x20, 15, 15),
	[ICO_DONE] = REG_FIELD(0x20, 14, 14),
	[IN_VINDPM] = REG_FIELD(0x20, 12, 12),
	[IN_IINDPM] = REG_FIELD(0x20, 11, 11),
	[IN_FCHRG] = REG_FIELD(0x20, 10, 10),
	[IN_PCHRG] = REG_FIELD(0x20, 9, 9),
	[IN_OTG] = REG_FIELD(0x20, 8, 8),
	[F_ACOV] = REG_FIELD(0x20, 7, 7),
	[F_BATOC] = REG_FIELD(0x20, 6, 6),
	[F_ACOC] = REG_FIELD(0x20, 5, 5),
	[SYSOVP_STAT] = REG_FIELD(0x20, 4, 4),
	[F_LATCHOFF] = REG_FIELD(0x20, 2, 2),
	[F_OTG_OVP] = REG_FIELD(0x20, 1, 1),
	[F_OTG_OCP] = REG_FIELD(0x20, 0, 0),
	/*REG21*/
	[STAT_COMP] = REG_FIELD(0x21, 6, 6),
	[STAT_ICRIT] = REG_FIELD(0x21, 5, 5),
	[STAT_INOM] = REG_FIELD(0x21, 4, 4),
	[STAT_IDCHG] = REG_FIELD(0x21, 3, 3),
	[STAT_VSYS] = REG_FIELD(0x21, 2, 2),
	[STAT_BAT_REMOV] = REG_FIELD(0x21, 1, 1),
	[STAT_ADP_REMOV] = REG_FIELD(0x21, 0, 0),
	/*REG22*/
	[INPUT_CURRENT_DPM] = REG_FIELD(0x22, 8, 14),
	/*REG23H*/
	[OUTPUT_INPUT_VOL] = REG_FIELD(0x23, 8, 15),
	[OUTPUT_SYS_POWER] = REG_FIELD(0x23, 0, 7),
	/*REG24H*/
	[OUTPUT_DSG_CUR] = REG_FIELD(0x24, 8, 14),
	[OUTPUT_CHG_CUR] = REG_FIELD(0x24, 0, 6),
	/*REG25H*/
	[OUTPUT_INPUT_CUR] = REG_FIELD(0x25, 8, 15),
	[OUTPUT_CMPIN_VOL] = REG_FIELD(0x25, 0, 7),
	/*REG26H*/
	[OUTPUT_SYS_VOL] = REG_FIELD(0x26, 8, 15),
	[OUTPUT_BAT_VOL] = REG_FIELD(0x26, 0, 6),

	/*REG30*/
	[EN_IBAT] = REG_FIELD(0x30, 15, 15),
	[EN_PROCHOT_LPWR] = REG_FIELD(0x30, 13, 14),
	[EN_PSYS] = REG_FIELD(0x30, 12, 12),
	[RSNS_RAC] = REG_FIELD(0x30, 11, 11),
	[RSNS_RSR] = REG_FIELD(0x30, 10, 10),
	[PSYS_RATIO] = REG_FIELD(0x30, 9, 9),
	[CMP_REF] = REG_FIELD(0x30, 7, 7),
	[CMP_POL] = REG_FIELD(0x30, 6, 6),
	[CMP_DEG] = REG_FIELD(0x30, 4, 5),
	[FORCE_LATCHOFF] = REG_FIELD(0x30, 3, 3),
	[EN_SHIP_DCHG] = REG_FIELD(0x30, 1, 1),
	[AUTO_WAKEUP_EN] = REG_FIELD(0x30, 0, 0),
	/*REG31*/
	[PKPWR_TOVLD_REG] = REG_FIELD(0x31, 14, 15),
	[EN_PKPWR_IDPM] = REG_FIELD(0x31, 13, 13),
	[EN_PKPWR_VSYS] = REG_FIELD(0x31, 12, 12),
	[PKPWER_OVLD_STAT] = REG_FIELD(0x31, 11, 11),
	[PKPWR_RELAX_STAT] = REG_FIELD(0x31, 10, 10),
	[PKPWER_TMAX] = REG_FIELD(0x31, 8, 9),
	[EN_EXTILIM] = REG_FIELD(0x31, 7, 7),
	[EN_ICHG_IDCHG] = REG_FIELD(0x31, 6, 6),
	[Q2_OCP] = REG_FIELD(0x31, 5, 5),
	[ACX_OCP] = REG_FIELD(0x31, 4, 4),
	[EN_ACOC] = REG_FIELD(0x31, 3, 3),
	[ACOC_VTH] = REG_FIELD(0x31, 2, 2),
	[EN_BATOC] = REG_FIELD(0x31, 1, 1),
	[BATCOC_VTH] = REG_FIELD(0x31, 0, 0),
	/*REG32*/
	[EN_HIZ] = REG_FIELD(0x32, 15, 15),
	[RESET_REG] = REG_FIELD(0x32, 14, 14),
	[RESET_VINDPM] = REG_FIELD(0x32, 13, 13),
	[EN_OTG] = REG_FIELD(0x32, 12, 12),
	[EN_ICO_MODE] = REG_FIELD(0x32, 11, 11),
	[BATFETOFF_HIZ] = REG_FIELD(0x32, 1, 1),
	[PSYS_OTG_IDCHG] = REG_FIELD(0x32, 0, 0),
	/*REG33*/
	[ILIM2_VTH] = REG_FIELD(0x33, 11, 15),
	[ICRIT_DEG] = REG_FIELD(0x33, 9, 10),
	[VSYS_VTH] = REG_FIELD(0x33, 6, 7),
	[EN_PROCHOT_EXT] = REG_FIELD(0x33, 5, 5),
	[PROCHOT_WIDTH] = REG_FIELD(0x33, 3, 4),
	[PROCHOT_CLEAR] = REG_FIELD(0x33, 2, 2),
	[INOM_DEG] = REG_FIELD(0x33, 1, 1),
	/*REG34*/
	[IDCHG_VTH] = REG_FIELD(0x34, 10, 15),
	[IDCHG_DEG] = REG_FIELD(0x34, 8, 9),
	[PROCHOT_PROFILE_COMP] = REG_FIELD(0x34, 6, 6),
	[PROCHOT_PROFILE_ICRIT] = REG_FIELD(0x34, 5, 5),
	[PROCHOT_PROFILE_INOM] = REG_FIELD(0x34, 4, 4),
	[PROCHOT_PROFILE_IDCHG] = REG_FIELD(0x34, 3, 3),
	[PROCHOT_PROFILE_VSYS] = REG_FIELD(0x34, 2, 2),
	[PROCHOT_PROFILE_BATPRES] = REG_FIELD(0x34, 1, 1),
	[PROCHOT_PROFILE_ACOK] = REG_FIELD(0x34, 0, 0),
	/*REG35*/
	[ADC_CONV] = REG_FIELD(0x35, 15, 15),
	[ADC_START] = REG_FIELD(0x35, 14, 14),
	[ADC_FULLSCALE] = REG_FIELD(0x35, 13, 13),
	[EN_ADC_CMPIN] = REG_FIELD(0x35, 7, 7),
	[EN_ADC_VBUS] = REG_FIELD(0x35, 6, 6),
	[EN_ADC_PSYS] = REG_FIELD(0x35, 5, 5),
	[EN_ADC_IIN] = REG_FIELD(0x35, 4, 4),
	[EN_ADC_IDCHG] = REG_FIELD(0x35, 3, 3),
	[EN_ADC_ICHG] = REG_FIELD(0x35, 2, 2),
	[EN_ADC_VSYS] = REG_FIELD(0x35, 1, 1),
	[EN_ADC_VBAT] = REG_FIELD(0x35, 0, 0),
	/*REG3B*/
	[OTG_VOLTAGE] = REG_FIELD(0x3B, 6, 13),
	/*REG3C*/
	[OTG_CURRENT] = REG_FIELD(0x3C, 8, 14),
	/*REG3D*/
	[INPUT_VOLTAGE] = REG_FIELD(0x3D, 6, 13),
	/*REG3E*/
	[MIN_SYS_VOTAGE] = REG_FIELD(0x3E, 8, 13),
	/*REG3F*/
	[INPUT_CURRENT] = REG_FIELD(0x3F, 8, 14),

	/*REGFE*/
	[MANUFACTURE_ID] = REG_FIELD(0xFE, 0, 7),
	/*REFFF*/
	[DEVICE_ID] = REG_FIELD(0xFF, 0, 7),
};

static const struct reg_field bq25703_reg_fields[] = {
	/*REG00*/
	[EN_LWPWR] = REG_FIELD(0x00, 15, 15),
	[WDTWR_ADJ] = REG_FIELD(0x00, 13, 14),
	[IDPM_AUTO_DISABLE] = REG_FIELD(0x00, 12, 12),
	[EN_OOA] = REG_FIELD(0x00, 10, 10),
	[PWM_FREQ] = REG_FIELD(0x00, 9, 9),
	[EN_LEARN] = REG_FIELD(0x00, 5, 5),
	[IADP_GAIN] = REG_FIELD(0x00, 4, 4),
	[IBAT_GAIN] = REG_FIELD(0x00, 3, 3),
	[EN_LDO] = REG_FIELD(0x00, 2, 2),
	[EN_IDPM] = REG_FIELD(0x00, 1, 1),
	[CHRG_INHIBIT] = REG_FIELD(0x00, 0, 0),
	/*REG0x02*/
	[CHARGE_CURRENT] = REG_FIELD(0x02, 6, 12),
	/*REG0x04*/
	[MAX_CHARGE_VOLTAGE] = REG_FIELD(0x04, 4, 14),
	/*REG20*/
	[AC_STAT] = REG_FIELD(0x20, 15, 15),
	[ICO_DONE] = REG_FIELD(0x20, 14, 14),
	[IN_VINDPM] = REG_FIELD(0x20, 12, 12),
	[IN_IINDPM] = REG_FIELD(0x20, 11, 11),
	[IN_FCHRG] = REG_FIELD(0x20, 10, 10),
	[IN_PCHRG] = REG_FIELD(0x20, 9, 9),
	[IN_OTG] = REG_FIELD(0x20, 8, 8),
	[F_ACOV] = REG_FIELD(0x20, 7, 7),
	[F_BATOC] = REG_FIELD(0x20, 6, 6),
	[F_ACOC] = REG_FIELD(0x20, 5, 5),
	[SYSOVP_STAT] = REG_FIELD(0x20, 4, 4),
	[F_LATCHOFF] = REG_FIELD(0x20, 2, 2),
	[F_OTG_OVP] = REG_FIELD(0x20, 1, 1),
	[F_OTG_OCP] = REG_FIELD(0x20, 0, 0),
	/*REG22*/
	[STAT_COMP] = REG_FIELD(0x22, 6, 6),
	[STAT_ICRIT] = REG_FIELD(0x22, 5, 5),
	[STAT_INOM] = REG_FIELD(0x22, 4, 4),
	[STAT_IDCHG] = REG_FIELD(0x22, 3, 3),
	[STAT_VSYS] = REG_FIELD(0x22, 2, 2),
	[STAT_BAT_REMOV] = REG_FIELD(0x22, 1, 1),
	[STAT_ADP_REMOV] = REG_FIELD(0x22, 0, 0),
	/*REG24*/
	[INPUT_CURRENT_DPM] = REG_FIELD(0x24, 8, 14),

	/*REG26H*/
	[OUTPUT_INPUT_VOL] = REG_FIELD(0x26, 8, 15),
	[OUTPUT_SYS_POWER] = REG_FIELD(0x26, 0, 7),
	/*REG28H*/
	[OUTPUT_DSG_CUR] = REG_FIELD(0x28, 8, 14),
	[OUTPUT_CHG_CUR] = REG_FIELD(0x28, 0, 6),
	/*REG2aH*/
	[OUTPUT_INPUT_CUR] = REG_FIELD(0x2a, 8, 15),
	[OUTPUT_CMPIN_VOL] = REG_FIELD(0x2a, 0, 7),
	/*REG2cH*/
	[OUTPUT_SYS_VOL] = REG_FIELD(0x2c, 8, 15),
	[OUTPUT_BAT_VOL] = REG_FIELD(0x2c, 0, 6),

	/*REG30*/
	[EN_IBAT] = REG_FIELD(0x30, 15, 15),
	[EN_PROCHOT_LPWR] = REG_FIELD(0x30, 13, 14),
	[EN_PSYS] = REG_FIELD(0x30, 12, 12),
	[RSNS_RAC] = REG_FIELD(0x30, 11, 11),
	[RSNS_RSR] = REG_FIELD(0x30, 10, 10),
	[PSYS_RATIO] = REG_FIELD(0x30, 9, 9),
	[CMP_REF] = REG_FIELD(0x30, 7, 7),
	[CMP_POL] = REG_FIELD(0x30, 6, 6),
	[CMP_DEG] = REG_FIELD(0x30, 4, 5),
	[FORCE_LATCHOFF] = REG_FIELD(0x30, 3, 3),
	[EN_SHIP_DCHG] = REG_FIELD(0x30, 1, 1),
	[AUTO_WAKEUP_EN] = REG_FIELD(0x30, 0, 0),
	/*REG32*/
	[PKPWR_TOVLD_REG] = REG_FIELD(0x32, 14, 15),
	[EN_PKPWR_IDPM] = REG_FIELD(0x32, 13, 13),
	[EN_PKPWR_VSYS] = REG_FIELD(0x32, 12, 12),
	[PKPWER_OVLD_STAT] = REG_FIELD(0x32, 11, 11),
	[PKPWR_RELAX_STAT] = REG_FIELD(0x32, 10, 10),
	[PKPWER_TMAX] = REG_FIELD(0x32, 8, 9),
	[EN_EXTILIM] = REG_FIELD(0x32, 7, 7),
	[EN_ICHG_IDCHG] = REG_FIELD(0x32, 6, 6),
	[Q2_OCP] = REG_FIELD(0x32, 5, 5),
	[ACX_OCP] = REG_FIELD(0x32, 4, 4),
	[EN_ACOC] = REG_FIELD(0x32, 3, 3),
	[ACOC_VTH] = REG_FIELD(0x32, 2, 2),
	[EN_BATOC] = REG_FIELD(0x32, 1, 1),
	[BATCOC_VTH] = REG_FIELD(0x32, 0, 0),
	/*REG34*/
	[EN_HIZ] = REG_FIELD(0x34, 15, 15),
	[RESET_REG] = REG_FIELD(0x34, 14, 14),
	[RESET_VINDPM] = REG_FIELD(0x34, 13, 13),
	[EN_OTG] = REG_FIELD(0x34, 12, 12),
	[EN_ICO_MODE] = REG_FIELD(0x34, 11, 11),
	[BATFETOFF_HIZ] = REG_FIELD(0x34, 1, 1),
	[PSYS_OTG_IDCHG] = REG_FIELD(0x34, 0, 0),
	/*REG36*/
	[ILIM2_VTH] = REG_FIELD(0x36, 11, 15),
	[ICRIT_DEG] = REG_FIELD(0x36, 9, 10),
	[VSYS_VTH] = REG_FIELD(0x36, 6, 7),
	[EN_PROCHOT_EXT] = REG_FIELD(0x36, 5, 5),
	[PROCHOT_WIDTH] = REG_FIELD(0x36, 3, 4),
	[PROCHOT_CLEAR] = REG_FIELD(0x36, 2, 2),
	[INOM_DEG] = REG_FIELD(0x36, 1, 1),
	/*REG38*/
	[IDCHG_VTH] = REG_FIELD(0x38, 10, 15),
	[IDCHG_DEG] = REG_FIELD(0x38, 8, 9),
	[PROCHOT_PROFILE_COMP] = REG_FIELD(0x38, 6, 6),
	[PROCHOT_PROFILE_ICRIT] = REG_FIELD(0x38, 5, 5),
	[PROCHOT_PROFILE_INOM] = REG_FIELD(0x38, 4, 4),
	[PROCHOT_PROFILE_IDCHG] = REG_FIELD(0x38, 3, 3),
	[PROCHOT_PROFILE_VSYS] = REG_FIELD(0x38, 2, 2),
	[PROCHOT_PROFILE_BATPRES] = REG_FIELD(0x38, 1, 1),
	[PROCHOT_PROFILE_ACOK] = REG_FIELD(0x38, 0, 0),
	/*REG3a*/
	[ADC_CONV] = REG_FIELD(0x3a, 15, 15),
	[ADC_START] = REG_FIELD(0x3a, 14, 14),
	[ADC_FULLSCALE] = REG_FIELD(0x3a, 13, 13),
	[EN_ADC_CMPIN] = REG_FIELD(0x3a, 7, 7),
	[EN_ADC_VBUS] = REG_FIELD(0x3a, 6, 6),
	[EN_ADC_PSYS] = REG_FIELD(0x3a, 5, 5),
	[EN_ADC_IIN] = REG_FIELD(0x3a, 4, 4),
	[EN_ADC_IDCHG] = REG_FIELD(0x3a, 3, 3),
	[EN_ADC_ICHG] = REG_FIELD(0x3a, 2, 2),
	[EN_ADC_VSYS] = REG_FIELD(0x3a, 1, 1),
	[EN_ADC_VBAT] = REG_FIELD(0x3a, 0, 0),

	/*REG06*/
	[OTG_VOLTAGE] = REG_FIELD(0x06, 6, 13),
	/*REG08*/
	[OTG_CURRENT] = REG_FIELD(0x08, 8, 14),
	/*REG0a*/
	[INPUT_VOLTAGE] = REG_FIELD(0x0a, 6, 13),
	/*REG0C*/
	[MIN_SYS_VOTAGE] = REG_FIELD(0x0c, 8, 13),
	/*REG0e*/
	[INPUT_CURRENT] = REG_FIELD(0x0e, 8, 14),

	/*REG2E*/
	[MANUFACTURE_ID] = REG_FIELD(0x2E, 0, 7),
	/*REF2F*/
	[DEVICE_ID] = REG_FIELD(0x2F, 0, 7),
};

/*
 * Most of the val -> idx conversions can be computed, given the minimum,
 * maximum and the step between values. For the rest of conversions, we use
 * lookup tables.
 */
enum bq25700_table_ids {
	/* range tables */
	TBL_ICHG,
	TBL_CHGMAX,
	TBL_INPUTVOL,
	TBL_INPUTCUR,
	TBL_SYSVMIN,
	TBL_OTGVOL,
	TBL_OTGCUR,
	TBL_EXTCON,
};

struct bq25700_range {
	u32 min;
	u32 max;
	u32 step;
};

struct bq25700_lookup {
	const u32 *tbl;
	u32 size;
};

static const union {
	struct bq25700_range  rt;
	struct bq25700_lookup lt;
} bq25700_tables[] = {
	/* range tables */
	[TBL_ICHG] =	{ .rt = {0,	  8128000, 64000} },
	/* uV */
	[TBL_CHGMAX] = { .rt = {0, 19200000, 16000} },
	/* uV  max charge voltage*/
	[TBL_INPUTVOL] = { .rt = {3200000, 19520000, 64000} },
	/* uV  input charge voltage*/
	[TBL_INPUTCUR] = {.rt = {0, 6350000, 50000} },
	/*uA input current*/
	[TBL_SYSVMIN] = { .rt = {1024000, 16182000, 256000} },
	/* uV min system voltage*/
	[TBL_OTGVOL] = {.rt = {4480000, 20800000, 64000} },
	/*uV OTG volage*/
	[TBL_OTGCUR] = {.rt = {0, 6350000, 50000} },
};

static const struct regmap_range bq25700_readonly_reg_ranges[] = {
	regmap_reg_range(0x20, 0x26),
	regmap_reg_range(0xFE, 0xFF),
};

static const struct regmap_access_table bq25700_writeable_regs = {
	.no_ranges = bq25700_readonly_reg_ranges,
	.n_no_ranges = ARRAY_SIZE(bq25700_readonly_reg_ranges),
};

static const struct regmap_range bq25700_volatile_reg_ranges[] = {
	regmap_reg_range(0x12, 0x12),
	regmap_reg_range(0x14, 0x15),
	regmap_reg_range(0x20, 0x26),
	regmap_reg_range(0x30, 0x35),
	regmap_reg_range(0x3B, 0x3F),
	regmap_reg_range(0xFE, 0xFF),
};

static const struct regmap_access_table bq25700_volatile_regs = {
	.yes_ranges = bq25700_volatile_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(bq25700_volatile_reg_ranges),
};

static const struct regmap_config bq25700_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,

	.max_register = 0xFF,
	.cache_type = REGCACHE_RBTREE,

	.wr_table = &bq25700_writeable_regs,
	.volatile_table = &bq25700_volatile_regs,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};

static const struct regmap_range bq25703_readonly_reg_ranges[] = {
	regmap_reg_range(0x20, 0x2F),
};

static const struct regmap_access_table bq25703_writeable_regs = {
	.no_ranges = bq25703_readonly_reg_ranges,
	.n_no_ranges = ARRAY_SIZE(bq25703_readonly_reg_ranges),
};

static const struct regmap_range bq25703_volatile_reg_ranges[] = {
	regmap_reg_range(0x00, 0x0F),
	regmap_reg_range(0x20, 0x3B),
};

static const struct regmap_access_table bq25703_volatile_regs = {
	.yes_ranges = bq25703_volatile_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(bq25703_volatile_reg_ranges),
};

static const struct regmap_config bq25703_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,

	.max_register = 0x3B,
	.cache_type = REGCACHE_RBTREE,

	.wr_table = &bq25703_writeable_regs,
	.volatile_table = &bq25703_volatile_regs,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};

static void bq25700_disable_charge(struct bq25700_device *charger);

static struct bq25700_device *bq25700_charger;

static int bq25700_field_read(struct bq25700_device *charger,
			      enum bq25700_fields field_id)
{
	int ret;
	int val;

	ret = regmap_field_read(charger->rmap_fields[field_id], &val);
	if (ret < 0)
		return ret;

	return val;
}

static int bq25700_field_write(struct bq25700_device *charger,
			       enum bq25700_fields field_id, unsigned int val)
{
	return regmap_field_write(charger->rmap_fields[field_id], val);
}

static int bq25700_get_chip_state(struct bq25700_device *charger,
				  struct bq25700_state *state)
{
	int i, ret;

	struct {
		enum bq25700_fields id;
		u8 *data;
	} state_fields[] = {
		{AC_STAT,	&state->ac_stat},
		{ICO_DONE,	&state->ico_done},
		{IN_VINDPM,	&state->in_vindpm},
		{IN_IINDPM, &state->in_iindpm},
		{IN_FCHRG,	&state->in_fchrg},
		{IN_PCHRG,	&state->in_pchrg},
		{IN_OTG,	&state->in_otg},
		{F_ACOV,	&state->fault_acov},
		{F_BATOC,	&state->fault_batoc},
		{F_ACOC,	&state->fault_acoc},
		{SYSOVP_STAT,	&state->sysovp_stat},
		{F_LATCHOFF,	&state->fault_latchoff},
		{F_OTG_OVP,	&state->fault_otg_ovp},
		{F_OTG_OCP,	&state->fault_otg_ocp},
	};

	for (i = 0; i < ARRAY_SIZE(state_fields); i++) {
		ret = bq25700_field_read(charger, state_fields[i].id);
		if (ret < 0)
			return ret;

		*state_fields[i].data = ret;
	}

	return 0;
}

static int bq25700_dump_regs(struct bq25700_device *charger)
{
	u32 val = 0;
	struct bq25700_state state;
	int ret = 0;

	ret = bq25700_field_write(charger, ADC_START, 1);
	if (ret < 0) {
		DBG("error: ADC_START\n");
		return ret;
	}

	DBG("\n==================================\n");
	regmap_read(charger->regmap, 0x12, &val);
	DBG("REG0x12 : 0x%x\n", val);
	regmap_read(charger->regmap, 0x14, &val);
	DBG("REG0x14 : 0x%x\n", val);
	regmap_read(charger->regmap, 0x15, &val);
	DBG("REG0x15 : 0x%x\n", val);
	regmap_read(charger->regmap, 0x30, &val);
	DBG("REG0x30 : 0x%x\n", val);
	regmap_read(charger->regmap, 0x31, &val);
	DBG("REG0x31 : 0x%x\n", val);
	regmap_read(charger->regmap, 0x32, &val);
	DBG("REG0x32 : 0x%x\n", val);
	regmap_read(charger->regmap, 0x33, &val);
	DBG("REG0x33 : 0x%x\n", val);
	regmap_read(charger->regmap, 0x34, &val);
	DBG("REG0x34 : 0x%x\n", val);
	regmap_read(charger->regmap, 0x35, &val);
	DBG("REG0x35 : 0x%x\n", val);
	regmap_read(charger->regmap, 0x20, &val);
	DBG("REG0x20 : 0x%x\n", val);
	regmap_read(charger->regmap, 0x21, &val);
	DBG("REG0x21 : 0x%x\n", val);
	regmap_read(charger->regmap, 0x22, &val);
	DBG("REG0x22 : 0x%x\n", val);
	regmap_read(charger->regmap, 0x23, &val);
	DBG("REG0x23 : 0x%x\n", val);
	regmap_read(charger->regmap, 0x24, &val);
	DBG("REG0x24 : 0x%x\n", val);
	regmap_read(charger->regmap, 0x25, &val);
	DBG("REG0x25 : 0x%x\n", val);
	regmap_read(charger->regmap, 0x26, &val);
	DBG("REG0x26 : 0x%x\n", val);
	regmap_read(charger->regmap, 0x3b, &val);
	DBG("REG0x3b : 0x%x\n", val);
	regmap_read(charger->regmap, 0x3c, &val);
	DBG("REG0x3c : 0x%x\n", val);
	regmap_read(charger->regmap, 0x3d, &val);
	DBG("REG0x3d : 0x%x\n", val);
	regmap_read(charger->regmap, 0x3e, &val);
	DBG("REG0x3e : 0x%x\n", val);
	regmap_read(charger->regmap, 0x3f, &val);
	DBG("REG0x3f : 0x%x\n", val);
	regmap_read(charger->regmap, 0xfe, &val);
	DBG("REG0xfe : 0x%x\n", val);
	regmap_read(charger->regmap, 0xff, &val);
	DBG("REG0xff : 0x%x\n", val);

	DBG("battery charge current: %dmA\n",
	    bq25700_field_read(charger, OUTPUT_DSG_CUR) * 64);
	DBG("battery discharge current: %dmA\n",
	    bq25700_field_read(charger, OUTPUT_CHG_CUR) * 256);
	DBG("VSYS volatge: %dmV\n",
	    2880 + bq25700_field_read(charger, OUTPUT_SYS_VOL) * 64);
	DBG("BAT volatge: %dmV\n",
	    2880 + bq25700_field_read(charger, OUTPUT_BAT_VOL) * 64);

	DBG("SET CHARGE_CURRENT: %dmA\n",
	    bq25700_field_read(charger, CHARGE_CURRENT) * 64);
	DBG("MAX_CHARGE_VOLTAGE: %dmV\n",
	    bq25700_field_read(charger, MAX_CHARGE_VOLTAGE) * 16);
	DBG("	  INPUT_VOLTAGE: %dmV\n",
	    3200 + bq25700_field_read(charger, INPUT_VOLTAGE) * 64);
	DBG("	  INPUT_CURRENT: %dmA\n",
	    bq25700_field_read(charger, INPUT_CURRENT) * 50);
	DBG("	 MIN_SYS_VOTAGE: %dmV\n",
	    1024 + bq25700_field_read(charger, MIN_SYS_VOTAGE) * 256);
	bq25700_get_chip_state(charger, &state);
	DBG("status:\n");
	DBG("AC_STAT:  %d\n", state.ac_stat);
	DBG("ICO_DONE: %d\n", state.ico_done);
	DBG("IN_VINDPM: %d\n", state.in_vindpm);
	DBG("IN_IINDPM: %d\n", state.in_iindpm);
	DBG("IN_FCHRG: %d\n", state.in_fchrg);
	DBG("IN_PCHRG: %d\n", state.in_pchrg);
	DBG("IN_OTG: %d\n", state.in_otg);
	DBG("F_ACOV: %d\n", state.fault_acov);
	DBG("F_BATOC: %d\n", state.fault_batoc);
	DBG("F_ACOC: %d\n", state.fault_acoc);
	DBG("SYSOVP_STAT: %d\n", state.sysovp_stat);
	DBG("F_LATCHOFF: %d\n", state.fault_latchoff);
	DBG("F_OTGOVP: %d\n", state.fault_otg_ovp);
	DBG("F_OTGOCP: %d\n", state.fault_otg_ocp);

	DBG("\n+++++++++++++++++++++++++++++++++++++++++++++++++\n");
	return 0;
}

static int bq25703_dump_regs(struct bq25700_device *charger)
{
	int i = 0;
	u32 val = 0;
	struct bq25700_state state;

	for (i = 0; i < 0x10; i += 0x02) {
		regmap_read(charger->regmap, i, &val);
		DBG("REG0x%x : 0x%x\n", i, val);
	}
	for (i = 0x20; i < 0x3C; i += 0x02) {
		regmap_read(charger->regmap, i, &val);
		DBG("REG0x%x : 0x%x\n", i, val);
	}

	DBG("battery charge current: %dmA\n",
	    bq25700_field_read(charger, OUTPUT_DSG_CUR) * 64);
	DBG("battery discharge current: %dmA\n",
	    bq25700_field_read(charger, OUTPUT_CHG_CUR) * 256);
	DBG("VSYS volatge: %dmV\n",
	    2880 + bq25700_field_read(charger, OUTPUT_SYS_VOL) * 64);
	DBG("BAT volatge: %dmV\n",
	    2880 + bq25700_field_read(charger, OUTPUT_BAT_VOL) * 64);

	DBG("SET CHARGE_CURRENT: %dmA\n",
	    bq25700_field_read(charger, CHARGE_CURRENT) * 64);
	DBG("MAX_CHARGE_VOLTAGE: %dmV\n",
	    bq25700_field_read(charger, MAX_CHARGE_VOLTAGE) * 16);
	DBG("	  INPUT_VOLTAGE: %dmV\n",
	    3200 + bq25700_field_read(charger, INPUT_VOLTAGE) * 64);
	DBG("	  INPUT_CURRENT: %dmA\n",
	    bq25700_field_read(charger, INPUT_CURRENT) * 50);
	DBG("	 MIN_SYS_VOTAGE: %dmV\n",
	    1024 + bq25700_field_read(charger, MIN_SYS_VOTAGE) * 256);
	bq25700_get_chip_state(charger, &state);

	DBG("status:\n");
	DBG("AC_STAT:  %d\n", state.ac_stat);
	DBG("ICO_DONE: %d\n", state.ico_done);
	DBG("IN_VINDPM: %d\n", state.in_vindpm);
	DBG("IN_IINDPM: %d\n", state.in_iindpm);
	DBG("IN_FCHRG: %d\n", state.in_fchrg);
	DBG("IN_PCHRG: %d\n", state.in_pchrg);
	DBG("IN_OTG: %d\n", state.in_otg);
	DBG("F_ACOV: %d\n", state.fault_acov);
	DBG("F_BATOC: %d\n", state.fault_batoc);
	DBG("F_ACOC: %d\n", state.fault_acoc);
	DBG("SYSOVP_STAT: %d\n", state.sysovp_stat);
	DBG("F_LATCHOFF: %d\n", state.fault_latchoff);
	DBG("F_OTGOVP: %d\n", state.fault_otg_ovp);
	DBG("F_OTGOCP: %d\n", state.fault_otg_ocp);

	return 0;
}

ssize_t bq25700_charge_info_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct bq25700_device *charger = dev_get_drvdata(dev);

	if ((charger->chip_id & 0xff) == BQ25700_ID)
		bq25700_dump_regs(charger);
	if ((charger->chip_id & 0xff) == BQ25703_ID)
		bq25703_dump_regs(charger);

	return 0;
}

static struct device_attribute bq25700_charger_attr[] = {
	__ATTR(charge_info, 0664, bq25700_charge_info_show, NULL),
};

static void bq25700_init_sysfs(struct bq25700_device *charger)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(bq25700_charger_attr); i++) {
		ret = sysfs_create_file(&charger->dev->kobj,
					&bq25700_charger_attr[i].attr);
		if (ret)
			dev_err(charger->dev, "create charger node(%s) error\n",
				bq25700_charger_attr[i].attr.name);
	}
}

static u32 bq25700_find_idx(u32 value, enum bq25700_table_ids id)
{
	u32 idx;
	u32 rtbl_size;
	const struct bq25700_range *rtbl = &bq25700_tables[id].rt;

	rtbl_size = (rtbl->max - rtbl->min) / rtbl->step + 1;

	for (idx = 1;
	     idx < rtbl_size && (idx * rtbl->step + rtbl->min <= value);
	     idx++)
		;

	return idx - 1;
}

void bq25700_charger_set_current(unsigned long event,
				 int current_value)
{
	int idx;

	if (!bq25700_charger) {
		pr_err("[%s,%d] bq25700_charger is null\n", __func__, __LINE__);
		return;
	}
	switch (event) {
	case CHARGER_CURRENT_EVENT:
		idx = bq25700_find_idx(current_value, TBL_ICHG);
		bq25700_field_write(bq25700_charger, CHARGE_CURRENT, idx);
		break;

	case INPUT_CURRENT_EVENT:
		idx = bq25700_find_idx(current_value, TBL_INPUTCUR);
		bq25700_field_write(bq25700_charger, INPUT_CURRENT, idx);
		break;

	default:
		return;
	}
}

static int bq25700_fw_read_u32_props(struct bq25700_device *charger)
{
	int ret;
	u32 property;
	int i;
	struct bq25700_init_data *init = &charger->init_data;
	struct {
		char *name;
		bool optional;
		enum bq25700_table_ids tbl_id;
		u32 *conv_data; /* holds converted value from given property */
	} props[] = {
		/* required properties */
		{"ti,charge-current", false, TBL_ICHG,
		 &init->ichg},
		{"ti,max-charge-voltage", false, TBL_CHGMAX,
		 &init->max_chg_vol},
		{"ti,input-current-sdp", false, TBL_INPUTCUR,
		 &init->input_current_sdp},
		{"ti,input-current-dcp", false, TBL_INPUTCUR,
		 &init->input_current_dcp},
		{"ti,input-current-cdp", false, TBL_INPUTCUR,
		 &init->input_current_cdp},
		{"ti,minimum-sys-voltage", false, TBL_SYSVMIN,
		 &init->sys_min_voltage},
		{"ti,otg-voltage", false, TBL_OTGVOL,
		 &init->otg_voltage},
		{"ti,otg-current", false, TBL_OTGCUR,
		 &init->otg_current},
	};

	/* initialize data for optional properties */
	for (i = 0; i < ARRAY_SIZE(props); i++) {
		ret = device_property_read_u32(charger->dev, props[i].name,
					       &property);
		if (ret < 0) {
			if (props[i].optional)
				continue;

			return ret;
		}

		if ((props[i].tbl_id == TBL_ICHG) &&
		    (property > MAX_CHARGECURRETNT)) {
			dev_err(charger->dev, "ti,charge-current is error\n");
			return -ENODEV;
		}
		if ((props[i].tbl_id == TBL_CHGMAX) &&
		    (property > MAX_CHARGEVOLTAGE)) {
			dev_err(charger->dev, "ti,max-charge-voltage is error\n");
			return -ENODEV;
		}
		if ((props[i].tbl_id == TBL_INPUTCUR) &&
		    (property > MAX_INPUTCURRENT)) {
			dev_err(charger->dev, "ti,input-current is error\n");
			return -ENODEV;
		}
		if ((props[i].tbl_id == TBL_OTGVOL) &&
		    (property > MAX_OTGVOLTAGE)) {
			dev_err(charger->dev, "ti,ti,otg-voltage is error\n");
			return -ENODEV;
		}
		if ((props[i].tbl_id == TBL_OTGVOL) &&
		    (property > MAX_OTGCURRENT)) {
			dev_err(charger->dev, "ti,otg-current is error\n");
			return -ENODEV;
		}

		*props[i].conv_data = bq25700_find_idx(property,
						       props[i].tbl_id);
		DBG("%s, val: %d, tbl_id =%d\n", props[i].name, property,
		    *props[i].conv_data);
	}

	return 0;
}

static int bq25700_hw_init(struct bq25700_device *charger)
{
	int ret;
	int i;
	struct bq25700_state state;

	const struct {
		enum bq25700_fields id;
		u32 value;
	} init_data[] = {
		{CHARGE_CURRENT,	 charger->init_data.ichg},
		{MAX_CHARGE_VOLTAGE,	 charger->init_data.max_chg_vol},
		{MIN_SYS_VOTAGE,	 charger->init_data.sys_min_voltage},
		{OTG_VOLTAGE,	 charger->init_data.otg_voltage},
		{OTG_CURRENT,	 charger->init_data.otg_current},
	};

	/* disable watchdog */
	ret = bq25700_field_write(charger, WDTWR_ADJ, 0);
	if (ret < 0)
		return ret;

	/* initialize currents/voltages and other parameters */
	for (i = 0; i < ARRAY_SIZE(init_data); i++) {
		ret = bq25700_field_write(charger, init_data[i].id,
					  init_data[i].value);
		if (ret < 0)
			return ret;
	}

	DBG("	 CHARGE_CURRENT: %dmA\n",
	    bq25700_field_read(charger, CHARGE_CURRENT) * 64);
	DBG("MAX_CHARGE_VOLTAGE: %dmV\n",
	    bq25700_field_read(charger, MAX_CHARGE_VOLTAGE) * 16);
	DBG("	  INPUT_VOLTAGE: %dmV\n",
	    3200 + bq25700_field_read(charger, INPUT_VOLTAGE) * 64);
	DBG("	  INPUT_CURRENT: %dmA\n",
	    bq25700_field_read(charger, INPUT_CURRENT) * 50);
	DBG("	 MIN_SYS_VOTAGE: %dmV\n",
	    1024 + bq25700_field_read(charger, MIN_SYS_VOTAGE) * 256);

	/* Configure ADC for continuous conversions. This does not enable it. */

	ret = bq25700_field_write(charger, EN_LWPWR, 0);
	if (ret < 0) {
		DBG("error: EN_LWPWR\n");
		return ret;
	}

	ret = bq25700_field_write(charger, ADC_CONV, 1);
	if (ret < 0) {
		DBG("error: ADC_CONV\n");
		return ret;
	}

	ret = bq25700_field_write(charger, ADC_START, 1);
	if (ret < 0) {
		DBG("error: ADC_START\n");
		return ret;
	}

	ret = bq25700_field_write(charger, ADC_FULLSCALE, 1);
	if (ret < 0) {
		DBG("error: ADC_FULLSCALE\n");
		return ret;
	}

	ret = bq25700_field_write(charger, EN_ADC_CMPIN, 1);
	if (ret < 0) {
		DBG("error: EN_ADC_CMPIN\n");
		return ret;
	}

	ret = bq25700_field_write(charger, EN_ADC_VBUS, 1);
	if (ret < 0) {
		DBG("error: EN_ADC_VBUS\n");
		return ret;
	}

	ret = bq25700_field_write(charger, EN_ADC_PSYS, 1);
	if (ret < 0) {
		DBG("error: EN_ADC_PSYS\n");
		return ret;
	}

	ret = bq25700_field_write(charger, EN_ADC_IIN, 1);
	if (ret < 0) {
		DBG("error: EN_ADC_IIN\n");
		return ret;
	}

	ret = bq25700_field_write(charger, EN_ADC_IDCHG, 1);
	if (ret < 0) {
		DBG("error: EN_ADC_IDCHG\n");
		return ret;
	}

	ret = bq25700_field_write(charger, EN_ADC_ICHG, 1);
	if (ret < 0) {
		DBG("error: EN_ADC_ICHG\n");
		return ret;
	}

	ret = bq25700_field_write(charger, EN_ADC_VSYS, 1);
	if (ret < 0) {
		DBG("error: EN_ADC_VSYS\n");
		return ret;
	}

	ret = bq25700_field_write(charger, EN_ADC_VBAT, 1);
	if (ret < 0) {
		DBG("error: EN_ADC_VBAT\n");
		return ret;
	}

	bq25700_get_chip_state(charger, &state);
	charger->state = state;

	return 0;
}

static int bq25700_fw_probe(struct bq25700_device *charger)
{
	int ret;

	ret = bq25700_fw_read_u32_props(charger);
	if (ret < 0)
		return ret;

	return 0;
}

static void bq25700_enable_charger(struct bq25700_device *charger,
				   u32 input_current)
{
	bq25700_field_write(charger, INPUT_CURRENT, input_current);
	bq25700_field_write(charger, CHARGE_CURRENT, charger->init_data.ichg);
}

static enum power_supply_property bq25700_power_supply_props[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static int bq25700_power_supply_get_property(struct power_supply *psy,
					     enum power_supply_property psp,
					     union power_supply_propval *val)
{
	int ret;
	struct bq25700_device *bq = power_supply_get_drvdata(psy);
	struct bq25700_state state;

	state = bq->state;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (!state.ac_stat)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (state.in_fchrg == 1 ||
			 state.in_pchrg == 1)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;

	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = BQ25700_MANUFACTURER;
		break;

	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = state.ac_stat;
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		if (!state.fault_acoc &&
		    !state.fault_acov && !state.fault_batoc)
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		else if (state.fault_batoc)
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		/* read measured value */
		ret = bq25700_field_read(bq, OUTPUT_CHG_CUR);
		if (ret < 0)
			return ret;

		/* converted_val = ADC_val * 64mA  */
		val->intval = ret * 64000;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = bq25700_tables[TBL_ICHG].rt.max;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		if (!state.ac_stat) {
			val->intval = 0;
			break;
		}

		/* read measured value */
		ret = bq25700_field_read(bq, OUTPUT_BAT_VOL);
		if (ret < 0)
			return ret;

		/* converted_val = 2.88V + ADC_val * 64mV */
		val->intval = 2880000 + ret * 64000;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		val->intval = bq25700_tables[TBL_CHGMAX].rt.max;
		break;

	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		val->intval = bq25700_tables[TBL_INPUTVOL].rt.max;
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		val->intval = bq25700_tables[TBL_INPUTCUR].rt.max;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		ret = bq25700_field_read(bq, MAX_CHARGE_VOLTAGE);
		val->intval = ret * 16;
		break;

	case POWER_SUPPLY_PROP_CURRENT_MAX:
		ret = bq25700_field_read(bq, CHARGE_CURRENT);
		val->intval = ret * 64;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static char *bq25700_charger_supplied_to[] = {
	"charger",
};

static const struct power_supply_desc bq25700_power_supply_desc = {
	.name = "bq25700-charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = bq25700_power_supply_props,
	.num_properties = ARRAY_SIZE(bq25700_power_supply_props),
	.get_property = bq25700_power_supply_get_property,
};

static int bq25700_power_supply_init(struct bq25700_device *charger)
{
	struct power_supply_config psy_cfg = { .drv_data = charger, };

	psy_cfg.supplied_to = bq25700_charger_supplied_to;
	psy_cfg.num_supplicants = ARRAY_SIZE(bq25700_charger_supplied_to);

	charger->supply_charger =
		power_supply_register(charger->dev,
				      &bq25700_power_supply_desc,
				      &psy_cfg);

	return PTR_ERR_OR_ZERO(charger->supply_charger);
}

static irqreturn_t bq25700_irq_handler_thread(int irq, void *private)
{
	struct bq25700_device *charger = private;
	int irq_flag;
	struct bq25700_state state;

	if (bq25700_field_read(charger, AC_STAT)) {
		irq_flag = IRQF_TRIGGER_LOW;
	} else {
		irq_flag = IRQF_TRIGGER_HIGH;
		bq25700_field_write(charger, INPUT_CURRENT,
				    charger->init_data.input_current_sdp);
		bq25700_disable_charge(charger);
		bq25700_get_chip_state(charger, &state);
		charger->state = state;
		power_supply_changed(charger->supply_charger);
		charger->typec0_status = USB_STATUS_NONE;
		charger->typec1_status = USB_STATUS_NONE;
	}
	irq_set_irq_type(irq, irq_flag | IRQF_ONESHOT);

	return IRQ_HANDLED;
}

static void bq25700_enable_typec0(struct bq25700_device *charger)
{
	if (!IS_ERR_OR_NULL(charger->typec0_enable_io))
		gpiod_direction_output(charger->typec0_enable_io, 1);
	if (!IS_ERR_OR_NULL(charger->typec1_enable_io))
		gpiod_direction_output(charger->typec1_enable_io, 0);
}

static void bq25700_enable_typec1(struct bq25700_device *charger)
{
	if (!IS_ERR_OR_NULL(charger->typec0_enable_io))
		gpiod_direction_output(charger->typec0_enable_io, 0);
	if (!IS_ERR_OR_NULL(charger->typec1_enable_io))
		gpiod_direction_output(charger->typec1_enable_io, 1);
}

static void bq25700_disable_charge(struct bq25700_device *charger)
{
	if (!IS_ERR_OR_NULL(charger->typec0_enable_io))
		gpiod_direction_output(charger->typec0_enable_io, 0);
	if (!IS_ERR_OR_NULL(charger->typec1_enable_io))
		gpiod_direction_output(charger->typec1_enable_io, 0);
}

static void bq25700_typec0_discharge(struct bq25700_device *charger)
{
	if (!IS_ERR_OR_NULL(charger->typec0_discharge_io))
		gpiod_direction_output(charger->typec0_discharge_io, 1);
	msleep(20);
	if (!IS_ERR_OR_NULL(charger->typec0_discharge_io))
		gpiod_direction_output(charger->typec0_discharge_io, 0);
}

static void bq25700_typec1_discharge(struct bq25700_device *charger)
{
	if (!IS_ERR_OR_NULL(charger->typec1_discharge_io))
		gpiod_direction_output(charger->typec1_discharge_io, 1);
	msleep(20);
	if (!IS_ERR_OR_NULL(charger->typec1_discharge_io))
		gpiod_direction_output(charger->typec1_discharge_io, 0);
}

static void bq25700_pd_connect(struct bq25700_device *charger,
			       struct extcon_dev *edev,
			       enum tpyec_port_t port)
{
	union extcon_property_value prop_val;
	struct bq25700_state state;
	int ret;
	int vol, cur;
	int vol_idx, cur_idx;
	int i;

	if (charger->typec0_status == USB_STATUS_PD ||
	    charger->typec1_status == USB_STATUS_PD)
		return;

	if (extcon_get_cable_state_(edev, EXTCON_CHG_USB_FAST) > 0) {
		ret = extcon_get_property(edev, EXTCON_CHG_USB_FAST,
					  EXTCON_PROP_USB_TYPEC_POLARITY,
					  &prop_val);
		DBG("usb pd charge...\n");
		vol = prop_val.intval & 0xffff;
		cur = prop_val.intval >> 15;
		if (ret == 0) {
			if (port == USB_TYPEC_0) {
				charger->typec0_status = USB_STATUS_PD;
				bq25700_enable_typec0(charger);
			} else {
				charger->typec1_status = USB_STATUS_PD;
				bq25700_enable_typec1(charger);
			}

			i = 0;
			while (!bq25700_field_read(charger, AC_STAT) && i < 5) {
				msleep(1000);
				i++;
			}
			vol_idx = bq25700_find_idx((vol - 1280) * 1000,
						   TBL_INPUTVOL);
			cur_idx = bq25700_find_idx(cur * 1000, TBL_INPUTCUR);
			bq25700_field_write(charger, INPUT_VOLTAGE, vol_idx);
			bq25700_field_write(charger, INPUT_CURRENT, cur_idx);
			bq25700_field_write(charger, CHARGE_CURRENT,
					    charger->init_data.ichg);
		}

		bq25700_get_chip_state(charger, &state);
		charger->state = state;
		power_supply_changed(charger->supply_charger);
	}
}

static void bq25700_pd_evt_worker(struct work_struct *work)
{
	struct bq25700_device *charger = container_of(work,
				struct bq25700_device, pd_work.work);
	struct extcon_dev *edev = charger->cable_edev;

	bq25700_pd_connect(charger, edev, USB_TYPEC_0);
}

static void bq25700_pd_evt_worker1(struct work_struct *work)
{
	struct bq25700_device *charger = container_of(work,
				struct bq25700_device, pd_work1.work);
	struct extcon_dev *edev = charger->cable_edev_1;

	bq25700_pd_connect(charger, edev, USB_TYPEC_1);
}

static int bq25700_pd_evt_notifier(struct notifier_block *nb,
				   unsigned long event,
				   void *ptr)
{
	struct bq25700_device *charger =
		container_of(nb, struct bq25700_device, cable_pd_nb);

	queue_delayed_work(charger->usb_charger_wq, &charger->pd_work,
			   msecs_to_jiffies(10));

	return NOTIFY_DONE;
}

static int bq25700_pd_evt_notifier1(struct notifier_block *nb,
				    unsigned long event,
				    void *ptr)
{
	struct bq25700_device *charger =
		container_of(nb, struct bq25700_device, cable_pd_nb1);

	queue_delayed_work(charger->usb_charger_wq, &charger->pd_work1,
			   msecs_to_jiffies(10));

	return NOTIFY_DONE;
}

static void bq25700_charger_evt_handel(struct bq25700_device *charger,
				       struct extcon_dev *edev,
				       enum tpyec_port_t port)
{
	struct bq25700_state state;
	enum charger_t charger_state = USB_TYPE_UNKNOWN_CHARGER;

	if (charger->typec0_status == USB_STATUS_PD ||
	    charger->typec1_status == USB_STATUS_PD)
		return;

	/* Determine cable/charger type */
	if (extcon_get_cable_state_(edev, EXTCON_CHG_USB_SDP) > 0) {
		charger_state = USB_TYPE_USB_CHARGER;

		bq25700_enable_charger(charger,
				       charger->init_data.input_current_sdp);
		DBG("USB_TYPE_USB_CHARGER\n");
	} else if (extcon_get_cable_state_(edev, EXTCON_CHG_USB_DCP) > 0) {
		charger_state = USB_TYPE_AC_CHARGER;
		bq25700_enable_charger(charger,
				       charger->init_data.input_current_dcp);
		DBG("USB_TYPE_AC_CHARGER\n");
	} else if (extcon_get_cable_state_(edev, EXTCON_CHG_USB_CDP) > 0) {
		charger_state = USB_TYPE_CDP_CHARGER;
		bq25700_enable_charger(charger,
				       charger->init_data.input_current_cdp);
		DBG("USB_TYPE_CDP_CHARGER\n");
	}
	if (port == USB_TYPEC_0) {
		if (charger_state == USB_TYPE_USB_CHARGER)
			charger->typec0_status = USB_STATUS_USB;
		else
			charger->typec0_status = USB_STATUS_AC;
		bq25700_enable_typec0(charger);
	} else {
		if (charger_state == USB_TYPE_USB_CHARGER)
			charger->typec1_status = USB_STATUS_USB;
		else
			charger->typec1_status = USB_STATUS_AC;
		bq25700_enable_typec1(charger);
	}

	bq25700_get_chip_state(charger, &state);
	charger->state = state;
	power_supply_changed(charger->supply_charger);
}

static void bq25700_charger_usb_bc_handel(struct bq25700_device *charger)
{
	struct bq25700_state state;

	switch (charger->bc_event) {
	case USB_BC_TYPE_SDP:
		bq25700_enable_charger(charger,
				       charger->init_data.input_current_sdp);
		DBG("USB_TYPE_USB_CHARGER\n");
		break;
	case USB_BC_TYPE_DCP:
		bq25700_enable_charger(charger,
				       charger->init_data.input_current_dcp);
		break;
	case USB_BC_TYPE_CDP:
		bq25700_enable_charger(charger,
				       charger->init_data.input_current_cdp);
		DBG("USB_TYPE_CDP_CHARGER\n");
		break;
	default:
		break;
	}
	bq25700_get_chip_state(charger, &state);
	charger->state = state;
	power_supply_changed(charger->supply_charger);
}

static void bq25700_charger_evt_worker(struct work_struct *work)
{
	struct bq25700_device *charger = container_of(work,
				struct bq25700_device, usb_work.work);
	struct extcon_dev *edev = charger->cable_edev;
	if (charger->usb_bc == 0)
		bq25700_charger_evt_handel(charger, edev, USB_TYPEC_0);
	else
		bq25700_charger_usb_bc_handel(charger);
}

static void bq25700_charger_evt_worker1(struct work_struct *work)
{
	struct bq25700_device *charger = container_of(work,
				struct bq25700_device, usb_work1.work);
	struct extcon_dev *edev = charger->cable_edev_1;

	bq25700_charger_evt_handel(charger, edev, USB_TYPEC_1);
}

static int bq25700_charger_evt_notifier(struct notifier_block *nb,
					unsigned long event,
					void *ptr)
{
	struct bq25700_device *charger =
		container_of(nb, struct bq25700_device, cable_cg_nb);
	charger->bc_event = event;
	queue_delayed_work(charger->usb_charger_wq, &charger->usb_work,
			   msecs_to_jiffies(10));

	return NOTIFY_DONE;
}

static int bq25700_charger_evt_notifier1(struct notifier_block *nb,
					 unsigned long event,
					 void *ptr)
{
	struct bq25700_device *charger =
		container_of(nb, struct bq25700_device, cable_cg_nb1);

	queue_delayed_work(charger->usb_charger_wq, &charger->usb_work1,
			   msecs_to_jiffies(10));

	return NOTIFY_DONE;
}

static void bq25700_host_evt_worker(struct work_struct *work)
{
	struct bq25700_device *charger =
		container_of(work, struct bq25700_device, host_work.work);
	struct extcon_dev *edev = charger->cable_edev;

	/* Determine cable/charger type */
	if (extcon_get_cable_state_(edev, EXTCON_USB_VBUS_EN) > 0) {
		if (!IS_ERR_OR_NULL(charger->otg_mode_en_io))
			gpiod_direction_output(charger->otg_mode_en_io, 1);
		bq25700_field_write(charger, EN_OTG, 1);
		DBG("OTG enable\n");
	} else if (extcon_get_cable_state_(edev, EXTCON_USB_VBUS_EN) == 0) {
		if (!IS_ERR_OR_NULL(charger->otg_mode_en_io))
			gpiod_direction_output(charger->otg_mode_en_io, 0);
		bq25700_field_write(charger, EN_OTG, 0);
		DBG("OTG disable\n");
	}
}

static void bq25700_host_evt_worker1(struct work_struct *work)
{
	struct bq25700_device *charger =
		container_of(work, struct bq25700_device, host_work1.work);
	struct extcon_dev *edev = charger->cable_edev_1;

	/* Determine cable/charger type */
	if (extcon_get_cable_state_(edev, EXTCON_USB_VBUS_EN) > 0) {
		if (!IS_ERR_OR_NULL(charger->otg_mode_en_io))
			gpiod_direction_output(charger->otg_mode_en_io, 1);
		bq25700_field_write(charger, EN_OTG, 1);
		DBG("OTG enable\n");
	} else if (extcon_get_cable_state_(edev, EXTCON_USB_VBUS_EN) == 0) {
		if (!IS_ERR_OR_NULL(charger->otg_mode_en_io))
			gpiod_direction_output(charger->otg_mode_en_io, 0);
		bq25700_field_write(charger, EN_OTG, 0);
		DBG("OTG disable\n");
	}
}

static int bq25700_host_evt_notifier(struct notifier_block *nb,
				     unsigned long event, void *ptr)
{
	struct bq25700_device *charger =
		container_of(nb, struct bq25700_device, cable_host_nb);

	queue_delayed_work(charger->usb_charger_wq, &charger->host_work,
			   msecs_to_jiffies(10));

	return NOTIFY_DONE;
}

static int bq25700_host_evt_notifier1(struct notifier_block *nb,
				      unsigned long event, void *ptr)
{
	struct bq25700_device *charger =
		container_of(nb, struct bq25700_device, cable_host_nb1);

	queue_delayed_work(charger->usb_charger_wq, &charger->host_work1,
			   msecs_to_jiffies(10));

	return NOTIFY_DONE;
}

static void bq25700_discnt(struct bq25700_device *charger,
			   enum tpyec_port_t port)
{
	int vol_idx;
	struct bq25700_state state;

	if (bq25700_field_read(charger, AC_STAT) == 0) {
		bq25700_disable_charge(charger);
		if (port == USB_TYPEC_0) {
			bq25700_typec0_discharge(charger);
			charger->typec0_status = USB_STATUS_NONE;
		} else {
			bq25700_typec1_discharge(charger);
			charger->typec1_status = USB_STATUS_NONE;
		}

		vol_idx = bq25700_find_idx(DEFAULT_INPUTVOL, TBL_INPUTVOL);
		bq25700_field_write(charger, INPUT_VOLTAGE, vol_idx);
		bq25700_field_write(charger, INPUT_CURRENT,
				    charger->init_data.input_current_sdp);
		bq25700_get_chip_state(charger, &state);
		charger->state = state;
		power_supply_changed(charger->supply_charger);
	}
}

static void bq25700_discnt_evt_worker(struct work_struct *work)
{
	struct bq25700_device *charger = container_of(work,
						      struct bq25700_device,
						      discnt_work.work);

	bq25700_discnt(charger, USB_TYPEC_0);
}

static void bq25700_discnt_evt_worker1(struct work_struct *work)
{
	struct bq25700_device *charger = container_of(work,
						      struct bq25700_device,
						      discnt_work1.work);
	bq25700_discnt(charger, USB_TYPEC_1);
}

static int bq25700_discnt_evt_notfier(struct notifier_block *nb,
				      unsigned long event,
				      void *ptr)
{
	struct bq25700_device *charger =
		container_of(nb, struct bq25700_device, cable_discnt_nb);

	queue_delayed_work(charger->usb_charger_wq,
			   &charger->discnt_work,
			   msecs_to_jiffies(0));

	return NOTIFY_DONE;
}

static int bq25700_discnt_evt_notfier1(struct notifier_block *nb,
				       unsigned long event,
				       void *ptr)
{
	struct bq25700_device *charger =
		container_of(nb, struct bq25700_device, cable_discnt_nb1);

	queue_delayed_work(charger->usb_charger_wq,
			   &charger->discnt_work1,
			   msecs_to_jiffies(0));

	return NOTIFY_DONE;
}

static int bq25700_register_cg_extcon(struct bq25700_device *charger,
				      struct extcon_dev *edev,
				      struct notifier_block *able_cg_nb)
{
	int ret;

	ret = extcon_register_notifier(edev,
				       EXTCON_CHG_USB_SDP,
				       able_cg_nb);
	if (ret < 0) {
		dev_err(charger->dev, "failed to register notifier for SDP\n");
		return -1;
	}

	ret = extcon_register_notifier(edev,
				       EXTCON_CHG_USB_DCP,
				       able_cg_nb);
	if (ret < 0) {
		dev_err(charger->dev, "failed to register notifier for DCP\n");
		return -1;
	}

	ret = extcon_register_notifier(edev,
				       EXTCON_CHG_USB_CDP,
				       able_cg_nb);
	if (ret < 0) {
		dev_err(charger->dev, "failed to register notifier for CDP\n");
		return -1;
	}

	return 0;
}

static int bq25700_register_cg_nb(struct bq25700_device *charger)
{
	enum bc_port_type bc_type;
	int ret;

	if (charger->usb_bc == 0) {
		if (charger->cable_edev) {
			/* Register chargers  */
			INIT_DELAYED_WORK(&charger->usb_work,
					  bq25700_charger_evt_worker);
			charger->cable_cg_nb.notifier_call =
				bq25700_charger_evt_notifier;
			bq25700_register_cg_extcon(charger, charger->cable_edev,
						   &charger->cable_cg_nb);
		}

		if (charger->cable_edev_1) {
			INIT_DELAYED_WORK(&charger->usb_work1,
					  bq25700_charger_evt_worker1);
			charger->cable_cg_nb1.notifier_call =
				bq25700_charger_evt_notifier1;
			bq25700_register_cg_extcon(charger,
						   charger->cable_edev_1,
						   &charger->cable_cg_nb1);
		}
	} else {
		INIT_DELAYED_WORK(&charger->usb_work,
				  bq25700_charger_evt_worker);
		charger->cable_cg_nb.notifier_call =
			bq25700_charger_evt_notifier;

		ret = rk_bc_detect_notifier_register(&charger->cable_cg_nb,
						     &bc_type);
		if (ret) {
			dev_err(charger->dev, "failed to register notifier for bc\n");
			return -EINVAL;
		}
	}
	return 0;
}

static int bq25700_register_discnt_nb(struct bq25700_device *charger)
{
	int ret;

	/* Register discnt usb */
	if (charger->cable_edev) {
		INIT_DELAYED_WORK(&charger->discnt_work,
				  bq25700_discnt_evt_worker);
		charger->cable_discnt_nb.notifier_call =
			bq25700_discnt_evt_notfier;
		ret = extcon_register_notifier(charger->cable_edev,
					       EXTCON_USB,
					       &charger->cable_discnt_nb);
		if (ret < 0) {
			dev_err(charger->dev,
				"failed to register notifier for HOST\n");
			return ret;
		}
	}

	if (charger->cable_edev_1) {
		INIT_DELAYED_WORK(&charger->discnt_work1,
				  bq25700_discnt_evt_worker1);
		charger->cable_discnt_nb1.notifier_call =
			bq25700_discnt_evt_notfier1;
		ret = extcon_register_notifier(charger->cable_edev_1,
					       EXTCON_USB,
					       &charger->cable_discnt_nb1);
		if (ret < 0) {
			dev_err(charger->dev,
				"failed to register notifier for HOST\n");
			return ret;
		}
	}

	return 0;
}

static int bq25700_register_pd_nb(struct bq25700_device *charger)
{
	if (charger->cable_edev) {
		INIT_DELAYED_WORK(&charger->pd_work, bq25700_pd_evt_worker);
		charger->cable_pd_nb.notifier_call = bq25700_pd_evt_notifier;
		extcon_register_notifier(charger->cable_edev,
					 EXTCON_CHG_USB_FAST,
					 &charger->cable_pd_nb);
	}

	if (charger->cable_edev_1) {
		INIT_DELAYED_WORK(&charger->pd_work1, bq25700_pd_evt_worker1);
		charger->cable_pd_nb1.notifier_call = bq25700_pd_evt_notifier1;
		extcon_register_notifier(charger->cable_edev_1,
					 EXTCON_CHG_USB_FAST,
					 &charger->cable_pd_nb1);
	}

	return 0;
}

static int bq25700_register_host_nb(struct bq25700_device *charger)
{
	int ret;

	/* Register host */
	if (charger->cable_edev) {
		INIT_DELAYED_WORK(&charger->host_work, bq25700_host_evt_worker);
		charger->cable_host_nb.notifier_call =
			bq25700_host_evt_notifier;
		ret = extcon_register_notifier(charger->cable_edev,
					       EXTCON_USB_HOST,
					       &charger->cable_host_nb);
		if (ret < 0) {
			dev_err(charger->dev,
				"failed to register notifier for HOST\n");
			return -1;
		}
	}

	if (charger->cable_edev_1) {
		INIT_DELAYED_WORK(&charger->host_work1,
				  bq25700_host_evt_worker1);
		charger->cable_host_nb1.notifier_call =
			bq25700_host_evt_notifier1;
		ret = extcon_register_notifier(charger->cable_edev_1,
					       EXTCON_USB_HOST,
					       &charger->cable_host_nb1);
		if (ret < 0) {
			dev_err(charger->dev,
				"failed to register notifier for HOST\n");
			return -1;
		}
	}

	return 0;
}

static long bq25700_init_usb(struct bq25700_device *charger)
{
	struct extcon_dev *edev, *edev1;
	struct device *dev = charger->dev;

	charger->usb_charger_wq = alloc_ordered_workqueue("%s",
							  WQ_MEM_RECLAIM |
							  WQ_FREEZABLE,
							  "bq25700-usb-wq");
	/* type-C */
	edev = extcon_get_edev_by_phandle(dev, 0);
	if (IS_ERR(edev)) {
		if (PTR_ERR(edev) != -EPROBE_DEFER)
			dev_err(dev, "Invalid or missing extcon dev0\n");
		charger->cable_edev = NULL;
	} else {
		charger->cable_edev = edev;
	}

	edev1 = extcon_get_edev_by_phandle(dev, 1);
	if (IS_ERR(edev1)) {
		if (PTR_ERR(edev1) != -EPROBE_DEFER)
			dev_err(dev, "Invalid or missing extcon dev1\n");
		charger->cable_edev_1 = NULL;
	} else {
		charger->cable_edev_1 = edev1;
	}
	if (!charger->pd_charge_only)
		bq25700_register_cg_nb(charger);
	bq25700_register_host_nb(charger);
	bq25700_register_discnt_nb(charger);
	bq25700_register_pd_nb(charger);

	if (charger->cable_edev) {
		schedule_delayed_work(&charger->host_work, 0);
		schedule_delayed_work(&charger->pd_work, 0);
		if (!charger->pd_charge_only)
			schedule_delayed_work(&charger->usb_work, 0);
	}
	if (charger->cable_edev_1) {
		schedule_delayed_work(&charger->host_work1, 0);
		schedule_delayed_work(&charger->pd_work1, 0);
		if (!charger->pd_charge_only)
			schedule_delayed_work(&charger->usb_work1, 0);
	}

	return 0;
}

static int bq25700_parse_dt(struct bq25700_device *charger)
{
	int ret;
	struct device_node *np = charger->dev->of_node;
	struct device_node *temp_np = NULL;

	charger->typec0_enable_io = devm_gpiod_get_optional(charger->dev,
							    "typec0-enable",
							    GPIOD_IN);
	if (!IS_ERR_OR_NULL(charger->typec0_enable_io))
		gpiod_direction_output(charger->typec0_enable_io, 0);

	charger->typec1_enable_io = devm_gpiod_get_optional(charger->dev,
							    "typec1-enable",
							    GPIOD_IN);
	if (!IS_ERR_OR_NULL(charger->typec1_enable_io))
		gpiod_direction_output(charger->typec1_enable_io, 0);

	charger->typec0_discharge_io =
		devm_gpiod_get_optional(charger->dev, "typec0-discharge",
					GPIOD_IN);

	charger->typec1_discharge_io =
		devm_gpiod_get_optional(charger->dev, "typec1-discharge",
					GPIOD_IN);

	charger->otg_mode_en_io =  devm_gpiod_get_optional(charger->dev,
							    "otg-mode-en",
							    GPIOD_IN);
	if (!IS_ERR_OR_NULL(charger->otg_mode_en_io))
		gpiod_direction_output(charger->otg_mode_en_io, 0);

	ret = of_property_read_u32(np, "pd-charge-only",
				   &charger->pd_charge_only);
	if (ret < 0)
		dev_err(charger->dev, "pd-charge-only!\n");

	temp_np = of_find_node_by_name(NULL, "usb_bc");
	if (!temp_np)
		charger->usb_bc = 0;
	else
		charger->usb_bc = 1;

	return 0;
}

static int bq25700_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct device *dev = &client->dev;
	struct bq25700_device *charger;
	struct device_node *charger_np;
	int ret = 0;
	u32 i = 0;
	int irq_flag;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA))
		return -EIO;

	charger = devm_kzalloc(&client->dev, sizeof(*charger), GFP_KERNEL);
	if (!charger)
		return -EINVAL;

	charger->client = client;
	charger->dev = dev;

	charger_np = of_find_compatible_node(NULL, NULL, "ti,bq25700");
	if (charger_np) {
		charger->regmap = devm_regmap_init_i2c(client,
						       &bq25700_regmap_config);
		if (IS_ERR(charger->regmap)) {
			dev_err(&client->dev, "Failed to initialize regmap\n");
			return -EINVAL;
		}

		for (i = 0; i < ARRAY_SIZE(bq25700_reg_fields); i++) {
			const struct reg_field *reg_fields = bq25700_reg_fields;

			charger->rmap_fields[i] =
				devm_regmap_field_alloc(dev,
							charger->regmap,
							reg_fields[i]);
			if (IS_ERR(charger->rmap_fields[i])) {
				dev_err(dev, "cannot allocate regmap field\n");
				return PTR_ERR(charger->rmap_fields[i]);
			}
		}
	} else {
		charger->regmap = devm_regmap_init_i2c(client,
						       &bq25703_regmap_config);

		if (IS_ERR(charger->regmap)) {
			dev_err(&client->dev, "Failed to initialize regmap\n");
			return -EINVAL;
		}

		for (i = 0; i < ARRAY_SIZE(bq25703_reg_fields); i++) {
			const struct reg_field *reg_fields = bq25703_reg_fields;

			charger->rmap_fields[i] =
				devm_regmap_field_alloc(dev,
							charger->regmap,
							reg_fields[i]);
			if (IS_ERR(charger->rmap_fields[i])) {
				dev_err(dev, "cannot allocate regmap field\n");
				return PTR_ERR(charger->rmap_fields[i]);
			}
		}
	}
	i2c_set_clientdata(client, charger);

	/*read chip id. Confirm whether to support the chip*/
	charger->chip_id = bq25700_field_read(charger, DEVICE_ID);

	if (charger->chip_id < 0) {
		dev_err(dev, "Cannot read chip ID.\n");
		return charger->chip_id;
	}

	if (!dev->platform_data) {
		ret = bq25700_fw_probe(charger);
		if (ret < 0) {
			dev_err(dev, "Cannot read device properties.\n");
			return ret;
		}
	} else {
		return -ENODEV;
	}

	ret = bq25700_hw_init(charger);
	if (ret < 0) {
		dev_err(dev, "Cannot initialize the chip.\n");
		return ret;
	}

	bq25700_parse_dt(charger);
	bq25700_init_sysfs(charger);

	bq25700_power_supply_init(charger);
	bq25700_init_usb(charger);

	if (client->irq < 0) {
		dev_err(dev, "No irq resource found.\n");
		return client->irq;
	}

	if (bq25700_field_read(charger, AC_STAT))
		irq_flag = IRQF_TRIGGER_LOW;
	else
		irq_flag = IRQF_TRIGGER_HIGH;

	device_init_wakeup(dev, 1);

	ret = devm_request_threaded_irq(dev, client->irq, NULL,
					bq25700_irq_handler_thread,
					irq_flag | IRQF_ONESHOT,
					"bq25700_irq", charger);
	if (ret)
		goto irq_fail;
	enable_irq_wake(client->irq);

	bq25700_charger = charger;

irq_fail:
	return ret;
}

void bq25700_shutdown(struct i2c_client *client)
{
	int vol_idx;
	struct bq25700_device *charger = i2c_get_clientdata(client);

	vol_idx = bq25700_find_idx(DEFAULT_INPUTVOL, TBL_INPUTVOL);
	bq25700_field_write(charger, INPUT_VOLTAGE, vol_idx);
	bq25700_field_write(charger, INPUT_CURRENT,
			    charger->init_data.input_current_sdp);
}

#ifdef CONFIG_PM_SLEEP
static int bq25700_pm_suspend(struct device *dev)
{
	return 0;
}

static int bq25700_pm_resume(struct device *dev)
{
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(bq25700_pm_ops, bq25700_pm_suspend, bq25700_pm_resume);

static const struct i2c_device_id bq25700_i2c_ids[] = {
	{ "bq25700"},
	{ },
};
MODULE_DEVICE_TABLE(i2c, bq25700_i2c_ids);

#ifdef CONFIG_OF
static const struct of_device_id bq25700_of_match[] = {
	{ .compatible = "ti,bq25700", },
	{ .compatible = "ti,bq25703", },
	{ },
};
MODULE_DEVICE_TABLE(of, bq25700_of_match);
#else
static const struct of_device_id bq25700_of_match[] = {
	{ },
};
#endif

static struct i2c_driver bq25700_driver = {
	.probe		= bq25700_probe,
	.shutdown	= bq25700_shutdown,
	.id_table	= bq25700_i2c_ids,
	.driver = {
		.name		= "bq25700-charger",
		.pm		= &bq25700_pm_ops,
		.of_match_table	= of_match_ptr(bq25700_of_match),
	},
};
module_i2c_driver(bq25700_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("shengfeixu <xsf@rock-chips.com>");
MODULE_DESCRIPTION("TI bq25700 Charger Driver");
