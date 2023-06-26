// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"LCDB: %s: " fmt, __func__

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/of_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>

#define QPNP_LCDB_REGULATOR_DRIVER_NAME		"qcom,qpnp-lcdb-regulator"
#define QPNP_LCDB_REGULATOR_DRIVER_660		"qcom,lcdb-pm660"
#define QPNP_LCDB_REGULATOR_DRIVER_632	"qcom,lcdb-pmi632"
#define QPNP_LCDB_REGULATOR_DRIVER_6150L	"qcom,lcdb-pm6150l"
#define QPNP_LCDB_REGULATOR_DRIVER_7325B	"qcom,lcdb-pm7325b"

/* LCDB */
#define LCDB_REVISION3_REG			0x02
#define LCDB_REVISION4_REG			0x03

#define LCDB_STS1_REG			0x08

#define INT_RT_STATUS_REG		0x10
#define VREG_OK_RT_STS_BIT		BIT(0)
#define SC_ERROR_RT_STS_BIT		BIT(1)

#define LCDB_STS3_REG			0x0A
#define LDO_VREG_OK_BIT			BIT(7)

#define LCDB_STS4_REG			0x0B
#define NCP_VREG_OK_BIT			BIT(7)

#define LCDB_AUTO_TOUCH_WAKE_CTL_REG	0x40
#define EN_AUTO_TOUCH_WAKE_BIT		BIT(7)
#define ATTW_TOFF_TIME_MASK		GENMASK(3, 2)
#define ATTW_TON_TIME_MASK		GENMASK(1, 0)
#define ATTW_TOFF_TIME_SHIFT		2
#define ATTW_MIN_MS			4
#define ATTW_MAX_MS			32

#define LCDB_BST_OUTPUT_VOLTAGE_REG	0x41
#define PM660_BST_OUTPUT_VOLTAGE_MASK	GENMASK(4, 0)
#define BST_OUTPUT_VOLTAGE_MASK		GENMASK(5, 0)
#define PM7325B_BST_OUTPUT_VOLTAGE_MASK		GENMASK(7, 0)

#define LCDB_STEPPER_VOUT_CTL_REG		0x42
#define VOUT_STEP_DLY_2US		0x4

#define LCDB_CONFIG_SEL_REG		0x43
#define EN_FAST_STARTUP_BIT			BIT(7)
#define PDN_CONFIG_SEL_BIT			BIT(4)
#define PWRUP_CONFIG_SEL_MASK		GENMASK(2, 0)
#define PWRUP_CONFIG_MAX		0x4

#define LCDB_MODULE_RDY_REG		0x45
#define MODULE_RDY_BIT			BIT(7)

#define LCDB_ENABLE_CTL1_REG		0x46
#define MODULE_EN_BIT			BIT(7)
#define HWEN_RDY_BIT			BIT(6)

/* BST */
#define LCDB_BST_PD_CTL_REG		0x47
#define BOOST_DIS_PULLDOWN_BIT		BIT(1)
#define BOOST_PD_STRENGTH_BIT		BIT(0)
#define PM7325B_BOOST_EN_PULLDOWN_BIT		BIT(7)

#define LCDB_BST_ILIM_CTL_REG		0x4B
#define EN_BST_ILIM_BIT			BIT(7)
#define SET_BST_ILIM_MASK		GENMASK(2, 0)
#define MIN_BST_ILIM_MA			200
#define MAX_BST_ILIM_MA			1600
#define PM7325B_MIN_BST_ILIM_MA			1130
#define PM7325B_MAX_BST_ILIM_MA			2250
#define PM7325B_BST_ILIM_MA_STEP			160

#define LCDB_PS_CTL_REG			0x50
#define EN_PS_BIT			BIT(7)
#define PM660_PS_THRESH_MASK		GENMASK(1, 0)
#define PS_THRESH_MASK			GENMASK(2, 0)
#define MIN_BST_PS_MA			50
#define MAX_BST_PS_MA			80
#define PM7325B_MIN_BST_PS_MV			360
#define PM7325B_MAX_BST_PS_MV			528

#define LCDB_RDSON_MGMNT_REG		0x53
#define NFET_SW_SIZE_MASK		GENMASK(3, 2)
#define NFET_SW_SIZE_SHIFT		2
#define PFET_SW_SIZE_MASK		GENMASK(1, 0)

#define PM7325B_LCDB_P2_BLANK_TIMER_REG		0x54
#define HIGH_P2_BLK_SEL_MASK		GENMASK(6, 4)
#define HIGH_P2_BLK_SEL_SHIFT		4
#define LOW_P2_BLK_SEL_MASK		GENMASK(2, 0)

#define LCDB_BST_VREG_OK_CTL_REG	0x55
#define BST_VREG_OK_DEB_MASK		GENMASK(1, 0)

#define PM7325B_LCDB_BST_VREG_OK_CTL_REG	0x56

#define LCDB_BST_SS_CTL_REG		0x5B
#define BST_SS_TIME_MASK		GENMASK(1, 0)
#define BST_PRECHG_SHORT_ALARM_SHIFT	2
#define BST_PRECHARGE_DONE_DEB_BIT	BIT(4)
#define BST_SS_TIME_OVERRIDE_SHIFT	5

#define BST_SS_TIME_OVERRIDE_0MS	0
#define BST_SS_TIME_OVERRIDE_0P5_MS	1
#define BST_SS_TIME_OVERRIDE_1MS	2
#define BST_SS_TIME_OVERRIDE_2MS	3

#define EN_BST_PRECHG_SHORT_ALARM	0
#define DIS_BST_PRECHG_SHORT_ALARM	1

#define PM7325B_LCDB_WARMUP_DLY_SEL_1_REG		0x5C
#define PM7325B_LCDB_WARMUP_DLY_SEL_2_REG		0x5D
#define PM7325B_LCDB_PRECHARGE_CTL_REG		0x5E

#define LCDB_SOFT_START_CTL_REG		0x5F

#define LCDB_MISC_CTL_REG		0x60
#define AUTO_GM_EN_BIT			BIT(4)
#define EN_TOUCH_WAKE_BIT		BIT(3)
#define DIS_SCP_BIT			BIT(0)

#define PM7325B_LCDB_MPC_CTL_REG		0x60
#define MPC_NCP_SD_SEL_MASK		GENMASK(2, 0)
#define MPC_CURRENT_MIN		160
#define MPC_CURRENT_MAX		440
#define MPC_CURRENT_STEP		40

#define LCDB_PFM_CTL_REG		0x62
#define EN_PFM_BIT			BIT(7)
#define BYP_BST_SOFT_START_COMP_BIT	BIT(0)
#define PFM_HYSTERESIS_SHIFT		4
#define PFM_CURRENT_SHIFT		2

#define LCDB_PWRUP_PWRDN_CTL_REG	0x66
#define PWRUP_DELAY_MASK		GENMASK(3, 2)
#define PWRDN_DELAY_MASK		GENMASK(1, 0)
#define PWRUP_DELAY_SHIFT				2
#define PWRDN_DELAY_MIN_MS		0
#define PWRDN_DELAY_MAX_MS		8

/* LDO */
#define LCDB_LDO_OUTPUT_VOLTAGE_REG	0x71
#define SET_OUTPUT_VOLTAGE_MASK		GENMASK(4, 0)
#define PM7325B_SET_OUTPUT_VOLTAGE_MASK	GENMASK(5, 0)

#define LCDB_LDO_VREG_OK_CTL_REG	0x75
#define VREG_OK_DEB_MASK		GENMASK(1, 0)

#define LCDB_LDO_PD_CTL_REG		0x77
#define LDO_DIS_PULLDOWN_BIT		BIT(1)
#define LDO_PD_STRENGTH_BIT		BIT(0)
#define PM7325B_LDO_EN_PULLDOWN_BIT		BIT(7)

#define LCDB_LDO_FORCE_PD_CTL_REG	0x79
#define LDO_FORCE_PD_EN_BIT		BIT(0)
#define LDO_FORCE_PD_MODE		BIT(7)

#define LCDB_LDO_ILIM_CTL1_REG		0x7B
#define EN_LDO_ILIM_BIT			BIT(7)
#define SET_LDO_ILIM_MASK		GENMASK(2, 0)
#define SET_LDO_ILIM_MASK_SD		GENMASK(6, 4)
#define SET_LDO_ILIM_MASK_SD_SHIFT	4
#define MIN_LDO_ILIM_MA			110
#define MAX_LDO_ILIM_MA			460
#define PM7325B_MIN_LDO_ILIM_MA			35
#define PM7325B_MAX_LDO_ILIM_MA			840
#define LDO_ILIM_STEP_MA		50

#define LCDB_LDO_ILIM_CTL2_REG		0x7C

#define LCDB_LDO_SOFT_START_CTL_REG	0x7F
#define SOFT_START_MASK			GENMASK(1, 0)

/* NCP */
#define LCDB_NCP_OUTPUT_VOLTAGE_REG	0x81
#define EN_NCP_VOUT_SYMMETRY_BIT		BIT(7)

#define LCDB_NCP_VREG_OK_CTL_REG	0x85

#define LCDB_NCP_PD_CTL_REG		0x87
#define NCP_DIS_PULLDOWN_BIT		BIT(1)
#define NCP_PD_STRENGTH_BIT		BIT(0)
#define PM7325B_EN_NCP_PULLDOWN_BIT		BIT(1)
#define PM7325B_EN_PD_SYMMETRY_BIT			BIT(7)

#define LCDB_NCP_ILIM_CTL1_REG		0x8B
#define EN_NCP_ILIM_BIT			BIT(7)
#define SET_NCP_ILIM_MASK		GENMASK(1, 0)
#define PM7325B_SET_NCP_ILIM_SD_MASK		GENMASK(5, 4)
#define MIN_NCP_ILIM_MA			260
#define MAX_NCP_ILIM_MA			810
#define PM7325B_MIN_NCP_ILIM_MA			700
#define PM7325B_MAX_NCP_ILIM_MA			1000

#define LCDB_NCP_ILIM_CTL2_REG		0x8C

#define LCDB_NCP_SOFT_START_CTL_REG	0x8F

/* common for BST/NCP/LDO */
#define MIN_DBC_US			2
#define MAX_DBC_US			32

#define MIN_SOFT_START_US		0
#define MAX_SOFT_START_US		2000

#define PM660_BST_HEADROOM_DEFAULT_MV	200
#define BST_HEADROOM_DEFAULT_MV		150

#define PMIC5_LCDB_OFF_ON_DELAY_US	20000

struct ldo_regulator {
	struct regulator_desc		rdesc;
	struct regulator_dev		*rdev;
	struct device_node		*node;

	/* LDO DT params */
	int				pd;
	int				pd_strength;
	int				ilim_ma;
	int				soft_start_us;
	int				vreg_ok_dbc_us;
	int				voltage_mv;
	int				prev_voltage_mv;
};

struct ncp_regulator {
	struct regulator_desc		rdesc;
	struct regulator_dev		*rdev;
	struct device_node		*node;

	/* NCP DT params */
	int				pd;
	int				pd_strength;
	int				ilim_ma;
	int				soft_start_us;
	int				vreg_ok_dbc_us;
	int				voltage_mv;
	int				prev_voltage_mv;
};

struct bst_params {
	struct device_node		*node;

	/* BST DT params */
	int				pd;
	int				pd_strength;
	int				ilim_ma;
	int				ps;
	int				ps_threshold;
	int				soft_start_us;
	int				vreg_ok_dbc_us;
	int				voltage_mv;
	u16				headroom_mv;
};

enum pmic_type {
	PM_DEFAULT,
	PM660L,
	PMI632,
	PM6150L,
	PM7325B,
};

struct qpnp_lcdb {
	struct device			*dev;
	struct platform_device		*pdev;
	struct regmap			*regmap;
	enum pmic_type			subtype;
	u32				base;
	u32				wa_flags;
	int				sc_irq;
	int				pwrdn_delay_ms;
	int				pwrup_delay_ms;
	int				min_voltage_mv;
	int				max_voltage_mv;
	int				pwrup_config;
	int				high_p2_blk_ns;
	int				low_p2_blk_ns;
	int				mpc_current_thr_ma;
	bool			ncp_symmetry;

	/* TTW params */
	bool				ttw_enable;
	bool				ttw_mode_sw;

	/* status parameters */
	bool				lcdb_enabled;
	bool				settings_saved;
	bool				lcdb_sc_disable;
	bool				voltage_step_ramp;
	int				sc_count;
	ktime_t				sc_module_enable_time;

	struct mutex			lcdb_mutex;
	struct mutex			read_write_mutex;
	struct bst_params		bst;
	struct ldo_regulator		ldo;
	struct ncp_regulator		ncp;
};

struct settings {
	u16	address;
	u8	value;
	bool	sec_access;
	bool	valid;
};

enum lcdb_module {
	LDO,
	NCP,
	BST,
	LDO_NCP,
};

enum pfm_hysteresis {
	PFM_HYST_15MV,
	PFM_HYST_25MV,
	PFM_HYST_35MV,
	PFM_HYST_45MV,
};

enum pfm_peak_current {
	PFM_PEAK_CURRENT_300MA,
	PFM_PEAK_CURRENT_400MA,
	PFM_PEAK_CURRENT_500MA,
	PFM_PEAK_CURRENT_600MA,
};

enum rdson_fet_size {
	RDSON_QUARTER,
	RDSON_HALF,
	RDSON_THREE_FOURTH,
	RDSON_FULLSIZE,
};

enum lcdb_settings_index {
	LCDB_BST_PD_CTL = 0,
	LCDB_RDSON_MGMNT,
	LCDB_MISC_CTL,
	LCDB_SOFT_START_CTL,
	LCDB_PFM_CTL,
	LCDB_PWRUP_PWRDN_CTL,
	LCDB_LDO_PD_CTL,
	LCDB_LDO_SOFT_START_CTL,
	LCDB_NCP_PD_CTL,
	LCDB_NCP_SOFT_START_CTL,
	LCDB_BST_SS_CTL,
	LCDB_LDO_VREG_OK_CTL,
	LCDB_STEPPER_VOUT_CTL,
	LCDB_CONFIG_SEL,
	PM7325B_LCDB_BST_VREG_OK_CTL,
	PM7325B_LCDB_WARMUP_DLY_SEL_1,
	PM7325B_LCDB_WARMUP_DLY_SEL_2,
	PM7325B_LCDB_PRECHARGE_CTL,
	LCDB_SETTING_MAX,
};

enum lcdb_wa_flags {
	NCP_SCP_DISABLE_WA = BIT(0),
	FORCE_PD_ENABLE_WA = BIT(1),
};

static const u32 soft_start_us[] = {
	0,
	500,
	1000,
	2000,
};

static const u32 dbc_us[] = {
	2,
	4,
	16,
	32,
};

static const u32 ncp_ilim_ma[] = {
	260,
	460,
	640,
	810,
};

static const u32 pwrup_pwrdn_ms[] = {
	0,
	1,
	4,
	8,
};

static const u32 ncp_dbc_us[] = {
	64,
	128,
	256,
	512,
};

static const u32 bst_dbc_us[] = {
	4,
	8,
	16,
	32,
};

static const u32 pm7325b_ncp_ilim_ma[] = {
	700,
	800,
	900,
	1000,
};

static const u32 pm7325b_ldo_ilim_ma[] = {
	35,
	175,
	280,
	420,
	455,
	595,
	700,
	840,
};

static const u32 pm7325b_p2_blk_ns[] = {
	40,
	69,
	99,
	129,
	159,
	189,
	220,
	250,
};

#define SETTING(_id, _sec_access, _valid)	\
	[_id] = {				\
		.address = _id##_REG,		\
		.sec_access = _sec_access,	\
		.valid = _valid			\
	}					\

static int qpnp_lcdb_set_voltage_step(struct qpnp_lcdb *lcdb,
				      int voltage_start_mv, u8 type);

static int qpnp_lcdb_set_voltage(struct qpnp_lcdb *lcdb,
				 int voltage_mv, u8 type);

static bool is_between(int value, int min, int max)
{
	if (value < min || value > max)
		return false;
	return true;
}

static int qpnp_lcdb_read(struct qpnp_lcdb *lcdb,
			u16 addr, u8 *value, u8 count)
{
	int rc = 0;

	mutex_lock(&lcdb->read_write_mutex);
	rc = regmap_bulk_read(lcdb->regmap, addr, value, count);
	if (rc < 0)
		pr_err("Failed to read from addr=0x%02x rc=%d\n", addr, rc);
	mutex_unlock(&lcdb->read_write_mutex);

	return rc;
}

static int qpnp_lcdb_write(struct qpnp_lcdb *lcdb,
			u16 addr, u8 *value, u8 count)
{
	int rc;

	mutex_lock(&lcdb->read_write_mutex);
	rc = regmap_bulk_write(lcdb->regmap, addr, value, count);
	if (rc < 0)
		pr_err("Failed to write to addr=0x%02x rc=%d\n", addr, rc);
	mutex_unlock(&lcdb->read_write_mutex);

	return rc;
}

#define SEC_ADDRESS_REG			0xD0
#define SECURE_UNLOCK_VALUE		0xA5
static int qpnp_lcdb_secure_write(struct qpnp_lcdb *lcdb,
					u16 addr, u8 value)
{
	int rc;
	u8 val = SECURE_UNLOCK_VALUE;

	mutex_lock(&lcdb->read_write_mutex);
	if (lcdb->subtype == PM660L) {
		rc = regmap_write(lcdb->regmap, lcdb->base + SEC_ADDRESS_REG,
				  val);
		if (rc < 0) {
			pr_err("Failed to unlock register rc=%d\n", rc);
			goto fail_write;
		}
	}
	rc = regmap_write(lcdb->regmap, addr, value);
	if (rc < 0)
		pr_err("Failed to write to addr=0x%02x rc=%d\n", addr, rc);

fail_write:
	mutex_unlock(&lcdb->read_write_mutex);
	return rc;
}

static int qpnp_lcdb_masked_write(struct qpnp_lcdb *lcdb,
				u16 addr, u8 mask, u8 value)
{
	int rc = 0;

	mutex_lock(&lcdb->read_write_mutex);
	rc = regmap_update_bits(lcdb->regmap, addr, mask, value);
	if (rc < 0)
		pr_err("Failed to write addr=0x%02x value=0x%02x rc=%d\n",
			addr, value, rc);
	mutex_unlock(&lcdb->read_write_mutex);

	return rc;
}

static bool is_lcdb_enabled(struct qpnp_lcdb *lcdb)
{
	int rc;
	u8 val = 0;

	rc = qpnp_lcdb_read(lcdb, lcdb->base + LCDB_ENABLE_CTL1_REG, &val, 1);
	if (rc < 0)
		pr_err("Failed to read ENABLE_CTL1 rc=%d\n", rc);

	return rc ? false : !!(val & MODULE_EN_BIT);
}

static int dump_status_registers(struct qpnp_lcdb *lcdb)
{
	int rc = 0, len = (lcdb->subtype == PM7325B) ? 5 : 6;
	u8 sts[6] = {0};

	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_STS1_REG, &sts[0], len);
	if (rc < 0) {
		pr_err("Failed to write to STS registers rc=%d\n", rc);
	} else {
		rc = qpnp_lcdb_read(lcdb, lcdb->base + LCDB_STS1_REG, sts, len);
		if (rc < 0)
			pr_err("Failed to read lcdb status rc=%d\n", rc);
		else {
			pr_err("STS1=0x%02x STS2=0x%02x STS3=0x%02x STS4=0x%02x STS5=0x%02x\n",
				sts[0], sts[1], sts[2], sts[3], sts[4]);
			if (lcdb->subtype != PM7325B)
				pr_err("STS6=0x%02x\n", sts[5]);
		}
	}

	return rc;
}

static struct settings lcdb_settings_pm660l[] = {
	SETTING(LCDB_BST_PD_CTL, false, true),
	SETTING(LCDB_RDSON_MGMNT, false, true),
	SETTING(LCDB_MISC_CTL, false, true),
	SETTING(LCDB_SOFT_START_CTL, false, true),
	SETTING(LCDB_PFM_CTL, false, true),
	SETTING(LCDB_PWRUP_PWRDN_CTL, true, true),
	SETTING(LCDB_LDO_PD_CTL, false, true),
	SETTING(LCDB_LDO_SOFT_START_CTL, false, true),
	SETTING(LCDB_NCP_PD_CTL, false, true),
	SETTING(LCDB_NCP_SOFT_START_CTL, false, true),
	SETTING(LCDB_BST_SS_CTL, false, false),
	SETTING(LCDB_LDO_VREG_OK_CTL, false, false),
};

/* For PMICs like pmi632/pm8150L */
static struct settings lcdb_settings[] = {
	SETTING(LCDB_BST_PD_CTL, false, true),
	SETTING(LCDB_RDSON_MGMNT, false, false),
	SETTING(LCDB_MISC_CTL, false, false),
	SETTING(LCDB_SOFT_START_CTL, false, false),
	SETTING(LCDB_PFM_CTL, false, false),
	SETTING(LCDB_PWRUP_PWRDN_CTL, false, true),
	SETTING(LCDB_LDO_PD_CTL, false, true),
	SETTING(LCDB_LDO_SOFT_START_CTL, false, true),
	SETTING(LCDB_NCP_PD_CTL, false, true),
	SETTING(LCDB_NCP_SOFT_START_CTL, false, true),
	SETTING(LCDB_BST_SS_CTL, false, true),
	SETTING(LCDB_LDO_VREG_OK_CTL, false, true),
};

static struct settings lcdb_settings_pm7325b[] = {
	SETTING(LCDB_BST_PD_CTL, false, true),
	SETTING(LCDB_RDSON_MGMNT, false, false),
	SETTING(LCDB_MISC_CTL, false, false),
	SETTING(LCDB_SOFT_START_CTL, false, false),
	SETTING(LCDB_PFM_CTL, false, false),
	SETTING(LCDB_PWRUP_PWRDN_CTL, false, false),
	SETTING(LCDB_LDO_PD_CTL, false, true),
	SETTING(LCDB_LDO_SOFT_START_CTL, false, true),
	SETTING(LCDB_NCP_PD_CTL, false, true),
	SETTING(LCDB_NCP_SOFT_START_CTL, false, true),
	SETTING(LCDB_BST_SS_CTL, false, true),
	SETTING(LCDB_LDO_VREG_OK_CTL, false, false),
	SETTING(PM7325B_LCDB_BST_VREG_OK_CTL, false, true),
	SETTING(LCDB_STEPPER_VOUT_CTL, false, true),
	SETTING(LCDB_CONFIG_SEL, false, true),
	SETTING(PM7325B_LCDB_WARMUP_DLY_SEL_1, false, true),
	SETTING(PM7325B_LCDB_WARMUP_DLY_SEL_2, false, true),
	SETTING(PM7325B_LCDB_PRECHARGE_CTL, false, true),
};

static int qpnp_lcdb_save_settings(struct qpnp_lcdb *lcdb)
{
	int i, size, rc = 0;
	struct settings *setting;

	switch (lcdb->subtype) {
	case PM660L:
		setting = lcdb_settings_pm660l;
		size = ARRAY_SIZE(lcdb_settings_pm660l);
		break;
	case PM7325B:
		setting = lcdb_settings_pm7325b;
		size = ARRAY_SIZE(lcdb_settings_pm7325b);
		break;
	default:
		setting = lcdb_settings;
		size = ARRAY_SIZE(lcdb_settings);
		break;
	}

	for (i = 0; i < size; i++) {
		if (setting[i].valid) {
			rc = qpnp_lcdb_read(lcdb, lcdb->base +
					    setting[i].address,
					    &setting[i].value, 1);
			if (rc < 0) {
				pr_err("Failed to read lcdb register address=%x\n",
					setting[i].address);
				return rc;
			}
		}
	}

	return 0;
}

static int qpnp_lcdb_restore_settings(struct qpnp_lcdb *lcdb)
{
	int i, size, rc = 0;
	struct settings *setting;

	switch (lcdb->subtype) {
	case PM660L:
		setting = lcdb_settings_pm660l;
		size = ARRAY_SIZE(lcdb_settings_pm660l);
		break;
	case PM7325B:
		setting = lcdb_settings_pm7325b;
		size = ARRAY_SIZE(lcdb_settings_pm7325b);
		break;
	default:
		setting = lcdb_settings;
		size = ARRAY_SIZE(lcdb_settings);
		break;
	}

	for (i = 0; i < size; i++) {
		if (setting[i].valid) {
			if (setting[i].sec_access)
				rc = qpnp_lcdb_secure_write(lcdb, lcdb->base +
							    setting[i].address,
							    setting[i].value);
			else
				rc = qpnp_lcdb_write(lcdb, lcdb->base +
						     setting[i].address,
						     &setting[i].value, 1);
			if (rc < 0) {
				pr_err("Failed to write register address=%x\n",
					     setting[i].address);
				return rc;
			}
		}
	}

	return 0;
}

static int qpnp_lcdb_ttw_enter(struct qpnp_lcdb *lcdb)
{
	int rc;
	u8 val;

	if (!lcdb->settings_saved) {
		rc = qpnp_lcdb_save_settings(lcdb);
		if (rc < 0) {
			pr_err("Failed to save LCDB settings rc=%d\n", rc);
			return rc;
		}
		lcdb->settings_saved = true;
	}

	val = (BST_SS_TIME_OVERRIDE_1MS << BST_SS_TIME_OVERRIDE_SHIFT) |
	      (DIS_BST_PRECHG_SHORT_ALARM << BST_PRECHG_SHORT_ALARM_SHIFT);
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_BST_SS_CTL_REG, &val, 1);
	if (rc < 0)
		return rc;

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_NCP_SOFT_START_CTL_REG,
			     &val, 1);
	if (rc < 0)
		return rc;

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_LDO_SOFT_START_CTL_REG,
			     &val, 1);
	if (rc < 0)
		return rc;

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_PWRUP_PWRDN_CTL_REG,
			     &val, 1);
	if (rc < 0)
		return rc;

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_BST_VREG_OK_CTL_REG,
			     &val, 1);
	if (rc < 0)
		return rc;

	val = BOOST_DIS_PULLDOWN_BIT;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_BST_PD_CTL_REG,
			     &val, 1);
	if (rc < 0)
		return rc;

	val = LDO_DIS_PULLDOWN_BIT;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_LDO_PD_CTL_REG,
							&val, 1);
	if (rc < 0)
		return rc;

	val = NCP_DIS_PULLDOWN_BIT;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_NCP_PD_CTL_REG,
			     &val, 1);
	if (rc < 0)
		return rc;

	val = HWEN_RDY_BIT;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_ENABLE_CTL1_REG,
			     &val, 1);

	return rc;
}

static int qpnp_lcdb_ttw_enter_pm660l(struct qpnp_lcdb *lcdb)
{
	int rc;
	u8 val;

	if (!lcdb->settings_saved) {
		rc = qpnp_lcdb_save_settings(lcdb);
		if (rc < 0) {
			pr_err("Failed to save LCDB settings rc=%d\n", rc);
			return rc;
		}
		lcdb->settings_saved = true;
	}

	val = BOOST_DIS_PULLDOWN_BIT;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_BST_PD_CTL_REG,
							&val, 1);
	if (rc < 0) {
		pr_err("Failed to set BST PD rc=%d\n", rc);
		return rc;
	}

	val = (RDSON_HALF << NFET_SW_SIZE_SHIFT) | RDSON_HALF;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_RDSON_MGMNT_REG,
							&val, 1);
	if (rc < 0) {
		pr_err("Failed to set RDSON MGMT rc=%d\n", rc);
		return rc;
	}

	val = AUTO_GM_EN_BIT | EN_TOUCH_WAKE_BIT | DIS_SCP_BIT;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_MISC_CTL_REG,
							&val, 1);
	if (rc < 0) {
		pr_err("Failed to set MISC CTL rc=%d\n", rc);
		return rc;
	}

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_SOFT_START_CTL_REG,
						&val, 1);
	if (rc < 0) {
		pr_err("Failed to set LCDB_SOFT_START rc=%d\n", rc);
		return rc;
	}

	val = EN_PFM_BIT | (PFM_HYST_25MV << PFM_HYSTERESIS_SHIFT) |
		     (PFM_PEAK_CURRENT_400MA << PFM_CURRENT_SHIFT) |
				BYP_BST_SOFT_START_COMP_BIT;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_PFM_CTL_REG,
							&val, 1);
	if (rc < 0) {
		pr_err("Failed to set PFM_CTL rc=%d\n", rc);
		return rc;
	}

	val = 0;
	rc = qpnp_lcdb_secure_write(lcdb, lcdb->base + LCDB_PWRUP_PWRDN_CTL_REG,
							val);
	if (rc < 0) {
		pr_err("Failed to set PWRUP_PWRDN_CTL rc=%d\n", rc);
		return rc;
	}

	val = LDO_DIS_PULLDOWN_BIT;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_LDO_PD_CTL_REG,
							&val, 1);
	if (rc < 0) {
		pr_err("Failed to set LDO_PD_CTL rc=%d\n", rc);
		return rc;
	}

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_LDO_SOFT_START_CTL_REG,
							&val, 1);
	if (rc < 0) {
		pr_err("Failed to set LDO_SOFT_START rc=%d\n", rc);
		return rc;
	}

	val = NCP_DIS_PULLDOWN_BIT;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_NCP_PD_CTL_REG,
							&val, 1);
	if (rc < 0) {
		pr_err("Failed to set NCP_PD_CTL rc=%d\n", rc);
		return rc;
	}

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_NCP_SOFT_START_CTL_REG,
							&val, 1);
	if (rc < 0) {
		pr_err("Failed to set NCP_SOFT_START rc=%d\n", rc);
		return rc;
	}

	if (lcdb->ttw_mode_sw) {
		rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_AUTO_TOUCH_WAKE_CTL_REG,
				EN_AUTO_TOUCH_WAKE_BIT,
				EN_AUTO_TOUCH_WAKE_BIT);
		if (rc < 0)
			pr_err("Failed to enable auto(sw) TTW\n rc = %d\n", rc);
	} else {
		val = HWEN_RDY_BIT;
		rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_ENABLE_CTL1_REG,
							&val, 1);
		if (rc < 0)
			pr_err("Failed to hw_enable lcdb rc= %d\n", rc);
	}

	return rc;
}

static int qpnp_lcdb_ttw_enter_pm7325b(struct qpnp_lcdb *lcdb)
{
	int rc;
	u8 val;

	if (!lcdb->settings_saved) {
		rc = qpnp_lcdb_save_settings(lcdb);
		if (rc < 0) {
			pr_err("Failed to save LCDB settings rc=%d\n", rc);
			return rc;
		}
		lcdb->settings_saved = true;
	}

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_BST_PD_CTL_REG,
			     &val, 1);
	if (rc < 0)
		return rc;

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + PM7325B_LCDB_BST_VREG_OK_CTL_REG,
			     &val, 1);
	if (rc < 0)
		return rc;

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + PM7325B_LCDB_WARMUP_DLY_SEL_1_REG,
			     &val, 1);
	if (rc < 0)
		return rc;

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + PM7325B_LCDB_WARMUP_DLY_SEL_2_REG,
			     &val, 1);
	if (rc < 0)
		return rc;

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + PM7325B_LCDB_PRECHARGE_CTL_REG,
			     &val, 1);
	if (rc < 0)
		return rc;

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_BST_SS_CTL_REG, &val, 1);
	if (rc < 0)
		return rc;

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_LDO_PD_CTL_REG,
							&val, 1);
	if (rc < 0)
		return rc;

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_LDO_SOFT_START_CTL_REG,
			     &val, 1);
	if (rc < 0)
		return rc;

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_NCP_PD_CTL_REG,
			     &val, 1);
	if (rc < 0)
		return rc;

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_NCP_SOFT_START_CTL_REG,
			     &val, 1);
	if (rc < 0)
		return rc;

	val = VOUT_STEP_DLY_2US;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_STEPPER_VOUT_CTL_REG,
			     &val, 1);
	if (rc < 0)
		return rc;

	val = EN_FAST_STARTUP_BIT;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_CONFIG_SEL_REG,
			     &val, 1);
	if (rc < 0)
		return rc;

	val = HWEN_RDY_BIT;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_ENABLE_CTL1_REG,
			     &val, 1);

	return rc;
}

static int qpnp_lcdb_ttw_exit(struct qpnp_lcdb *lcdb)
{
	int rc;

	if (lcdb->settings_saved) {
		rc = qpnp_lcdb_restore_settings(lcdb);
		if (rc < 0) {
			pr_err("Failed to restore lcdb settings rc=%d\n", rc);
			return rc;
		}
		lcdb->settings_saved = false;
	}

	return 0;
}

static int qpnp_lcdb_enable_wa(struct qpnp_lcdb *lcdb)
{
	int rc;
	u8 val = 0;

	/* required only for PM660L */
	if (lcdb->subtype != PM660L)
		return 0;

	val = MODULE_EN_BIT;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_ENABLE_CTL1_REG,
						&val, 1);
	if (rc < 0) {
		pr_err("Failed to enable lcdb rc= %d\n", rc);
		return rc;
	}

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_ENABLE_CTL1_REG,
							&val, 1);
	if (rc < 0) {
		pr_err("Failed to disable lcdb rc= %d\n", rc);
		return rc;
	}

	return 0;
}

#define VOLTAGE_START_MV	4500
#define VOLTAGE_STEP_MV		500

static int qpnp_lcdb_enable(struct qpnp_lcdb *lcdb)
{
	int rc = 0, timeout, delay;
	int voltage_mv = VOLTAGE_START_MV;
	u8 val = 0;

	if (lcdb->lcdb_enabled || lcdb->lcdb_sc_disable) {
		pr_debug("lcdb_enabled=%d lcdb_sc_disable=%d\n",
			lcdb->lcdb_enabled, lcdb->lcdb_sc_disable);
		return 0;
	}

	if (lcdb->ttw_enable) {
		rc = qpnp_lcdb_ttw_exit(lcdb);
		if (rc < 0) {
			pr_err("Failed to exit TTW mode rc=%d\n", rc);
			return rc;
		}
	}

	rc = qpnp_lcdb_enable_wa(lcdb);
	if (rc < 0) {
		pr_err("Failed to execute enable_wa rc=%d\n", rc);
		return rc;
	}

	if (lcdb->voltage_step_ramp) {
		if (lcdb->ldo.voltage_mv < VOLTAGE_START_MV)
			voltage_mv = lcdb->ldo.voltage_mv;

		rc = qpnp_lcdb_set_voltage(lcdb, voltage_mv, LDO);
		if (rc < 0)
			return rc;

		if (lcdb->ncp.voltage_mv < VOLTAGE_START_MV)
			voltage_mv = lcdb->ncp.voltage_mv;

		rc = qpnp_lcdb_set_voltage(lcdb, voltage_mv, NCP);
		if (rc < 0)
			return rc;
	}

	val = MODULE_EN_BIT;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_ENABLE_CTL1_REG,
							&val, 1);
	if (rc < 0) {
		pr_err("Failed to disable lcdb rc= %d\n", rc);
		goto fail_enable;
	}

	/* poll for vreg_ok */
	timeout = 10;
	delay = lcdb->bst.soft_start_us + lcdb->ldo.soft_start_us +
					lcdb->ncp.soft_start_us;
	delay += lcdb->bst.vreg_ok_dbc_us + lcdb->ldo.vreg_ok_dbc_us +
					lcdb->ncp.vreg_ok_dbc_us;
	while (timeout--) {
		rc = qpnp_lcdb_read(lcdb, lcdb->base + INT_RT_STATUS_REG,
								&val, 1);
		if (rc < 0) {
			pr_err("Failed to poll for vreg-ok status rc=%d\n", rc);
			break;
		}
		if (val & VREG_OK_RT_STS_BIT)
			break;

		usleep_range(delay, delay + 100);
	}

	if (rc || !timeout) {
		if (!timeout) {
			pr_err("lcdb-vreg-ok status failed to change\n");
			rc = -ETIMEDOUT;
		}
		goto fail_enable;
	}

	lcdb->lcdb_enabled = true;
	if (lcdb->voltage_step_ramp) {
		usleep_range(10000, 11000);
		rc = qpnp_lcdb_set_voltage_step(lcdb,
						voltage_mv + VOLTAGE_STEP_MV,
						LDO_NCP);
		if (rc < 0) {
			pr_err("Failed to set LCDB voltage rc=%d\n", rc);
			return rc;
		}
	}

	pr_debug("lcdb enabled successfully!\n");

	return 0;

fail_enable:
	dump_status_registers(lcdb);
	pr_err("Failed to enable lcdb rc=%d\n", rc);
	return rc;
}

static int qpnp_lcdb_disable(struct qpnp_lcdb *lcdb)
{
	int rc = 0;
	u8 val = 0;

	if (!lcdb->lcdb_enabled)
		return 0;

	if (lcdb->ttw_enable) {
		switch (lcdb->subtype) {
		case PM660L:
			rc = qpnp_lcdb_ttw_enter_pm660l(lcdb);
			break;
		case PM7325B:
			rc = qpnp_lcdb_ttw_enter_pm7325b(lcdb);
			break;
		default:
			rc = qpnp_lcdb_ttw_enter(lcdb);
			break;
		}

		if (rc < 0) {
			pr_err("Failed to enable TTW mode rc=%d\n", rc);
			return rc;
		}
		lcdb->lcdb_enabled = false;

		return 0;
	}

	if (lcdb->wa_flags & FORCE_PD_ENABLE_WA) {
		/*
		 * force pull-down to enable quick discharge after
		 * turning off
		 */
		val = LDO_FORCE_PD_EN_BIT | LDO_FORCE_PD_MODE;
		rc = qpnp_lcdb_write(lcdb, lcdb->base +
				     LCDB_LDO_FORCE_PD_CTL_REG, &val, 1);
		if (rc < 0)
			return rc;
	}

	val = 0;
	rc = qpnp_lcdb_write(lcdb, lcdb->base + LCDB_ENABLE_CTL1_REG,
							&val, 1);
	if (rc < 0)
		pr_err("Failed to disable lcdb rc= %d\n", rc);
	else
		lcdb->lcdb_enabled = false;

	if (lcdb->wa_flags & FORCE_PD_ENABLE_WA) {
		/* wait for 10 msec after module disable for LDO to discharge */
		usleep_range(10000, 11000);

		val = 0;
		rc = qpnp_lcdb_write(lcdb, lcdb->base +
				     LCDB_LDO_FORCE_PD_CTL_REG, &val, 1);
		if (rc < 0)
			return rc;
	}

	return rc;
}

#define LCDB_SC_RESET_CNT_DLY_US	1000000
#define LCDB_SC_CNT_MAX			10
static int qpnp_lcdb_handle_sc_event(struct qpnp_lcdb *lcdb)
{
	int rc = 0;
	s64 elapsed_time_us;

	mutex_lock(&lcdb->lcdb_mutex);
	rc = qpnp_lcdb_disable(lcdb);
	if (rc < 0) {
		pr_err("Failed to disable lcdb rc=%d\n", rc);
		goto unlock_mutex;
	}

	/* Check if the SC re-occurred immediately */
	elapsed_time_us = ktime_us_delta(ktime_get(),
			lcdb->sc_module_enable_time);
	if (elapsed_time_us > LCDB_SC_RESET_CNT_DLY_US) {
		lcdb->sc_count = 0;
	} else if (lcdb->sc_count > LCDB_SC_CNT_MAX) {
		pr_err("SC triggered %d times, disabling LCDB forever!\n",
						lcdb->sc_count);
		lcdb->lcdb_sc_disable = true;
		goto unlock_mutex;
	}
	lcdb->sc_count++;
	lcdb->sc_module_enable_time = ktime_get();

	/* delay for SC to clear */
	usleep_range(10000, 10100);

	rc = qpnp_lcdb_enable(lcdb);
	if (rc < 0)
		pr_err("Failed to enable lcdb rc=%d\n", rc);

unlock_mutex:
	mutex_unlock(&lcdb->lcdb_mutex);
	return rc;
}

static irqreturn_t qpnp_lcdb_sc_irq_handler(int irq, void *data)
{
	struct qpnp_lcdb *lcdb = data;
	int rc;
	u8 val, val2[2] = {0};

	mutex_lock(&lcdb->lcdb_mutex);
	rc = qpnp_lcdb_read(lcdb, lcdb->base + INT_RT_STATUS_REG, &val, 1);
	mutex_unlock(&lcdb->lcdb_mutex);
	if (rc < 0)
		goto irq_handled;

	if (val & SC_ERROR_RT_STS_BIT) {
		rc = qpnp_lcdb_read(lcdb,
			lcdb->base + LCDB_MISC_CTL_REG, &val, 1);
		if (rc < 0)
			goto irq_handled;

		if (lcdb->subtype == PM660L &&
				(val & EN_TOUCH_WAKE_BIT)) {
			/* blanking time */
			usleep_range(300, 310);
			/*
			 * The status registers need to written with any value
			 * before reading
			 */
			rc = qpnp_lcdb_write(lcdb,
				lcdb->base + LCDB_STS3_REG, val2, 2);
			if (rc < 0)
				goto irq_handled;

			rc = qpnp_lcdb_read(lcdb,
				lcdb->base + LCDB_STS3_REG, val2, 2);
			if (rc < 0)
				goto irq_handled;

			if (!(val2[0] & LDO_VREG_OK_BIT) ||
					!(val2[1] & NCP_VREG_OK_BIT)) {
				rc = qpnp_lcdb_handle_sc_event(lcdb);
				if (rc < 0) {
					pr_err("Failed to handle SC rc=%d\n",
								rc);
					goto irq_handled;
				}
			}
		} else {
			/* blanking time */
			usleep_range(2000, 2100);
			/* Read the SC status again to confirm true SC */
			mutex_lock(&lcdb->lcdb_mutex);
			/*
			 * Wait for the completion of LCDB module enable,
			 * which could be initiated in a previous SC event,
			 * to avoid multiple module disable/enable calls.
			 */
			rc = qpnp_lcdb_read(lcdb,
				lcdb->base + INT_RT_STATUS_REG, &val, 1);
			mutex_unlock(&lcdb->lcdb_mutex);
			if (rc < 0)
				goto irq_handled;

			if (val & SC_ERROR_RT_STS_BIT) {
				rc = qpnp_lcdb_handle_sc_event(lcdb);
				if (rc < 0) {
					pr_err("Failed to handle SC rc=%d\n",
								rc);
					goto irq_handled;
				}
			}
		}
	}
irq_handled:
	return IRQ_HANDLED;
}

#define MIN_BST_VOLTAGE_MV			4700
#define PM7325B_MIN_BST_VOLTAGE_MV		2000
#define PM660_MAX_BST_VOLTAGE_MV		6250
#define MAX_BST_VOLTAGE_MV			6275
#define PM7325B_MAX_BST_VOLTAGE_MV			6400
#define MIN_VOLTAGE_MV				4000
#define MAX_VOLTAGE_MV				6000
#define PM7325B_MIN_VOLTAGE_MV				4400
#define PM7325B_MAX_VOLTAGE_MV				6000
#define VOLTAGE_MIN_STEP_100_MV			4000
#define VOLTAGE_MIN_STEP_50_MV			4950
#define VOLTAGE_STEP_100_MV			100
#define VOLTAGE_STEP_50_MV			50
#define VOLTAGE_STEP_25_MV			25
#define VOLTAGE_STEP_50MV_OFFSET		0xA
static int qpnp_lcdb_set_bst_voltage(struct qpnp_lcdb *lcdb,
					int voltage_mv, u8 type)
{
	int rc = 0;
	u8 val, voltage_step, mask = 0;
	int bst_voltage_mv, min_bst_voltage;
	struct ldo_regulator *ldo = &lcdb->ldo;
	struct ncp_regulator *ncp = &lcdb->ncp;
	struct bst_params *bst = &lcdb->bst;

	/* Vout_Boost = headroom_mv + max( Vout_LDO, abs (Vout_NCP)) */
	bst_voltage_mv = max(voltage_mv, max(ldo->voltage_mv, ncp->voltage_mv));
	bst_voltage_mv += bst->headroom_mv;

	if (bst_voltage_mv < MIN_BST_VOLTAGE_MV)
		bst_voltage_mv = MIN_BST_VOLTAGE_MV;

	switch (lcdb->subtype) {
	case PM660L:
		if (bst_voltage_mv > PM660_MAX_BST_VOLTAGE_MV)
			bst_voltage_mv = PM660_MAX_BST_VOLTAGE_MV;
		break;
	case PM7325B:
		if (bst_voltage_mv > PM7325B_MAX_BST_VOLTAGE_MV)
			bst_voltage_mv = PM7325B_MAX_BST_VOLTAGE_MV;
		break;
	default:
		if (bst_voltage_mv > MAX_BST_VOLTAGE_MV)
			bst_voltage_mv = MAX_BST_VOLTAGE_MV;
		break;
	}

	if (bst_voltage_mv != bst->voltage_mv) {
		switch (lcdb->subtype) {
		case PM660L:
			mask = PM660_BST_OUTPUT_VOLTAGE_MASK;
			voltage_step = VOLTAGE_STEP_50_MV;
			min_bst_voltage = MIN_BST_VOLTAGE_MV;
			break;
		case PM7325B:
			mask =  PM7325B_BST_OUTPUT_VOLTAGE_MASK;
			voltage_step = VOLTAGE_STEP_25_MV;
			min_bst_voltage = PM7325B_MIN_BST_VOLTAGE_MV;
			break;
		default:
			mask =  BST_OUTPUT_VOLTAGE_MASK;
			voltage_step = VOLTAGE_STEP_25_MV;
			min_bst_voltage = MIN_BST_VOLTAGE_MV;
			break;
		}

		val = DIV_ROUND_UP(bst_voltage_mv - min_bst_voltage,
							voltage_step);
		rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
					LCDB_BST_OUTPUT_VOLTAGE_REG,
					mask, val);
		if (rc < 0) {
			pr_err("Failed to set boost voltage %d mv rc=%d\n",
				bst_voltage_mv, rc);
		} else {
			pr_debug("Boost voltage set = %d mv (0x%02x = 0x%02x)\n",
			      bst_voltage_mv, LCDB_BST_OUTPUT_VOLTAGE_REG, val);
			bst->voltage_mv = bst_voltage_mv;
		}
	}

	return rc;
}

static int qpnp_lcdb_get_bst_voltage(struct qpnp_lcdb *lcdb,
					int *voltage_mv)
{
	int rc, min_bst_voltage;
	u8 val, voltage_step, mask = 0;

	rc = qpnp_lcdb_read(lcdb, lcdb->base + LCDB_BST_OUTPUT_VOLTAGE_REG,
						&val, 1);
	if (rc < 0) {
		pr_err("Failed to reat BST voltage rc=%d\n", rc);
		return rc;
	}

	switch (lcdb->subtype) {
	case PM660L:
		mask = PM660_BST_OUTPUT_VOLTAGE_MASK;
		voltage_step = VOLTAGE_STEP_50_MV;
		min_bst_voltage = MIN_BST_VOLTAGE_MV;
		break;
	case PM7325B:
		mask =  PM7325B_BST_OUTPUT_VOLTAGE_MASK;
		voltage_step = VOLTAGE_STEP_25_MV;
		min_bst_voltage = PM7325B_MIN_BST_VOLTAGE_MV;
		break;
	default:
		mask =  BST_OUTPUT_VOLTAGE_MASK;
		voltage_step = VOLTAGE_STEP_25_MV;
		min_bst_voltage = MIN_BST_VOLTAGE_MV;
	}

	val &= mask;
	*voltage_mv = (val * voltage_step) + min_bst_voltage;

	return 0;
}

static int qpnp_lcdb_set_voltage(struct qpnp_lcdb *lcdb,
					int voltage_mv, u8 type)
{
	int rc = 0;
	u16 offset = LCDB_LDO_OUTPUT_VOLTAGE_REG;
	u8 val = 0;
	int voltage_mask = (lcdb->subtype == PM7325B) ?
		PM7325B_SET_OUTPUT_VOLTAGE_MASK : SET_OUTPUT_VOLTAGE_MASK;

	if (!is_between(voltage_mv, lcdb->min_voltage_mv, lcdb->max_voltage_mv)) {
		pr_err("Invalid voltage %dmv (min=%d max=%d)\n",
			voltage_mv, lcdb->min_voltage_mv, lcdb->max_voltage_mv);
		return -EINVAL;
	}

	rc = qpnp_lcdb_set_bst_voltage(lcdb, voltage_mv, type);
	if (rc < 0) {
		pr_err("Failed to set boost voltage rc=%d\n", rc);
		return rc;
	}

	/* Below logic is only valid for LDO and NCP type */
	if (voltage_mv < VOLTAGE_MIN_STEP_50_MV) {
		val = DIV_ROUND_UP(voltage_mv - VOLTAGE_MIN_STEP_100_MV,
						VOLTAGE_STEP_100_MV);
	} else {
		val = DIV_ROUND_UP(voltage_mv - VOLTAGE_MIN_STEP_50_MV,
						VOLTAGE_STEP_50_MV);
		val += VOLTAGE_STEP_50MV_OFFSET;
	}

	if (type == NCP)
		offset = LCDB_NCP_OUTPUT_VOLTAGE_REG;

	rc = qpnp_lcdb_masked_write(lcdb, lcdb->base + offset,
				voltage_mask, val);
	if (rc < 0)
		pr_err("Failed to set output voltage %d mv for %s rc=%d\n",
			voltage_mv, (type == LDO) ? "LDO" : "NCP", rc);
	else
		pr_debug("%s voltage set = %d mv (0x%02x = 0x%02x)\n",
			(type == LDO) ? "LDO" : "NCP", voltage_mv, offset, val);

	return rc;
}

static int qpnp_lcdb_set_voltage_step(struct qpnp_lcdb *lcdb,
				      int voltage_start_mv, u8 type)
{
	int i, ldo_voltage, ncp_voltage, voltage, rc = 0;

	for (i = voltage_start_mv; i <= (MAX_VOLTAGE_MV + VOLTAGE_STEP_MV);
						i += VOLTAGE_STEP_MV) {

		ldo_voltage = (lcdb->ldo.voltage_mv < i) ?
					lcdb->ldo.voltage_mv : i;

		ncp_voltage = (lcdb->ncp.voltage_mv < i) ?
					lcdb->ncp.voltage_mv : i;
		if (type == LDO_NCP) {
			rc = qpnp_lcdb_set_voltage(lcdb, ldo_voltage, LDO);
			if (rc < 0)
				return rc;

			rc = qpnp_lcdb_set_voltage(lcdb, ncp_voltage, NCP);
			if (rc < 0)
				return rc;

			pr_debug(" LDO voltage step %d NCP voltage step %d\n",
					ldo_voltage, ncp_voltage);

			if ((i >= lcdb->ncp.voltage_mv) &&
					(i >= lcdb->ldo.voltage_mv))
				break;
		} else {
			voltage = (type == LDO) ? ldo_voltage : ncp_voltage;
			rc = qpnp_lcdb_set_voltage(lcdb, voltage, type);
			if (rc < 0)
				return rc;

			pr_debug("%s voltage step %d\n",
				 (type == LDO) ? "LDO" : "NCP", voltage);
			if ((type == LDO) && (i >= lcdb->ldo.voltage_mv))
				break;

			if ((type == NCP) && (i >= lcdb->ncp.voltage_mv))
				break;

		}

		usleep_range(1000, 1100);
	}

	return rc;
}

static int qpnp_lcdb_get_voltage(struct qpnp_lcdb *lcdb,
					u32 *voltage_mv, u8 type)
{
	int rc = 0;
	u16 offset = LCDB_LDO_OUTPUT_VOLTAGE_REG;
	u8 val = 0;
	int voltage_mask = (lcdb->subtype == PM7325B) ?
		PM7325B_SET_OUTPUT_VOLTAGE_MASK : SET_OUTPUT_VOLTAGE_MASK;

	if (type == BST)
		return qpnp_lcdb_get_bst_voltage(lcdb, voltage_mv);

	/* When symmetry is enabled, NCP voltage directly follows LDO voltage */
	if (type == NCP && !lcdb->ncp_symmetry)
		offset = LCDB_NCP_OUTPUT_VOLTAGE_REG;

	rc = qpnp_lcdb_read(lcdb, lcdb->base + offset, &val, 1);
	if (rc < 0) {
		pr_err("Failed to read %s volatge rc=%d\n",
			(type == LDO) ? "LDO" : "NCP", rc);
		return rc;
	}

	val &= voltage_mask;
	if (val < VOLTAGE_STEP_50MV_OFFSET) {
		*voltage_mv = VOLTAGE_MIN_STEP_100_MV +
				(val * VOLTAGE_STEP_100_MV);
	} else {
		*voltage_mv = VOLTAGE_MIN_STEP_50_MV +
			((val - VOLTAGE_STEP_50MV_OFFSET) * VOLTAGE_STEP_50_MV);
	}

	if (!rc)
		pr_debug("%s voltage read-back = %d mv (0x%02x = 0x%02x)\n",
					(type == LDO) ? "LDO" : "NCP",
					*voltage_mv, offset, val);

	return rc;
}

static int qpnp_lcdb_set_soft_start(struct qpnp_lcdb *lcdb,
					u32 ss_us, u8 type)
{
	int rc = 0, i = 0;
	u16 offset = LCDB_LDO_SOFT_START_CTL_REG;
	u8 val = 0;

	if (type == NCP)
		offset = LCDB_NCP_SOFT_START_CTL_REG;

	if (!is_between(ss_us, MIN_SOFT_START_US, MAX_SOFT_START_US)) {
		pr_err("Invalid soft_start_us %d (min=%d max=%d)\n",
			ss_us, MIN_SOFT_START_US, MAX_SOFT_START_US);
		return -EINVAL;
	}

	i = 0;
	while (ss_us >= soft_start_us[i])
		i++;
	val = ((i == 0) ? 0 : i - 1) & SOFT_START_MASK;

	rc = qpnp_lcdb_masked_write(lcdb,
			lcdb->base + offset, SOFT_START_MASK, val);
	if (rc < 0)
		pr_err("Failed to write %s soft-start time %d rc=%d\n",
			(type == LDO) ? "LDO" : "NCP", soft_start_us[i], rc);

	return rc;
}

static int qpnp_lcdb_ldo_regulator_enable(struct regulator_dev *rdev)
{
	int rc = 0;
	struct qpnp_lcdb *lcdb  = rdev_get_drvdata(rdev);

	mutex_lock(&lcdb->lcdb_mutex);
	rc = qpnp_lcdb_enable(lcdb);
	if (rc < 0)
		pr_err("Failed to enable lcdb rc=%d\n", rc);
	mutex_unlock(&lcdb->lcdb_mutex);

	return rc;
}

static int qpnp_lcdb_ldo_regulator_disable(struct regulator_dev *rdev)
{
	int rc = 0;
	struct qpnp_lcdb *lcdb  = rdev_get_drvdata(rdev);

	mutex_lock(&lcdb->lcdb_mutex);
	rc = qpnp_lcdb_disable(lcdb);
	if (rc < 0)
		pr_err("Failed to disable lcdb rc=%d\n", rc);
	mutex_unlock(&lcdb->lcdb_mutex);

	return rc;
}

static int qpnp_lcdb_ldo_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct qpnp_lcdb *lcdb  = rdev_get_drvdata(rdev);

	return lcdb->lcdb_enabled;
}

static int qpnp_lcdb_ldo_regulator_set_voltage(struct regulator_dev *rdev,
				int min_uV, int max_uV, unsigned int *selector)
{
	int rc = 0;
	struct qpnp_lcdb *lcdb  = rdev_get_drvdata(rdev);

	lcdb->ldo.voltage_mv = min_uV / 1000;
	if (lcdb->voltage_step_ramp)
		rc = qpnp_lcdb_set_voltage_step(lcdb,
			lcdb->ldo.prev_voltage_mv + VOLTAGE_STEP_MV, LDO);
	else
		rc = qpnp_lcdb_set_voltage(lcdb, lcdb->ldo.voltage_mv, LDO);

	if (rc < 0)
		pr_err("Failed to set LDO voltage rc=%c\n", rc);
	else
		lcdb->ldo.prev_voltage_mv = lcdb->ldo.voltage_mv;

	return rc;
}

static int qpnp_lcdb_ldo_regulator_get_voltage(struct regulator_dev *rdev)
{
	int rc = 0;
	u32 voltage_mv = 0;
	struct qpnp_lcdb *lcdb  = rdev_get_drvdata(rdev);

	rc = qpnp_lcdb_get_voltage(lcdb, &voltage_mv, LDO);
	if (rc < 0) {
		pr_err("Failed to get ldo voltage rc=%d\n", rc);
		return rc;
	}

	return voltage_mv * 1000;
}

static const struct regulator_ops qpnp_lcdb_ldo_ops = {
	.enable			= qpnp_lcdb_ldo_regulator_enable,
	.disable		= qpnp_lcdb_ldo_regulator_disable,
	.is_enabled		= qpnp_lcdb_ldo_regulator_is_enabled,
	.set_voltage		= qpnp_lcdb_ldo_regulator_set_voltage,
	.get_voltage		= qpnp_lcdb_ldo_regulator_get_voltage,
};

static int qpnp_lcdb_ncp_regulator_enable(struct regulator_dev *rdev)
{
	int rc = 0;
	struct qpnp_lcdb *lcdb  = rdev_get_drvdata(rdev);

	mutex_lock(&lcdb->lcdb_mutex);
	rc = qpnp_lcdb_enable(lcdb);
	if (rc < 0)
		pr_err("Failed to enable lcdb rc=%d\n", rc);
	mutex_unlock(&lcdb->lcdb_mutex);

	return rc;
}

static int qpnp_lcdb_ncp_regulator_disable(struct regulator_dev *rdev)
{
	int rc = 0;
	struct qpnp_lcdb *lcdb  = rdev_get_drvdata(rdev);

	mutex_lock(&lcdb->lcdb_mutex);
	rc = qpnp_lcdb_disable(lcdb);
	if (rc < 0)
		pr_err("Failed to disable lcdb rc=%d\n", rc);
	mutex_unlock(&lcdb->lcdb_mutex);

	return rc;
}

static int qpnp_lcdb_ncp_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct qpnp_lcdb *lcdb  = rdev_get_drvdata(rdev);

	return lcdb->lcdb_enabled;
}

static int qpnp_lcdb_ncp_regulator_set_voltage(struct regulator_dev *rdev,
				int min_uV, int max_uV, unsigned int *selector)
{
	int rc = 0;
	struct qpnp_lcdb *lcdb  = rdev_get_drvdata(rdev);

	lcdb->ncp.voltage_mv = min_uV / 1000;
	if (lcdb->voltage_step_ramp)
		rc = qpnp_lcdb_set_voltage_step(lcdb,
			lcdb->ncp.prev_voltage_mv + VOLTAGE_STEP_MV, NCP);
	else
		rc = qpnp_lcdb_set_voltage(lcdb, lcdb->ncp.voltage_mv, NCP);

	if (rc < 0)
		pr_err("Failed to set NCP voltage rc=%c\n", rc);
	else
		lcdb->ncp.prev_voltage_mv = lcdb->ncp.voltage_mv;

	return rc;
}

static int qpnp_lcdb_ncp_regulator_get_voltage(struct regulator_dev *rdev)
{
	int rc;
	u32 voltage_mv = 0;
	struct qpnp_lcdb *lcdb  = rdev_get_drvdata(rdev);

	rc = qpnp_lcdb_get_voltage(lcdb, &voltage_mv, NCP);
	if (rc < 0) {
		pr_err("Failed to get ncp voltage rc=%d\n", rc);
		return rc;
	}

	return voltage_mv * 1000;
}

static const struct regulator_ops qpnp_lcdb_ncp_ops = {
	.enable			= qpnp_lcdb_ncp_regulator_enable,
	.disable		= qpnp_lcdb_ncp_regulator_disable,
	.is_enabled		= qpnp_lcdb_ncp_regulator_is_enabled,
	.set_voltage		= qpnp_lcdb_ncp_regulator_set_voltage,
	.get_voltage		= qpnp_lcdb_ncp_regulator_get_voltage,
};

static int qpnp_lcdb_regulator_register(struct qpnp_lcdb *lcdb, u8 type)
{
	int rc = 0, off_on_delay = 0, voltage_step = VOLTAGE_STEP_50_MV;
	struct regulator_init_data *init_data;
	struct regulator_config cfg = {};
	struct regulator_desc *rdesc;
	struct regulator_dev *rdev;
	struct device_node *node;

	if (lcdb->subtype != PM660L)
		off_on_delay = PMIC5_LCDB_OFF_ON_DELAY_US;

	if (type == LDO) {
		node			= lcdb->ldo.node;
		rdesc			= &lcdb->ldo.rdesc;
		rdesc->ops		= &qpnp_lcdb_ldo_ops;
		rdesc->off_on_delay	= off_on_delay;
		rdesc->n_voltages = ((lcdb->max_voltage_mv - lcdb->min_voltage_mv)
					/ voltage_step) + 1;
		rdev			= lcdb->ldo.rdev;
	} else if (type == NCP) {
		node			= lcdb->ncp.node;
		rdesc			= &lcdb->ncp.rdesc;
		rdesc->ops		= &qpnp_lcdb_ncp_ops;
		rdesc->off_on_delay	= off_on_delay;
		rdesc->n_voltages = ((lcdb->max_voltage_mv - lcdb->min_voltage_mv)
					/ voltage_step) + 1;
		rdev			= lcdb->ncp.rdev;
	} else {
		pr_err("Invalid regulator type %d\n", type);
		return -EINVAL;
	}

	init_data = of_get_regulator_init_data(lcdb->dev, node, rdesc);
	if (!init_data) {
		pr_err("Failed to get regulator_init_data for %s\n",
					(type == LDO) ? "LDO" : "NCP");
		return -ENOMEM;
	}

	if (init_data->constraints.name) {
		rdesc->owner		= THIS_MODULE;
		rdesc->type		= REGULATOR_VOLTAGE;
		rdesc->name		= init_data->constraints.name;

		cfg.dev = lcdb->dev;
		cfg.init_data = init_data;
		cfg.driver_data = lcdb;
		cfg.of_node = node;

		if (of_get_property(lcdb->dev->of_node, "parent-supply", NULL))
			init_data->supply_regulator = "parent";

		init_data->constraints.valid_ops_mask
				|= REGULATOR_CHANGE_VOLTAGE
				| REGULATOR_CHANGE_STATUS;

		rdev = devm_regulator_register(lcdb->dev, rdesc, &cfg);
		if (IS_ERR(rdev)) {
			rc = PTR_ERR(rdev);
			rdev = NULL;
			pr_err("Failed to register lcdb_%s regulator rc = %d\n",
				(type == LDO) ? "LDO" : "NCP", rc);
			return rc;
		}
	} else {
		pr_err("%s_regulator name missing\n",
				(type == LDO) ? "LDO" : "NCP");
		return -EINVAL;
	}

	return rc;
}

static int qpnp_lcdb_parse_ttw(struct qpnp_lcdb *lcdb)
{
	int rc = 0;
	u32 temp;
	u8 val = 0;
	struct device_node *node = lcdb->dev->of_node;

	/* LCDB_AUTO_TOUCH_WAKE_CTL_REG is removed for PM7325B, but TTW is supported */
	if (lcdb->subtype == PM7325B)
		return 0;

	if (of_property_read_bool(node, "qcom,ttw-mode-sw")) {
		lcdb->ttw_mode_sw = true;
		rc = of_property_read_u32(node, "qcom,attw-toff-ms", &temp);
		if (!rc) {
			if (!is_between(temp, ATTW_MIN_MS, ATTW_MAX_MS)) {
				pr_err("Invalid TOFF val %d (min=%d max=%d)\n",
					temp, ATTW_MIN_MS, ATTW_MAX_MS);
					return -EINVAL;
			}
			val = ilog2(temp / 4) << ATTW_TOFF_TIME_SHIFT;
		} else {
			pr_err("qcom,attw-toff-ms not specified for TTW SW mode\n");
			return rc;
		}

		rc = of_property_read_u32(node, "qcom,attw-ton-ms", &temp);
		if (!rc) {
			if (!is_between(temp, ATTW_MIN_MS, ATTW_MAX_MS)) {
				pr_err("Invalid TON value %d (min=%d max=%d)\n",
					temp, ATTW_MIN_MS, ATTW_MAX_MS);
				return -EINVAL;
			}
			val |= ilog2(temp / 4);
		} else {
			pr_err("qcom,attw-ton-ms not specified for TTW SW mode\n");
			return rc;
		}
		rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_AUTO_TOUCH_WAKE_CTL_REG,
				ATTW_TON_TIME_MASK | ATTW_TOFF_TIME_MASK, val);
		if (rc < 0) {
			pr_err("Failed to write ATTW ON/OFF rc=%d\n", rc);
			return rc;
		}
	}

	return 0;
}

static int qpnp_lcdb_ldo_dt_init(struct qpnp_lcdb *lcdb)
{
	int rc = 0;
	struct device_node *node = lcdb->ldo.node;
	int ilim_min = (lcdb->subtype == PM7325B) ? PM7325B_MIN_LDO_ILIM_MA : MIN_LDO_ILIM_MA;
	int ilim_max = (lcdb->subtype == PM7325B) ? PM7325B_MAX_LDO_ILIM_MA : MAX_LDO_ILIM_MA;

	/* LDO output voltage */
	lcdb->ldo.voltage_mv = -EINVAL;
	rc = of_property_read_u32(node, "qcom,ldo-voltage-mv",
					&lcdb->ldo.voltage_mv);
	if (!rc && !is_between(lcdb->ldo.voltage_mv, lcdb->min_voltage_mv, lcdb->max_voltage_mv)) {
		pr_err("Invalid LDO voltage %dmv (min=%d max=%d)\n",
			lcdb->ldo.voltage_mv, lcdb->min_voltage_mv, lcdb->max_voltage_mv);
		return -EINVAL;
	}

	/* LDO PD configuration */
	lcdb->ldo.pd = -EINVAL;
	of_property_read_u32(node, "qcom,ldo-pd", &lcdb->ldo.pd);

	lcdb->ldo.pd_strength = -EINVAL;
	of_property_read_u32(node, "qcom,ldo-pd-strength",
					&lcdb->ldo.pd_strength);

	/* LDO ILIM configuration */
	lcdb->ldo.ilim_ma = -EINVAL;
	rc = of_property_read_u32(node, "qcom,ldo-ilim-ma", &lcdb->ldo.ilim_ma);
	if (!rc && !is_between(lcdb->ldo.ilim_ma, ilim_min, ilim_max)) {
		pr_err("Invalid ilim_ma %d (min=%d, max=%d)\n",
			lcdb->ldo.ilim_ma, ilim_min, ilim_max);
		return -EINVAL;
	}

	/* LDO soft-start (SS) configuration */
	lcdb->ldo.soft_start_us = -EINVAL;
	of_property_read_u32(node, "qcom,ldo-soft-start-us",
					&lcdb->ldo.soft_start_us);

	return 0;
}

static int qpnp_lcdb_ncp_dt_init(struct qpnp_lcdb *lcdb)
{
	int rc = 0;
	struct device_node *node = lcdb->ncp.node;
	int ilim_min = (lcdb->subtype == PM7325B) ? PM7325B_MIN_NCP_ILIM_MA : MIN_NCP_ILIM_MA;
	int ilim_max = (lcdb->subtype == PM7325B) ? PM7325B_MAX_NCP_ILIM_MA : MAX_NCP_ILIM_MA;

	/* NCP output voltage */
	lcdb->ncp.voltage_mv = -EINVAL;
	rc = of_property_read_u32(node, "qcom,ncp-voltage-mv",
					&lcdb->ncp.voltage_mv);
	if (!rc && !is_between(lcdb->ncp.voltage_mv, lcdb->min_voltage_mv, lcdb->max_voltage_mv)) {
		pr_err("Invalid NCP voltage %dmv (min=%d max=%d)\n",
			lcdb->ldo.voltage_mv, lcdb->min_voltage_mv, lcdb->max_voltage_mv);
		return -EINVAL;
	}

	/* NCP PD configuration */
	lcdb->ncp.pd = -EINVAL;
	of_property_read_u32(node, "qcom,ncp-pd", &lcdb->ncp.pd);

	lcdb->ncp.pd_strength = -EINVAL;
	of_property_read_u32(node, "qcom,ncp-pd-strength",
					&lcdb->ncp.pd_strength);

	/* NCP ILIM configuration */
	lcdb->ncp.ilim_ma = -EINVAL;
	rc = of_property_read_u32(node, "qcom,ncp-ilim-ma", &lcdb->ncp.ilim_ma);
	if (!rc && !is_between(lcdb->ncp.ilim_ma, ilim_min, ilim_max)) {
		pr_err("Invalid ilim_ma %d (min=%d, max=%d)\n",
			lcdb->ncp.ilim_ma, ilim_min, ilim_max);
		return -EINVAL;
	}

	/* NCP soft-start (SS) configuration */
	lcdb->ncp.soft_start_us = -EINVAL;
	of_property_read_u32(node, "qcom,ncp-soft-start-us",
					&lcdb->ncp.soft_start_us);

	return 0;
}

static int qpnp_lcdb_bst_dt_init(struct qpnp_lcdb *lcdb)
{
	int rc = 0;
	struct device_node *node = lcdb->bst.node;
	u16 default_headroom_mv;
	int ilim_min = (lcdb->subtype == PM7325B) ? PM7325B_MIN_BST_ILIM_MA : MIN_BST_ILIM_MA;
	int ilim_max = (lcdb->subtype == PM7325B) ? PM7325B_MAX_BST_ILIM_MA : MAX_BST_ILIM_MA;

	/* Boost PD  configuration */
	lcdb->bst.pd = -EINVAL;
	of_property_read_u32(node, "qcom,bst-pd", &lcdb->bst.pd);

	lcdb->bst.pd_strength = -EINVAL;
	of_property_read_u32(node, "qcom,bst-pd-strength",
					&lcdb->bst.pd_strength);

	/* Boost ILIM */
	lcdb->bst.ilim_ma = -EINVAL;
	rc = of_property_read_u32(node, "qcom,bst-ilim-ma", &lcdb->bst.ilim_ma);
	if (!rc && !is_between(lcdb->bst.ilim_ma, ilim_min, ilim_max)) {
		pr_err("Invalid ilim_ma %d (min=%d, max=%d)\n",
			lcdb->bst.ilim_ma, ilim_min, ilim_max);
			return -EINVAL;
	}

	/* Boost PS configuration */
	lcdb->bst.ps = -EINVAL;
	of_property_read_u32(node, "qcom,bst-ps", &lcdb->bst.ps);

	lcdb->bst.ps_threshold = -EINVAL;
	if (lcdb->subtype == PM7325B) {
		rc = of_property_read_u32(node, "qcom,bst-ps-threshold-mv",
						&lcdb->bst.ps_threshold);
		if (!rc && !is_between(lcdb->bst.ps_threshold,
					PM7325B_MIN_BST_PS_MV, PM7325B_MAX_BST_PS_MV)) {
			pr_err("Invalid bst ps_threshold %d mV (min=%d, max=%d)\n",
				lcdb->bst.ps_threshold, PM7325B_MIN_BST_PS_MV,
				PM7325B_MAX_BST_PS_MV);
			return -EINVAL;
		}
	} else {
		rc = of_property_read_u32(node, "qcom,bst-ps-threshold-ma",
						&lcdb->bst.ps_threshold);
		if (!rc && !is_between(lcdb->bst.ps_threshold,
					MIN_BST_PS_MA, MAX_BST_PS_MA)) {
			pr_err("Invalid bst ps_threshold %d mA (min=%d, max=%d)\n",
				lcdb->bst.ps_threshold, MIN_BST_PS_MA, MAX_BST_PS_MA);
			return -EINVAL;
		}
	}


	default_headroom_mv = (lcdb->subtype == PM660L) ?
			       PM660_BST_HEADROOM_DEFAULT_MV :
			       BST_HEADROOM_DEFAULT_MV;
	/* Boost head room configuration */
	of_property_read_u16(node, "qcom,bst-headroom-mv",
					&lcdb->bst.headroom_mv);
	if (lcdb->bst.headroom_mv < default_headroom_mv)
		lcdb->bst.headroom_mv = default_headroom_mv;

	return 0;
}

static int qpnp_lcdb_init_ldo(struct qpnp_lcdb *lcdb)
{
	int rc = 0, ilim_ma, i = 0;
	u8 val = 0, pd_mask, pd_enable, ilim_ctl_reg, ilim_mask, ilim_sd_shift;

	if (lcdb->subtype == PM7325B) {
		pd_mask = (u8)PM7325B_LDO_EN_PULLDOWN_BIT;
		pd_enable = (u8)PM7325B_LDO_EN_PULLDOWN_BIT;
		ilim_ctl_reg = (u8)LCDB_LDO_ILIM_CTL1_REG;
		ilim_mask = (u8)SET_LDO_ILIM_MASK_SD;
		ilim_sd_shift = (u8)SET_LDO_ILIM_MASK_SD_SHIFT;
	} else {
		pd_mask = (u8)LDO_DIS_PULLDOWN_BIT;
		pd_enable = (u8)~LDO_DIS_PULLDOWN_BIT;
		ilim_ctl_reg = (u8)LCDB_LDO_ILIM_CTL2_REG;
		ilim_mask = (u8)SET_LDO_ILIM_MASK;
		ilim_sd_shift = 0;
	}

	/* configure parameters only if LCDB is disabled */
	if (!is_lcdb_enabled(lcdb)) {
		if (lcdb->ldo.voltage_mv != -EINVAL) {
			rc = qpnp_lcdb_set_voltage(lcdb,
					lcdb->ldo.voltage_mv, LDO);
			if (rc < 0) {
				pr_err("Failed to set voltage rc=%d\n", rc);
				return rc;
			}
		}

		if (lcdb->ldo.pd != -EINVAL) {
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_LDO_PD_CTL_REG, pd_mask,
				lcdb->ldo.pd ? pd_enable : ~pd_enable);
			if (rc < 0) {
				pr_err("Failed to configure LDO PD rc=%d\n",
								rc);
				return rc;
			}
		}

		if (lcdb->ldo.pd_strength != -EINVAL) {
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_LDO_PD_CTL_REG, LDO_PD_STRENGTH_BIT,
				lcdb->ldo.pd_strength ?
				LDO_PD_STRENGTH_BIT : 0);
			if (rc < 0) {
				pr_err("Failed to configure LDO PD strength %s rc=%d\n",
						lcdb->ldo.pd_strength ?
						"(strong)" : "(weak)", rc);
				return rc;
			}
		}

		if (lcdb->ldo.ilim_ma != -EINVAL) {
			if (lcdb->subtype == PM7325B) {
				ilim_ma = lcdb->ldo.ilim_ma;
				/*
				 * Select the highest current available below the specified current
				 * if there is no exact match.
				 */
				for (i = 0; i < ARRAY_SIZE(pm7325b_ldo_ilim_ma); i++)
					if (ilim_ma < pm7325b_ldo_ilim_ma[i])
						break;
				val = (i == 0) ? 0 : i - 1;
			} else {
				ilim_ma = lcdb->ldo.ilim_ma - MIN_LDO_ILIM_MA;
				ilim_ma /= LDO_ILIM_STEP_MA;
				val = (ilim_ma & SET_LDO_ILIM_MASK);
			}

			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
					LCDB_LDO_ILIM_CTL1_REG,
					SET_LDO_ILIM_MASK | EN_LDO_ILIM_BIT,
					(val | EN_LDO_ILIM_BIT));
			if (rc < 0) {
				pr_err("Failed to configure LDO ilim_ma (CTL1=%d) rc=%d\n",
							val, rc);
				return rc;
			}

			val = val << ilim_sd_shift;
			rc = qpnp_lcdb_masked_write(lcdb,
					lcdb->base + ilim_ctl_reg,
					ilim_mask, val);
			if (rc < 0) {
				pr_err("Failed to configure LDO ilim_ma (CTL2=%d) rc=%d\n",
							val, rc);
				return rc;
			}
		}

		if (lcdb->ldo.soft_start_us != -EINVAL) {
			rc = qpnp_lcdb_set_soft_start(lcdb,
					lcdb->ldo.soft_start_us, LDO);
			if (rc < 0) {
				pr_err("Failed to set LDO soft_start rc=%d\n",
									rc);
				return rc;
			}
		}
	}

	rc = qpnp_lcdb_get_voltage(lcdb, &lcdb->ldo.voltage_mv, LDO);
	if (rc < 0) {
		pr_err("Failed to get LDO volatge rc=%d\n", rc);
		return rc;
	}

	lcdb->ldo.prev_voltage_mv = lcdb->ldo.voltage_mv;

	rc = qpnp_lcdb_read(lcdb, lcdb->base +
			LCDB_LDO_VREG_OK_CTL_REG, &val, 1);
	if (rc < 0) {
		pr_err("Failed to read ldo_vreg_ok rc=%d\n", rc);
		return rc;
	}
	lcdb->ldo.vreg_ok_dbc_us = dbc_us[val & VREG_OK_DEB_MASK];

	rc = qpnp_lcdb_read(lcdb, lcdb->base +
			LCDB_LDO_SOFT_START_CTL_REG, &val, 1);
	if (rc < 0) {
		pr_err("Failed to read ldo_soft_start_ctl rc=%d\n", rc);
		return rc;
	}
	lcdb->ldo.soft_start_us = soft_start_us[val & SOFT_START_MASK];

	rc = qpnp_lcdb_regulator_register(lcdb, LDO);
	if (rc < 0)
		pr_err("Failed to register ldo rc=%d\n", rc);

	return rc;
}

static int qpnp_lcdb_init_ncp(struct qpnp_lcdb *lcdb)
{
	int rc = 0, i = 0;
	const u32 *ncp_ilim, *dbc_ncp;
	u8 val = 0, pd_enable, ilim_ctl_reg, ilim_mask, ilim_sd_shift;

	if (lcdb->subtype == PM7325B) {
		pd_enable = (u8)PM7325B_EN_NCP_PULLDOWN_BIT;
		ilim_ctl_reg = (u8)LCDB_NCP_ILIM_CTL1_REG;
		ilim_mask = (u8)PM7325B_SET_NCP_ILIM_SD_MASK;
		ilim_sd_shift = (u8)SET_LDO_ILIM_MASK_SD_SHIFT;
		ncp_ilim = pm7325b_ncp_ilim_ma;
		dbc_ncp = ncp_dbc_us;
	} else {
		pd_enable = (u8)~NCP_DIS_PULLDOWN_BIT;
		ilim_ctl_reg = (u8)LCDB_NCP_ILIM_CTL2_REG;
		ilim_mask = (u8)SET_NCP_ILIM_MASK;
		ilim_sd_shift = 0;
		ncp_ilim = ncp_ilim_ma;
		dbc_ncp = dbc_us;
	}

	/* configure parameters only if LCDB is disabled */
	if (!is_lcdb_enabled(lcdb)) {
		if (lcdb->ncp.voltage_mv != -EINVAL) {
			rc = qpnp_lcdb_set_voltage(lcdb,
					lcdb->ncp.voltage_mv, NCP);
			if (rc < 0) {
				pr_err("Failed to set voltage rc=%d\n", rc);
				return rc;
			}
		}

		if (lcdb->ncp.pd != -EINVAL) {
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_NCP_PD_CTL_REG, NCP_DIS_PULLDOWN_BIT,
				lcdb->ncp.pd ? pd_enable : ~pd_enable);
			if (rc < 0) {
				pr_err("Failed to configure NCP PD rc=%d\n",
									rc);
				return rc;
			}
		}

		if (lcdb->ncp.pd_strength != -EINVAL) {
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_NCP_PD_CTL_REG, NCP_PD_STRENGTH_BIT,
				lcdb->ncp.pd_strength ?
				NCP_PD_STRENGTH_BIT : 0);
			if (rc < 0) {
				pr_err("Failed to configure NCP PD strength %s rc=%d\n",
					lcdb->ncp.pd_strength ?
					"(strong)" : "(weak)", rc);
				return rc;
			}
		}

		if (lcdb->ncp.ilim_ma != -EINVAL) {
			while (lcdb->ncp.ilim_ma >= ncp_ilim[i])
				i++;
			val = (i == 0) ? 0 : i - 1;
			val = (val & SET_NCP_ILIM_MASK);
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
						LCDB_NCP_ILIM_CTL1_REG,
						SET_NCP_ILIM_MASK | EN_NCP_ILIM_BIT,
						val | EN_NCP_ILIM_BIT);
			if (rc < 0) {
				pr_err("Failed to configure NCP ilim_ma (CTL1=%d) rc=%d\n",
								val, rc);
				return rc;
			}
			val = val << ilim_sd_shift;
			rc = qpnp_lcdb_masked_write(lcdb,
					lcdb->base + ilim_ctl_reg,
					ilim_mask, val);
			if (rc < 0) {
				pr_err("Failed to configure NCP ilim_ma (CTL2=%d) rc=%d\n",
							val, rc);
				return rc;
			}
		}

		if (lcdb->ncp.soft_start_us != -EINVAL) {
			rc = qpnp_lcdb_set_soft_start(lcdb,
				lcdb->ncp.soft_start_us, NCP);
			if (rc < 0) {
				pr_err("Failed to set NCP soft_start rc=%d\n",
								rc);
				return rc;
			}
		}
	}

	rc = qpnp_lcdb_get_voltage(lcdb, &lcdb->ncp.voltage_mv, NCP);
	if (rc < 0) {
		pr_err("Failed to get NCP volatge rc=%d\n", rc);
		return rc;
	}

	lcdb->ncp.prev_voltage_mv = lcdb->ncp.voltage_mv;

	rc = qpnp_lcdb_read(lcdb, lcdb->base +
			LCDB_NCP_VREG_OK_CTL_REG, &val, 1);
	if (rc < 0) {
		pr_err("Failed to read ncp_vreg_ok rc=%d\n", rc);
		return rc;
	}
	lcdb->ncp.vreg_ok_dbc_us = dbc_ncp[val & VREG_OK_DEB_MASK];

	rc = qpnp_lcdb_read(lcdb, lcdb->base +
			LCDB_NCP_SOFT_START_CTL_REG, &val, 1);
	if (rc < 0) {
		pr_err("Failed to read ncp_soft_start_ctl rc=%d\n", rc);
		return rc;
	}
	lcdb->ncp.soft_start_us = soft_start_us[val & SOFT_START_MASK];

	rc = qpnp_lcdb_regulator_register(lcdb, NCP);
	if (rc < 0)
		pr_err("Failed to register NCP rc=%d\n", rc);

	return rc;
}

static int qpnp_lcdb_init_bst(struct qpnp_lcdb *lcdb)
{
	int rc = 0, bst_ps_min, bst_ps_step;
	const u32 *dbc_bst;
	u8 val = 0, pd_mask, pd_enable, mask = 0, bst_ilim_en, bst_vreg_ok_reg;

	if (lcdb->subtype == PM7325B) {
		pd_mask = (u8)PM7325B_BOOST_EN_PULLDOWN_BIT;
		pd_enable = (u8)PM7325B_BOOST_EN_PULLDOWN_BIT;
		bst_ilim_en = 0;
		bst_ps_step = 24;
		bst_ps_min = PM7325B_MIN_BST_PS_MV;
		dbc_bst = bst_dbc_us;
		bst_vreg_ok_reg = (u8)PM7325B_LCDB_BST_VREG_OK_CTL_REG;
	} else {
		pd_mask = (u8)BOOST_DIS_PULLDOWN_BIT;
		pd_enable = (u8)~BOOST_DIS_PULLDOWN_BIT;
		bst_ilim_en = (u8)EN_BST_ILIM_BIT;
		bst_ps_step = 10;
		bst_ps_min = MIN_BST_PS_MA;
		dbc_bst = dbc_us;
		bst_vreg_ok_reg = (u8)LCDB_BST_VREG_OK_CTL_REG;
	}

	/* configure parameters only if LCDB is disabled */
	if (!is_lcdb_enabled(lcdb)) {
		if (lcdb->bst.pd != -EINVAL) {
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_BST_PD_CTL_REG, pd_mask,
				lcdb->bst.pd ? pd_enable : ~pd_enable);
			if (rc < 0) {
				pr_err("Failed to configure BST PD rc=%d\n",
									rc);
				return rc;
			}
		}

		if (lcdb->bst.pd_strength != -EINVAL) {
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_BST_PD_CTL_REG, BOOST_PD_STRENGTH_BIT,
				lcdb->bst.pd_strength ?
				BOOST_PD_STRENGTH_BIT : 0);
			if (rc < 0) {
				pr_err("Failed to configure NCP PD strength %s rc=%d\n",
					lcdb->bst.pd_strength ?
					"(strong)" : "(weak)", rc);
				return rc;
			}
		}

		if (lcdb->bst.ilim_ma != -EINVAL) {
			if (lcdb->subtype == PM7325B) {
				val = (lcdb->bst.ilim_ma - PM7325B_MIN_BST_ILIM_MA)
					/ PM7325B_BST_ILIM_MA_STEP;
			} else {
				val = (lcdb->bst.ilim_ma / MIN_BST_ILIM_MA) - 1;
			}
			val = (val & SET_BST_ILIM_MASK) | bst_ilim_en;
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_BST_ILIM_CTL_REG,
				SET_BST_ILIM_MASK | bst_ilim_en, val);
			if (rc < 0) {
				pr_err("Failed to configure BST ilim_ma rc=%d\n",
									rc);
				return rc;
			}
		}

		if (lcdb->bst.ps != -EINVAL) {
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
					LCDB_PS_CTL_REG, EN_PS_BIT,
					lcdb->bst.ps ? EN_PS_BIT : 0);
			if (rc < 0) {
				pr_err("Failed to disable BST PS rc=%d\n", rc);
				return rc;
			}
		}

		if (lcdb->bst.ps_threshold != -EINVAL) {
			mask = (lcdb->subtype == PM660L) ?
					PM660_PS_THRESH_MASK : PS_THRESH_MASK;
			val = (lcdb->bst.ps_threshold - bst_ps_min) / bst_ps_step;
			val = (val & mask) | EN_PS_BIT;
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
						LCDB_PS_CTL_REG,
						mask | EN_PS_BIT, val);
			if (rc < 0) {
				pr_err("Failed to configure BST PS threshold rc=%d\n",
								rc);
				return rc;
			}
		}
	}

	rc = qpnp_lcdb_get_voltage(lcdb, &lcdb->bst.voltage_mv, BST);
	if (rc < 0) {
		pr_err("Failed to get BST voltage rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_lcdb_read(lcdb, lcdb->base +
			bst_vreg_ok_reg, &val, 1);
	if (rc < 0) {
		pr_err("Failed to read bst_vreg_ok rc=%d\n", rc);
		return rc;
	}
	lcdb->bst.vreg_ok_dbc_us = dbc_bst[val & VREG_OK_DEB_MASK];

	if (lcdb->subtype == PM660L) {
		rc = qpnp_lcdb_read(lcdb, lcdb->base +
				    LCDB_SOFT_START_CTL_REG, &val, 1);
		if (rc < 0) {
			pr_err("Failed to read lcdb_soft_start_ctl rc=%d\n",
									rc);
			return rc;
		}
		lcdb->bst.soft_start_us = (val & SOFT_START_MASK) * 200 + 200;
		if (!lcdb->bst.headroom_mv)
			lcdb->bst.headroom_mv = PM660_BST_HEADROOM_DEFAULT_MV;
	} else {
		rc = qpnp_lcdb_read(lcdb, lcdb->base +
				    LCDB_BST_SS_CTL_REG, &val, 1);
		if (rc < 0) {
			pr_err("Failed to read bst_soft_start_ctl rc=%d\n", rc);
			return rc;
		}
		lcdb->bst.soft_start_us = soft_start_us[val & SOFT_START_MASK];
		if (!lcdb->bst.headroom_mv)
			lcdb->bst.headroom_mv = BST_HEADROOM_DEFAULT_MV;
	}

	return 0;
}

static void qpnp_lcdb_pmic_config(struct qpnp_lcdb *lcdb)
{
	switch (lcdb->subtype) {
	case PMI632:
	case PM6150L:
	case PM7325B:
		lcdb->wa_flags |= FORCE_PD_ENABLE_WA;
		break;
	default:
		break;
	}

	pr_debug("LCDB wa_flags = 0x%2x\n", lcdb->wa_flags);
}

static int qpnp_lcdb_hw_init(struct qpnp_lcdb *lcdb)
{
	int rc = 0;
	u8 val = 0;

	qpnp_lcdb_pmic_config(lcdb);

	if (lcdb->pwrdn_delay_ms != -EINVAL) {
		rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
					    LCDB_PWRUP_PWRDN_CTL_REG,
					    PWRDN_DELAY_MASK,
					    lcdb->pwrdn_delay_ms);
		if (rc < 0)
			return rc;
	}

	if (lcdb->pwrup_delay_ms != -EINVAL) {
		rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
					    LCDB_PWRUP_PWRDN_CTL_REG,
					    PWRUP_DELAY_MASK,
					    lcdb->pwrup_delay_ms << PWRUP_DELAY_SHIFT);
		if (rc < 0)
			return rc;
	}

	if (lcdb->pwrup_config != -EINVAL) {
		rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
					    LCDB_CONFIG_SEL_REG,
					    PWRUP_CONFIG_SEL_MASK,
					    lcdb->pwrup_config);
		if (rc < 0)
			return rc;
	}

	if (lcdb->high_p2_blk_ns != -EINVAL) {
		rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
					    PM7325B_LCDB_P2_BLANK_TIMER_REG,
					    HIGH_P2_BLK_SEL_MASK,
					    lcdb->high_p2_blk_ns << HIGH_P2_BLK_SEL_SHIFT);
		if (rc < 0)
			return rc;
	}

	if (lcdb->low_p2_blk_ns != -EINVAL) {
		rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
					    PM7325B_LCDB_P2_BLANK_TIMER_REG,
					    LOW_P2_BLK_SEL_MASK,
					    lcdb->low_p2_blk_ns);
		if (rc < 0)
			return rc;
	}

	if (lcdb->mpc_current_thr_ma != -EINVAL) {
		rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
					    PM7325B_LCDB_MPC_CTL_REG,
					    MPC_NCP_SD_SEL_MASK,
					    lcdb->mpc_current_thr_ma);
		if (rc < 0)
			return rc;
	}

	if (lcdb->ncp_symmetry) {
		rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
					    LCDB_NCP_OUTPUT_VOLTAGE_REG,
					    EN_NCP_VOUT_SYMMETRY_BIT,
					    EN_NCP_VOUT_SYMMETRY_BIT);
		if (rc < 0)
			return rc;
	}

	rc = qpnp_lcdb_init_bst(lcdb);
	if (rc < 0) {
		pr_err("Failed to initialize BOOST rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_lcdb_init_ldo(lcdb);
	if (rc < 0) {
		pr_err("Failed to initialize LDO rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_lcdb_init_ncp(lcdb);
	if (rc < 0) {
		pr_err("Failed to initialize NCP rc=%d\n", rc);
		return rc;
	}

	if (lcdb->sc_irq >= 0 && lcdb->subtype != PM660L) {
		lcdb->sc_count = 0;
		rc = devm_request_threaded_irq(lcdb->dev, lcdb->sc_irq,
				NULL, qpnp_lcdb_sc_irq_handler, IRQF_ONESHOT,
				"qpnp_lcdb_sc_irq", lcdb);
		if (rc < 0) {
			pr_err("Unable to request sc(%d) irq rc=%d\n",
						lcdb->sc_irq, rc);
			return rc;
		}
	}

	if (!is_lcdb_enabled(lcdb)) {
		rc = qpnp_lcdb_read(lcdb, lcdb->base +
				LCDB_MODULE_RDY_REG, &val, 1);
		if (rc < 0) {
			pr_err("Failed to read MODULE_RDY rc=%d\n", rc);
			return rc;
		}
		if (!(val & MODULE_RDY_BIT)) {
			rc = qpnp_lcdb_masked_write(lcdb, lcdb->base +
				LCDB_MODULE_RDY_REG, MODULE_RDY_BIT,
						MODULE_RDY_BIT);
			if (rc < 0) {
				pr_err("Failed to set MODULE RDY rc=%d\n", rc);
				return rc;
			}
		}
	} else {
		/* module already enabled */
		lcdb->lcdb_enabled = true;
	}

	return 0;
}

static int qpnp_lcdb_pwrup_dn_delay(int val, int *delay)
{
	int i;

	if (!is_between(val, PWRDN_DELAY_MIN_MS, PWRDN_DELAY_MAX_MS)) {
		pr_err("Invalid PWR_UP_DN_DLY val %d (min=%d max=%d)\n",
			val, PWRDN_DELAY_MIN_MS, PWRDN_DELAY_MAX_MS);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(pwrup_pwrdn_ms); i++) {
		if (val == pwrup_pwrdn_ms[i]) {
			*delay = i;
			break;
		}
	}

	return 0;
}

static int qpnp_lcdb_p2_blk_time(int val, int *time)
{
	int i;

	if (!is_between(val, pm7325b_p2_blk_ns[0], pm7325b_p2_blk_ns[7])) {
		pr_err("Invalid P2_BLK_TIME val %d (min=%d max=%d)\n",
			val, pm7325b_p2_blk_ns[0], pm7325b_p2_blk_ns[7]);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(pm7325b_p2_blk_ns); i++) {
		if (val == pm7325b_p2_blk_ns[i]) {
			*time = i;
			break;
		}
	}

	return 0;
}

static int qpnp_lcdb_mpc_current(int val, int *cur)
{
	if (!is_between(val, MPC_CURRENT_MIN, MPC_CURRENT_MAX)) {
		pr_err("Invalid MPC_CURRENT val %d (min=%d max=%d)\n",
			val, MPC_CURRENT_MIN, MPC_CURRENT_MAX);
		return -EINVAL;
	}

	*cur = (val - MPC_CURRENT_MIN) / MPC_CURRENT_STEP;

	return 0;
}

static int qpnp_lcdb_parse_dt(struct qpnp_lcdb *lcdb)
{
	int rc = 0;
	u32 tmp;
	const char *label;
	struct device_node *temp, *node = lcdb->dev->of_node;

	for_each_available_child_of_node(node, temp) {
		rc = of_property_read_string(temp, "label", &label);
		if (rc < 0) {
			pr_err("Failed to read label rc=%d\n", rc);
			return rc;
		}

		if (!strcmp(label, "ldo")) {
			lcdb->ldo.node = temp;
			rc = qpnp_lcdb_ldo_dt_init(lcdb);
		} else if (!strcmp(label, "ncp")) {
			lcdb->ncp.node = temp;
			rc = qpnp_lcdb_ncp_dt_init(lcdb);
		} else if (!strcmp(label, "bst")) {
			lcdb->bst.node = temp;
			rc = qpnp_lcdb_bst_dt_init(lcdb);
		} else {
			pr_err("Failed to identify label %s\n", label);
			return -EINVAL;
		}
		if (rc < 0) {
			pr_err("Failed to register %s module\n", label);
			return rc;
		}
	}

	if (of_property_read_bool(node, "qcom,ttw-enable")) {
		rc = qpnp_lcdb_parse_ttw(lcdb);
		if (rc < 0) {
			pr_err("Failed to parse ttw-params rc=%d\n", rc);
			return rc;
		}
		lcdb->ttw_enable = true;
	}

	lcdb->sc_irq = platform_get_irq_byname(lcdb->pdev, "sc-irq");
	if (lcdb->sc_irq < 0)
		pr_debug("sc irq is not defined\n");

	lcdb->voltage_step_ramp =
			of_property_read_bool(node, "qcom,voltage-step-ramp");

	lcdb->ncp_symmetry =
			of_property_read_bool(node, "qcom,ncp-symmetry");

	lcdb->pwrdn_delay_ms = -EINVAL;
	lcdb->pwrup_delay_ms = -EINVAL;
	lcdb->pwrup_config = -EINVAL;
	lcdb->high_p2_blk_ns = -EINVAL;
	lcdb->low_p2_blk_ns = -EINVAL;
	lcdb->mpc_current_thr_ma = -EINVAL;
	rc = of_property_read_u32(node, "qcom,pwrdn-delay-ms", &tmp);
	if (!rc) {
		rc = qpnp_lcdb_pwrup_dn_delay(tmp, &lcdb->pwrdn_delay_ms);
		if (rc < 0)
			return rc;
	}

	rc = of_property_read_u32(node, "qcom,pwrup-delay-ms", &tmp);
	if (!rc) {
		rc = qpnp_lcdb_pwrup_dn_delay(tmp, &lcdb->pwrup_delay_ms);
		if (rc < 0)
			return rc;
	}

	rc = of_property_read_u32(node, "qcom,pwrup-config", &lcdb->pwrup_config);
	if (!rc && lcdb->pwrup_config > PWRUP_CONFIG_MAX) {
		pr_err("Invalid pwrup config %d, max=%d\n",
			lcdb->pwrup_config, PWRUP_CONFIG_MAX);
		return -EINVAL;
	}

	rc = of_property_read_u32(node, "qcom,high-p2-blank-time-ns", &tmp);
	if (!rc) {
		rc = qpnp_lcdb_p2_blk_time(tmp, &lcdb->high_p2_blk_ns);
		if (rc < 0)
			return rc;
	}

	rc = of_property_read_u32(node, "qcom,low-p2-blank-time-ns", &tmp);
	if (!rc) {
		rc = qpnp_lcdb_p2_blk_time(tmp, &lcdb->low_p2_blk_ns);
		if (rc < 0)
			return rc;
	}

	rc = of_property_read_u32(node, "qcom,mpc-current-thr-ma", &tmp);
	if (!rc) {
		rc = qpnp_lcdb_mpc_current(tmp, &lcdb->mpc_current_thr_ma);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int qpnp_lcdb_regulator_probe(struct platform_device *pdev)
{
	int rc;
	struct device_node *node;
	struct qpnp_lcdb *lcdb;
	const struct of_device_id *dev_id;

	node = pdev->dev.of_node;
	if (!node) {
		pr_err("No nodes defined\n");
		return -ENODEV;
	}

	lcdb = devm_kzalloc(&pdev->dev, sizeof(*lcdb), GFP_KERNEL);
	if (!lcdb)
		return -ENOMEM;

	rc = of_property_read_u32(node, "reg", &lcdb->base);
	if (rc < 0) {
		pr_err("Failed to find reg node rc=%d\n", rc);
		return rc;
	}

	lcdb->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!lcdb->regmap) {
		pr_err("Failed to get the regmap handle rc=%d\n", rc);
		return -EINVAL;
	}

	lcdb->subtype = (u8)(unsigned long)of_device_get_match_data(&pdev->dev);
	lcdb->dev = &pdev->dev;
	lcdb->pdev = pdev;
	dev_id = of_match_device(lcdb->dev->driver->of_match_table, lcdb->dev);

	lcdb->min_voltage_mv = (lcdb->subtype == PM7325B) ?
			       PM7325B_MIN_VOLTAGE_MV : MIN_VOLTAGE_MV;
	lcdb->max_voltage_mv = (lcdb->subtype == PM7325B) ?
			       PM7325B_MAX_VOLTAGE_MV : MAX_VOLTAGE_MV;

	mutex_init(&lcdb->lcdb_mutex);
	mutex_init(&lcdb->read_write_mutex);

	rc = qpnp_lcdb_parse_dt(lcdb);
	if (rc < 0) {
		pr_err("Failed to parse dt rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_lcdb_hw_init(lcdb);
	if (rc < 0)
		pr_err("Failed to initialize LCDB module rc=%d\n", rc);
	else
		pr_info("LCDB module: %s successfully registered! lcdb_en=%d ldo_voltage=%dmV ncp_voltage=%dmV bst_voltage=%dmV\n",
			dev_id->compatible, lcdb->lcdb_enabled, lcdb->ldo.voltage_mv,
			lcdb->ncp.voltage_mv, lcdb->bst.voltage_mv);

	return rc;
}

static int qpnp_lcdb_regulator_remove(struct platform_device *pdev)
{
	struct qpnp_lcdb *lcdb = dev_get_drvdata(&pdev->dev);

	mutex_destroy(&lcdb->lcdb_mutex);
	mutex_destroy(&lcdb->read_write_mutex);

	return 0;
}

static const struct of_device_id lcdb_match_table[] = {
	{ .compatible = QPNP_LCDB_REGULATOR_DRIVER_NAME,
		.data = (void *)PM_DEFAULT,},
	{ .compatible = QPNP_LCDB_REGULATOR_DRIVER_660,
		.data = (void *)PM660L,},
	{ .compatible = QPNP_LCDB_REGULATOR_DRIVER_632,
		.data = (void *)PMI632,},
	{ .compatible = QPNP_LCDB_REGULATOR_DRIVER_6150L,
		.data = (void *)PM6150L,},
	{ .compatible = QPNP_LCDB_REGULATOR_DRIVER_7325B,
		.data = (void *)PM7325B,},
	{ },
};

static struct platform_driver qpnp_lcdb_regulator_driver = {
	.driver		= {
		.name		= QPNP_LCDB_REGULATOR_DRIVER_NAME,
		.of_match_table	= lcdb_match_table,
	},
	.probe		= qpnp_lcdb_regulator_probe,
	.remove		= qpnp_lcdb_regulator_remove,
};

static int __init qpnp_lcdb_regulator_init(void)
{
	return platform_driver_register(&qpnp_lcdb_regulator_driver);
}
arch_initcall(qpnp_lcdb_regulator_init);

static void __exit qpnp_lcdb_regulator_exit(void)
{
	platform_driver_unregister(&qpnp_lcdb_regulator_driver);
}
module_exit(qpnp_lcdb_regulator_exit);

MODULE_DESCRIPTION("QPNP LCDB regulator driver");
MODULE_LICENSE("GPL");
