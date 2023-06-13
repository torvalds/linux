// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "SMB1398: %s: " fmt, __func__

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pmic-voter.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/iio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/qti_power_supply.h>
#include <dt-bindings/iio/qti_power_supply_iio.h>

/* Status register definition */
#define REVID_REVISION4			0x103

#define INPUT_STATUS_REG		0x2609
#define INPUT_USB_IN			BIT(1)
#define INPUT_WLS_IN			BIT(0)

#define PERPH0_INT_RT_STS_REG		0x2610
#define USB_IN_OVLO_STS			BIT(7)
#define WLS_IN_OVLO_STS			BIT(6)
#define USB_IN_UVLO_STS			BIT(5)
#define WLS_IN_UVLO_STS			BIT(4)
#define DIV2_IREV_LATCH_STS		BIT(3)
#define VOL_UV_LATCH_STS		BIT(2)
#define TEMP_SHUTDOWN_STS		BIT(1)
#define CFLY_HARD_FAULT_LATCH_STS	BIT(0)

#define MODE_STATUS_REG			0x2641
#define SMB_EN				BIT(7)
#define PRE_EN_DCDC			BIT(6)
#define DIV2_EN_SLAVE			BIT(5)
#define LCM_EN				BIT(4)
#define DIV2_EN				BIT(3)
#define BUCK_EN				BIT(2)
#define CFLY_SS_DONE			BIT(1)
#define DCDC_EN				BIT(0)

#define SWITCHER_OFF_WIN_STATUS_REG	0x2642
#define DIV2_WIN_OV			BIT(1)
#define DIV2_WIN_UV			BIT(0)

#define SWITCHER_OFF_VIN_STATUS_REG	0x2643
#define USB_IN_OVLO			BIT(3)
#define WLS_IN_OVLO			BIT(2)
#define USB_IN_UVLO			BIT(1)
#define WLS_IN_UVLO			BIT(0)

#define SWITCHER_OFF_FAULT_REG		0x2644
#define VOUT_OV_3LVL_BUCK		BIT(5)
#define VOUT_UV_LATCH			BIT(4)
#define ITERM_3LVL_LATCH		BIT(3)
#define DIV2_IREV_LATCH			BIT(2)
#define TEMP_SHDWN			BIT(1)
#define CFLY_HARD_FAULT_LATCH		BIT(0)

#define BUCK_CC_CV_STATE_REG		0x2645
#define BUCK_IN_CC_REGULATION		BIT(1)
#define BUCK_IN_CV_REGULATION		BIT(0)

#define INPUT_CURRENT_REGULATION_REG	0x2646
#define BUCK_IN_ICL			BIT(1)
#define DIV2_IN_ILIM			BIT(0)

/* Config register definition */
#define PERPH0_MISC_CFG2_REG		0x2636
#define CFG_TEMP_PIN_ITEMP		BIT(1)

#define MISC_USB_WLS_SUSPEND_REG	0x2630
#define WLS_SUSPEND			BIT(1)
#define USB_SUSPEND			BIT(0)

#define MISC_SL_SWITCH_EN_REG		0x2631
#define EN_SLAVE			BIT(1)
#define EN_SWITCHER			BIT(0)

#define MISC_DIV2_3LVL_CTRL_REG		0x2632
#define MISC_DIV2_3LVL_CTRL_MASK	GENMASK(7, 0)
#define EN_DIV2_CP			BIT(2)
#define EN_3LVL_BULK			BIT(1)
#define EN_CHG_2X			BIT(0)

#define MISC_CFG0_REG			0x2634
#define DIS_SYNC_DRV_BIT		BIT(5)
#define SW_EN_SWITCHER_BIT		BIT(3)
#define CFG_DIS_FPF_IREV_BIT		BIT(1)

#define MISC_CFG1_REG			0x2635
#define MISC_CFG1_MASK			GENMASK(7, 0)
#define CFG_OP_MODE_MASK		GENMASK(2, 0)
#define OP_MODE_DISABLED		0
#define OP_MODE_3LVL_BULK		1
#define OP_MODE_COMBO			2
#define OP_MODE_DIV2_CP			3
#define OP_MODE_PRE_REG_3S		4
#define OP_MODE_ITLGS_1P		5
#define OP_MODE_ITLGS_2X		6
#define OP_MODE_PRE_REGULATOR		7

#define MISC_CFG2_REG			0x2636

#define NOLOCK_SPARE_REG		0x2637
#define EN_SLAVE_OWN_FREQ_BIT		BIT(5)
#define DIV2_WIN_UV_SEL_BIT		BIT(4)
#define DIV2_WIN_UV_25MV		0
#define COMBO_WIN_LO_EXIT_SEL_MASK	GENMASK(3, 2)
#define EXIT_DIV2_VOUT_HI_12P5MV	0
#define EXIT_DIV2_VOUT_HI_25MV		1
#define EXIT_DIV2_VOUT_HI_50MV		2
#define EXIT_DIV2_VOUT_HI_75MV		3
#define COMBO_WIN_HI_EXIT_SEL_MASK	GENMASK(1, 0)
#define EXIT_DIV2_VOUT_LO_75MV		0
#define EXIT_DIV2_VOUT_LO_100MV		1
#define EXIT_DIV2_VOUT_LO_200MV		2
#define EXIT_DIV2_VOUT_LO_250MV		3

#define SMB_EN_TRIGGER_CFG_REG		0x2639
#define SMB_EN_NEG_TRIGGER		BIT(1)
#define SMB_EN_POS_TRIGGER		BIT(0)

#define PERPH0_DIV2_SLAVE		0x2652
#define CFG_EN_SLAVE_OWN_FREQ		BIT(1)
#define CFG_DIV2_SYNC_CLK_PHASE_90		BIT(0)

#define DIV2_LCM_CFG_REG		0x2653
#define DIV2_LCM_REFRESH_TIMER_SEL_MASK	GENMASK(5, 4)
#define DIV2_WIN_BURST_HIGH_REF_MASK	GENMASK(3, 2)
#define DIV2_WIN_BURST_LOW_REF_MASK	GENMASK(1, 0)

#define DIV2_CURRENT_REG		0x2655
#define DIV2_EN_ILIM_DET		BIT(2)
#define DIV2_EN_IREV_DET		BIT(1)
#define DIV2_EN_OCP_DET			BIT(0)

#define DIV2_PROTECTION_REG		0x2656
#define DIV2_WIN_OV_SEL_MASK		GENMASK(1, 0)
#define WIN_OV_200_MV			0
#define WIN_OV_300_MV			1
#define WIN_OV_400_MV			2
#define WIN_OV_500_MV			3

#define PERPH0_OVLO_REF_REG			0x265B
#define SMB1394_INPUT_OVLO_CONF_MASK	GENMASK(2, 0)
#define SMB1394_INPUT_OVLO_13P04V	0x5

#define DIV2_MODE_CFG_REG		0x265C

#define LCM_EXIT_CTRL_REG		0x265D

#define ICHG_SS_DAC_TARGET_REG		0x2660
#define ICHG_SS_DAC_VALUE_MASK		GENMASK(5, 0)
#define ICHG_STEP_MA			100

#define VOUT_DAC_TARGET_REG		0x2663
#define VOUT_DAC_VALUE_MASK		GENMASK(7, 0)
#define VOUT_1P_MIN_MV			3300
#define VOUT_1S_MIN_MV			6600
#define VOUT_1P_STEP_MV			10
#define VOUT_1S_STEP_MV			20

#define VOUT_SS_DAC_TARGET_REG		0x2666
#define VOUT_SS_DAC_VALUE_MASK		GENMASK(5, 0)
#define VOUT_SS_1P_STEP_MV		90
#define VOUT_SS_1S_STEP_MV		180

#define IIN_SS_DAC_TARGET_REG		0x2669
#define IIN_SS_DAC_VALUE_MASK		GENMASK(6, 0)
#define IIN_STEP_MA			50

#define PERPH0_DIV2_REF_CFG		0x2671
#define CFG_IREV_REF_BIT		BIT(2)

#define PERPH0_CFG_SDCDC_REG		0x267A
#define EN_WIN_UV_BIT			BIT(7)
#define EN_WIN_OV_RISE_DEB_BIT		BIT(6)

#define PERPH0_SOVP_CFG0_REG		0x2680
#define CFG_OVP_VSNS_THRESHOLD		BIT(4)
#define CFG_OVP_IGNORE_UVLO		BIT(5)

#define PERPH0_SSUPPLY_CFG0_REG		0x2682
#define EN_HV_OV_OPTION2_BIT		BIT(7)
#define EN_MV_OV_OPTION2_BIT		BIT(5)
#define CFG_CMP_VOUT_VS_4V_REF_MASK	GENMASK(2, 1)
#define CMP_VOUT_VS_4V_REF_3P2V		0x3	/* Value for SMB1394 only */

#define SSUPLY_TEMP_CTRL_REG		0x2683
#define SEL_OUT_TEMP_MAX_MASK		GENMASK(7, 5)
#define SEL_OUT_TEMP_MAX_SHFT		5
#define SEL_OUT_HIGHZ			(0 << SEL_OUT_TEMP_MAX_SHFT)
#define SEL_OUT_VTEMP			(1 << SEL_OUT_TEMP_MAX_SHFT)
#define SEL_OUT_ICHG			(2 << SEL_OUT_TEMP_MAX_SHFT)
#define SEL_OUT_IIN_FB			(4 << SEL_OUT_TEMP_MAX_SHFT)

#define PERPH1_INT_RT_STS_REG		0x2710
#define DIV2_WIN_OV_STS			BIT(7)
#define DIV2_WIN_UV_STS			BIT(6)
#define DIV2_ILIM_STS			BIT(5)
#define DIV2_CFLY_SS_DONE_STS		BIT(1)

#define PERPH1_LOCK_SPARE_REG		0x27C3
#define CFG_LOCK_SPARE1_MASK		GENMASK(7, 6)
#define CFG_LOCK_SPARE1_SHIFT		6

/* available voters */
#define ILIM_VOTER			"ILIM_VOTER"
#define TAPER_VOTER			"TAPER_VOTER"
#define STATUS_CHANGE_VOTER		"STATUS_CHANGE_VOTER"
#define SHUTDOWN_VOTER			"SHUTDOWN_VOTER"
#define CUTOFF_SOC_VOTER		"CUTOFF_SOC_VOTER"
#define SRC_VOTER			"SRC_VOTER"
#define ICL_VOTER			"ICL_VOTER"
#define WIRELESS_VOTER			"WIRELESS_VOTER"
#define SWITCHER_TOGGLE_VOTER		"SWITCHER_TOGGLE_VOTER"
#define USER_VOTER			"USER_VOTER"
#define FCC_VOTER			"FCC_VOTER"
#define CP_VOTER			"CP_VOTER"
#define CC_MODE_VOTER			"CC_MODE_VOTER"
#define MAIN_DISABLE_VOTER		"MAIN_DISABLE_VOTER"
#define TAPER_MAIN_ICL_LIMIT_VOTER	"TAPER_MAIN_ICL_LIMIT_VOTER"

/* Constant definitions */
#define DIV2_MAX_ILIM_UA		5000000
#define DIV2_MAX_ILIM_DUAL_CP_UA	10000000
#define DIV2_ILIM_CFG_PCT		105

#define TAPER_STEPPER_UA_DEFAULT	100000
#define TAPER_STEPPER_UA_IN_CC_MODE	200000
#define CC_MODE_TAPER_MAIN_ICL_UA	500000

#define MAX_IOUT_UA			6300000
#define MAX_1S_VOUT_UV			11700000

#define THERMAL_SUSPEND_DECIDEGC	1400

#define DIV2_CP_MASTER			0
#define DIV2_CP_SLAVE			1
#define COMBO_PRE_REGULATOR		2
#define SMB1394_DIV2_CP_PRY		3
#define SMB1394_DIV2_CP_SECY		4

#define IS_SMB1394(role) \
	(role == SMB1394_DIV2_CP_PRY || role == SMB1394_DIV2_CP_SECY)

enum isns_mode {
	ISNS_MODE_OFF = 0,
	ISNS_MODE_ACTIVE,
	ISNS_MODE_STANDBY,
};

enum ovp {
	OVP_17P7V = 0,
	OVP_14V,
	OVP_22P2V,
	OVP_7P3,
};

enum {
	/* Perph0 IRQs */
	CFLY_HARD_FAULT_LATCH_IRQ,
	TEMP_SHDWN_IRQ,
	VOUT_UV_LATH_IRQ,
	DIV2_IREV_LATCH_IRQ,
	WLS_IN_UVLO_IRQ,
	USB_IN_UVLO_IRQ,
	WLS_IN_OVLO_IRQ,
	USB_IN_OVLO_IRQ,
	/* Perph1 IRQs */
	BK_IIN_REG_IRQ,
	CFLY_SS_DONE_IRQ,
	EN_DCDC_IRQ,
	ITERM_3LVL_LATCH_IRQ,
	VOUT_OV_3LB_IRQ,
	DIV2_ILIM_IRQ,
	DIV2_WIN_UV_IRQ,
	DIV2_WIN_OV_IRQ,
	/* Perph2 IRQs */
	IN_3LVL_MODE_IRQ,
	DIV2_MODE_IRQ,
	BK_CV_REG_IRQ,
	BK_CC_REG_IRQ,
	SS_DAC_INT_IRQ,
	SMB_EN_RISE_IRQ,
	SMB_EN_FALL_IRQ,
	/* End */
	NUM_IRQS,
};

struct smb_irq {
	const char		*name;
	const irq_handler_t	handler;
	const bool		wake;
	int			shift;
};

static const struct smb_irq smb_irqs[];

struct smb1398_chip {
	struct device		*dev;
	struct regmap		*regmap;
	u8			rev4;

	struct wakeup_source	*ws;
	struct iio_channel	*die_temp_chan;

	unsigned int		nchannels;
	struct iio_channel	**cp_slave_iio_chan_list;
	struct iio_chan_spec	*cp_iio_chan_ids;
	struct iio_channel	**smb5_iio_chan_list;

	struct power_supply	*div2_cp_master_psy;
	struct power_supply	*div2_cp_slave_psy;
	struct power_supply	*pre_regulator_psy;
	struct power_supply	*batt_psy;
	struct power_supply	*dc_psy;
	struct power_supply	*usb_psy;
	struct notifier_block	nb;

	struct votable		*awake_votable;
	struct votable		*div2_cp_disable_votable;
	struct votable		*div2_cp_slave_disable_votable;
	struct votable		*div2_cp_ilim_votable;
	struct votable		*pre_regulator_iout_votable;
	struct votable		*pre_regulator_vout_votable;
	struct votable		*fcc_votable;
	struct votable		*fv_votable;
	struct votable		*fcc_main_votable;
	struct votable		*usb_icl_votable;

	struct work_struct	status_change_work;
	struct work_struct	taper_work;

	struct mutex		die_chan_lock;
	spinlock_t		status_change_lock;

	int			irqs[NUM_IRQS];
	int			die_temp;
	int			div2_cp_min_ilim_ua;
	int			ilim_ua_disable_div2_cp_slave;
	int			max_cutoff_soc;
	int			taper_entry_fv;
	int			div2_irq_status;
	u32			div2_cp_role;
	u32			pl_output_mode;
	u32			pl_input_mode;
	enum isns_mode		current_capability;
	int			cc_mode_taper_main_icl_ua;
	int			cp_status1;
	int			cp_status2;
	int			cp_enable;
	int			cp_isns_master;
	int			cp_isns_slave;
	int			cp_ilim;
	int			adapter_type;

	bool			status_change_running;
	bool			taper_work_running;
	bool			cutoff_soc_checked;
	bool			smb_en;
	bool			switcher_en;
	bool			slave_en;
	bool			in_suspend;
	bool			disabled;
	bool			usb_present;
};

struct cp_iio_prop_channels {
	const char *datasheet_name;
	int channel_no;
	enum iio_chan_type type;
	long info_mask;
};

#define SMB1398_CHAN(_dname, _chno, _type, _mask)			\
	{								\
		.datasheet_name = _dname,				\
		.channel_no = _chno,					\
		.type = _type,						\
		.info_mask = _mask,					\
	},

#define SMB1398_CHAN_CUR(_dname, _chno)					\
	SMB1398_CHAN(_dname, _chno, IIO_CURRENT,			\
		  BIT(IIO_CHAN_INFO_PROCESSED))

#define SMB1398_CHAN_TEMP(_dname, _chno)				\
	SMB1398_CHAN(_dname, _chno, IIO_TEMP,				\
		  BIT(IIO_CHAN_INFO_PROCESSED))

#define SMB1398_CHAN_INDEX(_dname, _chno)				\
	SMB1398_CHAN(_dname, _chno, IIO_INDEX,				\
		  BIT(IIO_CHAN_INFO_PROCESSED))

static int smb1398_read(struct smb1398_chip *chip, u16 reg, u8 *val)
{
	int rc = 0, value = 0;

	rc = regmap_read(chip->regmap, reg, &value);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't read register 0x%x, rc=%d\n",
				reg, rc);
	else
		*val = (u8)value;

	return rc;
}

static int smb1398_masked_write(struct smb1398_chip *chip,
		u16 reg, u8 mask, u8 val)
{
	int rc = 0;

	rc = regmap_update_bits(chip->regmap, reg, mask, val);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't update register 0x%x to 0x%x with mask 0x%x, rc=%d\n",
				reg, val, mask, rc);

	return rc;
}

enum iio_type {
	CP_SLAVE,
	QPNP_SMB5,
};

enum cp_slave_channels {
	CURRENT_CAPABILITY = 0,
	CP_ENABLE,
	CP_INPUT_CURRENT_MAX,
};

enum smb5_iio_channels {
	REAL_TYPE = 0,
	ADAPTER_CC_MODE,
	PD_CURRENT_MAX,
	INPUT_CURRENT_SETTLED,
	SMB_EN_MODE,
	SMB_EN_REASON,
};

static const char * const cp_slave_iio_chans[] = {
	[CURRENT_CAPABILITY] = "cp_current_capability",
	[CP_ENABLE] = "cp_enable",
	[CP_INPUT_CURRENT_MAX] = "cp_input_current_max",
};

static const char * const cp_smb5_ext_iio_chan[] = {
	[REAL_TYPE] = "real_type",
	[ADAPTER_CC_MODE] = "adapter_cc_mode",
	[PD_CURRENT_MAX] = "pd_current_max",
	[INPUT_CURRENT_SETTLED] = "input_current_settled",
	[SMB_EN_MODE] = "smb_en_mode",
	[SMB_EN_REASON] = "smb_en_reason",
};

static int cp_read_iio_prop(struct smb1398_chip *chip,
		enum iio_type type, int iio_chan_id, int *val)
{
	struct iio_channel *iio_chan;
	int rc;

	if (type == QPNP_SMB5) {
		if (IS_ERR_OR_NULL(chip->smb5_iio_chan_list))
			return -ENODEV;
		iio_chan = chip->smb5_iio_chan_list[iio_chan_id];
	} else {
		pr_err_ratelimited("iio_type %d is not supported\n", type);
		return -EINVAL;
	}

	rc = iio_read_channel_processed(iio_chan, val);
	return rc < 0 ? rc : 0;
}

static int cp_write_iio_prop(struct smb1398_chip *chip,
		enum iio_type type, int iio_chan_id, int val)
{
	struct iio_channel *iio_chan;

	if (type == CP_SLAVE) {
		if (IS_ERR_OR_NULL(chip->cp_slave_iio_chan_list))
			return -ENODEV;
		iio_chan = chip->cp_slave_iio_chan_list[iio_chan_id];
	} else {
		pr_err_ratelimited("iio_type %d is not supported\n", type);
		return -EINVAL;
	}

	return iio_write_channel_raw(iio_chan, val);
}

static int smb1398_get_enable_status(struct smb1398_chip *chip)
{
	int rc = 0;
	u8 val;
	bool switcher_en = false;

	rc = smb1398_read(chip, MODE_STATUS_REG, &val);
	if (rc < 0)
		return rc;

	chip->smb_en = !!(val & SMB_EN);
	chip->switcher_en = !!(val & PRE_EN_DCDC);
	chip->slave_en = !!(val & DIV2_EN_SLAVE);

	rc = smb1398_read(chip, MISC_SL_SWITCH_EN_REG, &val);
	if (rc < 0)
		return rc;

	switcher_en = !!(val & EN_SWITCHER);
	chip->switcher_en = switcher_en && chip->switcher_en;

	dev_dbg(chip->dev, "smb_en = %d, switcher_en = %d, slave_en = %d\n",
			chip->smb_en, chip->switcher_en, chip->slave_en);
	return rc;
}

static int smb1398_get_iin_ma(struct smb1398_chip *chip, int *iin_ma)
{
	int rc = 0;
	u8 val;

	rc = smb1398_read(chip, IIN_SS_DAC_TARGET_REG, &val);
	if (rc < 0)
		return rc;

	*iin_ma = (val & IIN_SS_DAC_VALUE_MASK) * IIN_STEP_MA;

	dev_dbg(chip->dev, "get iin_ma = %dmA\n", *iin_ma);
	return rc;
}

static int smb1398_set_iin_ma(struct smb1398_chip *chip, int iin_ma)
{
	int rc = 0;
	u8 val;

	val = iin_ma / IIN_STEP_MA;
	rc = smb1398_masked_write(chip, IIN_SS_DAC_TARGET_REG,
			IIN_SS_DAC_VALUE_MASK, val);
	if (rc < 0)
		return rc;

	dev_dbg(chip->dev, "set iin_ma = %dmA\n", iin_ma);
	return rc;
}

static int smb1398_set_ichg_ma(struct smb1398_chip *chip, int ichg_ma)
{
	int rc = 0;
	u8 val;

	if (ichg_ma < 0 || ichg_ma > ICHG_SS_DAC_VALUE_MASK * ICHG_STEP_MA)
		return rc;

	val = ichg_ma / ICHG_STEP_MA;
	rc = smb1398_masked_write(chip, ICHG_SS_DAC_TARGET_REG,
			ICHG_SS_DAC_VALUE_MASK, val);

	dev_dbg(chip->dev, "set ichg %dmA\n", ichg_ma);
	return rc;
}

static int smb1398_get_ichg_ma(struct smb1398_chip *chip, int *ichg_ma)
{
	int rc = 0;
	u8 val;

	rc = smb1398_read(chip, ICHG_SS_DAC_TARGET_REG, &val);
	if (rc < 0)
		return rc;

	*ichg_ma = (val & ICHG_SS_DAC_VALUE_MASK) * ICHG_STEP_MA;

	dev_dbg(chip->dev, "get ichg %dmA\n", *ichg_ma);
	return 0;
}

static int smb1398_set_1s_vout_mv(struct smb1398_chip *chip, int vout_mv)
{
	int rc = 0;
	u8 val;

	if (vout_mv < VOUT_1S_MIN_MV)
		return -EINVAL;

	val = (vout_mv - VOUT_1S_MIN_MV) / VOUT_1S_STEP_MV;

	rc = smb1398_masked_write(chip, VOUT_DAC_TARGET_REG,
			VOUT_DAC_VALUE_MASK, val);
	if (rc < 0)
		return rc;

	return 0;
}

static int smb1398_get_1s_vout_mv(struct smb1398_chip *chip, int *vout_mv)
{
	int rc;
	u8 val;

	rc = smb1398_read(chip, VOUT_DAC_TARGET_REG, &val);
	if (rc < 0)
		return rc;

	*vout_mv = (val & VOUT_DAC_VALUE_MASK) * VOUT_1S_STEP_MV +
		VOUT_1S_MIN_MV;

	return 0;
}

static int smb1398_get_die_temp(struct smb1398_chip *chip, int *temp)
{
	int die_temp_deciC = 0, rc = 0;

	rc =  smb1398_get_enable_status(chip);
	if (rc < 0)
		return rc;

	if (!chip->smb_en)
		return -ENODATA;

	mutex_lock(&chip->die_chan_lock);
	rc = iio_read_channel_processed(chip->die_temp_chan, &die_temp_deciC);
	mutex_unlock(&chip->die_chan_lock);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read die_temp_chan, rc=%d\n", rc);
	} else {
		*temp = die_temp_deciC / 100;
		dev_dbg(chip->dev, "die temp %d\n", *temp);
	}

	return rc;
}

static int smb1398_div2_cp_get_status1(
		struct smb1398_chip *chip, u8 *status)
{
	int rc = 0;
	u8 val;
	bool ilim, win_uv, win_ov;

	rc = smb1398_read(chip, PERPH1_INT_RT_STS_REG, &val);
	if (rc < 0)
		return rc;

	win_uv = !!(val & DIV2_WIN_UV_STS);
	win_ov = !!(val & DIV2_WIN_OV_STS);
	ilim = !!(val & DIV2_ILIM_STS);
	*status = ilim << 5 | win_uv << 1 | win_ov;

	dev_dbg(chip->dev, "status1 = 0x%x\n", *status);
	return rc;
}

static int smb1398_div2_cp_get_status2(
		struct smb1398_chip *chip, u8 *status)
{
	int rc = 0;
	u8 val;
	bool smb_en, vin_ov, vin_uv, irev, tsd, switcher_off;

	rc = smb1398_read(chip, MODE_STATUS_REG, &val);
	if (rc < 0)
		return rc;

	smb_en = !!(val & SMB_EN);
	switcher_off = !(val & PRE_EN_DCDC);

	rc = smb1398_read(chip, PERPH1_INT_RT_STS_REG, &val);
	if (rc < 0)
		return rc;

	switcher_off = !(val & DIV2_CFLY_SS_DONE_STS) && switcher_off;

	rc = smb1398_read(chip, SWITCHER_OFF_VIN_STATUS_REG, &val);
	if (rc < 0)
		return rc;

	vin_ov = !!(val & USB_IN_OVLO);
	vin_uv = !!(val & USB_IN_UVLO);

	rc = smb1398_read(chip, SWITCHER_OFF_FAULT_REG, &val);
	if (rc < 0)
		return rc;

	irev = !!(val & DIV2_IREV_LATCH);
	tsd = !!(val & TEMP_SHDWN);

	*status = smb_en << 7 | vin_ov << 6 | vin_uv << 5
		| irev << 3 | tsd << 2 | switcher_off;

	dev_dbg(chip->dev, "status2 = 0x%x\n", *status);
	return rc;
}

static int smb1398_div2_cp_get_irq_status(
		struct smb1398_chip *chip, u8 *status)
{
	int rc = 0;
	u8 val;
	bool ilim, irev, tsd, off_vin, off_win;

	rc = smb1398_read(chip, PERPH1_INT_RT_STS_REG, &val);
	if (rc < 0)
		return rc;

	ilim = !!(val & DIV2_ILIM_STS);
	off_win = !!(val & (DIV2_WIN_OV_STS | DIV2_WIN_UV_STS));

	rc = smb1398_read(chip, PERPH0_INT_RT_STS_REG, &val);
	if (rc < 0)
		return rc;

	irev = !!(val & DIV2_IREV_LATCH_STS);
	tsd = !!(val & TEMP_SHUTDOWN_STS);
	off_vin = !!(val & (USB_IN_OVLO_STS | USB_IN_UVLO_STS));

	*status = ilim << 6 | irev << 3 | tsd << 2 | off_vin << 1 | off_win;

	dev_dbg(chip->dev, "irq_status = 0x%x\n", *status);
	return rc;
}

static int smb1398_div2_cp_switcher_en(struct smb1398_chip *chip, bool en)
{
	int rc;

	rc = smb1398_masked_write(chip, MISC_USB_WLS_SUSPEND_REG,
			USB_SUSPEND, en ? 0 : USB_SUSPEND);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't write USB_WLS_SUSPEND_REG, rc=%d\n",
				rc);
		return rc;
	}

	rc = smb1398_masked_write(chip, MISC_SL_SWITCH_EN_REG,
			EN_SWITCHER, en ? EN_SWITCHER : 0);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't write SWITCH_EN_REG, rc=%d\n", rc);
		return rc;
	}

	chip->switcher_en = en;

	dev_dbg(chip->dev, "%s switcher\n", en ? "enable" : "disable");
	return rc;
}

static int smb1398_div2_cp_isns_mode_control(
		struct smb1398_chip *chip, enum isns_mode mode)
{
	int rc = 0;
	u8 mux_sel;

	switch (mode) {
	case ISNS_MODE_STANDBY:
		/* VTEMP */
		mux_sel = SEL_OUT_VTEMP;
		break;
	case ISNS_MODE_OFF:
		/* High-Z */
		mux_sel = SEL_OUT_HIGHZ;
		break;
	case ISNS_MODE_ACTIVE:
		/* IIN_FB */
		mux_sel = SEL_OUT_IIN_FB;
		break;
	default:
		return -EINVAL;
	}

	rc = smb1398_masked_write(chip, SSUPLY_TEMP_CTRL_REG,
			SEL_OUT_TEMP_MAX_MASK, mux_sel);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set SSUPLY_TEMP_CTRL_REG, rc=%d\n",
				rc);
		return rc;
	}

	rc = smb1398_masked_write(chip, PERPH0_MISC_CFG2_REG,
			CFG_TEMP_PIN_ITEMP, 0);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set PERPH0_MISC_CFG2_REG, rc=%d\n",
				rc);
		return rc;
	}

	return 0;
}

static inline int calculate_div2_cp_isns_ua(int temp)
{
	/* ISNS = (2850 + (0.0034 * thermal_reading) / 0.32) * 1000 uA */
	return (2850 * 1000 + div_s64((s64)temp * 340, 32));
}

static struct iio_channel **get_ext_channels(struct device *dev,
		 const char *const *channel_map, int size)
{
	int i, rc = 0;
	struct iio_channel **iio_ch_ext;

	iio_ch_ext = devm_kcalloc(dev, size, sizeof(*iio_ch_ext), GFP_KERNEL);
	if (!iio_ch_ext)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < size; i++) {
		iio_ch_ext[i] = devm_iio_channel_get(dev, channel_map[i]);

		if (IS_ERR(iio_ch_ext[i])) {
			rc = PTR_ERR(iio_ch_ext[i]);
			if (rc != -EPROBE_DEFER)
				dev_err(dev, "%s channel unavailable, %d\n",
						channel_map[i], rc);
			return ERR_PTR(rc);
		}
	}

	return iio_ch_ext;
}

static bool is_cps_available(struct smb1398_chip *chip)
{
	int rc = 0;
	struct iio_channel **iio_list;

	if (IS_ERR(chip->cp_slave_iio_chan_list))
		return false;

	if (!chip->cp_slave_iio_chan_list) {
		iio_list = get_ext_channels(chip->dev,
			cp_slave_iio_chans, ARRAY_SIZE(cp_slave_iio_chans));
		if (IS_ERR(iio_list)) {
			rc = PTR_ERR(iio_list);
			if (rc != -EPROBE_DEFER) {
				dev_err(chip->dev, "Failed to get channels, rc=%d\n",
						rc);
				chip->cp_slave_iio_chan_list = ERR_PTR(-EINVAL);
			}
			return false;
		}
		chip->cp_slave_iio_chan_list = iio_list;
	}

	return true;
}

static int smb1398_div2_cp_get_master_isns(
		struct smb1398_chip *chip, int *isns_ua)
{
	int rc = 0, temp, val;

	rc = smb1398_get_enable_status(chip);
	if (rc < 0)
		return rc;

	if (!chip->smb_en)
		return -ENODATA;

	/*
	 * Follow this procedure to read master CP ISNS:
	 *   set slave CP TEMP_MUX to HighZ;
	 *   set master CP TEMP_MUX to IIN_FB;
	 *   set DIV2_CP switch phase-shift to 0 deg;
	 *   read corresponding ADC channel in Kekaha;
	 *   set DIV2_CP switch phase-shif back to 90 deg;
	 *   set master CP TEMP_MUX to VTEMP;
	 */
	mutex_lock(&chip->die_chan_lock);
	if (is_cps_available(chip)) {
		val = ISNS_MODE_OFF;
		rc = cp_write_iio_prop(chip, CP_SLAVE,
				CURRENT_CAPABILITY, val);
		if (rc < 0) {
			dev_err(chip->dev, "Couldn't set slave ISNS_MODE_OFF, rc=%d\n",
					rc);
			goto unlock;
		}
	}

	rc = smb1398_div2_cp_isns_mode_control(chip, ISNS_MODE_ACTIVE);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set master ISNS_MODE_ACTIVE, rc=%d\n",
				rc);
		goto unlock;
	}

	rc = smb1398_masked_write(chip, PERPH0_DIV2_SLAVE,
					CFG_DIV2_SYNC_CLK_PHASE_90, 0);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set PERPH0_DIV2_SLAVE, rc=%d\n",
				rc);
		goto unlock;
	}

	/* Delay for the phase switch to take effect */
	msleep(20);

	rc = iio_read_channel_processed(chip->die_temp_chan, &temp);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read die_temp_chan, rc=%d\n", rc);
		goto unlock;
	}

	rc = smb1398_masked_write(chip, PERPH0_DIV2_SLAVE,
			CFG_DIV2_SYNC_CLK_PHASE_90, CFG_DIV2_SYNC_CLK_PHASE_90);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set PERPH0_DIV2_SLAVE, rc=%d\n",
				rc);
		goto unlock;
	}

	rc = smb1398_div2_cp_isns_mode_control(chip, ISNS_MODE_STANDBY);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set master ISNS_MODE_STANDBY, rc=%d\n",
				rc);
		goto unlock;
	}

unlock:
	mutex_unlock(&chip->die_chan_lock);
	if (rc >= 0) {
		*isns_ua = calculate_div2_cp_isns_ua(temp);
		dev_dbg(chip->dev, "master isns = %duA\n", *isns_ua);
	}

	return rc;
}

static int smb1398_div2_cp_get_slave_isns(
		struct smb1398_chip *chip, int *isns_ua)
{
	int temp = 0, rc, val;

	if (!is_cps_available(chip)) {
		*isns_ua = 0;
		return 0;
	}

	rc = smb1398_get_enable_status(chip);
	if (rc < 0)
		return rc;

	if (!chip->smb_en || !chip->slave_en)
		return -ENODATA;

	/*
	 * Follow this procedure to read slave CP ISNS:
	 *   set master CP TEMP_MUX to HighZ;
	 *   set slave CP TEMP_MUX to IIN_FB;
	 *   set DIV2_CP switch phase-shift to 0 deg;
	 *   read corresponding ADC channel in Kekaha;
	 *   set DIV2_CP switch phase-shif back to 90 deg;
	 *   set master CP TEMP_MUX to VTEMP;
	 */
	mutex_lock(&chip->die_chan_lock);
	rc = smb1398_div2_cp_isns_mode_control(chip, ISNS_MODE_OFF);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set master ISNS_MODE_OFF, rc=%d\n",
				rc);
		goto unlock;
	}

	val = ISNS_MODE_ACTIVE;
	rc = cp_write_iio_prop(chip, CP_SLAVE, CURRENT_CAPABILITY, val);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set slave ISNS_MODE_ACTIVE, rc=%d\n",
				rc);
		goto unlock;
	}

	rc = smb1398_masked_write(chip, PERPH0_DIV2_SLAVE,
					CFG_DIV2_SYNC_CLK_PHASE_90, 0);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set PERPH0_DIV2_SLAVE, rc=%d\n",
				rc);
		goto unlock;
	}

	/* Delay for the phase switch to take effect */
	msleep(20);

	rc = iio_read_channel_processed(chip->die_temp_chan, &temp);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't get die_temp_chan, rc=%d\n", rc);
		goto unlock;
	}

	rc = smb1398_masked_write(chip, PERPH0_DIV2_SLAVE,
			CFG_DIV2_SYNC_CLK_PHASE_90, CFG_DIV2_SYNC_CLK_PHASE_90);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set PERPH0_DIV2_SLAVE, rc=%d\n",
				rc);
		goto unlock;
	}

	rc = smb1398_div2_cp_isns_mode_control(chip, ISNS_MODE_STANDBY);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set master ISNS_MODE_STANDBY, rc=%d\n",
				rc);
		goto unlock;
	}
unlock:
	mutex_unlock(&chip->die_chan_lock);

	if (rc >= 0) {
		*isns_ua = calculate_div2_cp_isns_ua(temp);
		dev_dbg(chip->dev, "slave isns = %duA\n", *isns_ua);
	}

	return rc;
}

static void smb1398_toggle_switcher(struct smb1398_chip *chip)
{
	int rc = 0;

	/*
	 * Disable DIV2_ILIM detection before toggling the switcher
	 * to prevent any ILIM interrupt storm while the toggling
	 */
	rc = smb1398_masked_write(chip, DIV2_CURRENT_REG, DIV2_EN_ILIM_DET, 0);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't disable EN_ILIM_DET, rc=%d\n", rc);

	vote(chip->div2_cp_disable_votable, SWITCHER_TOGGLE_VOTER, true, 0);

	/* Delay for toggling switcher */
	usleep_range(20, 30);
	vote(chip->div2_cp_disable_votable, SWITCHER_TOGGLE_VOTER, false, 0);

	rc = smb1398_masked_write(chip, DIV2_CURRENT_REG,
			DIV2_EN_ILIM_DET, DIV2_EN_ILIM_DET);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't disable EN_ILIM_DET, rc=%d\n", rc);
}

#define DEFAULT_HVDCP3_MIN_ICL_UA 1000000
static int smb1398_div2_cp_get_min_icl(struct smb1398_chip *chip)
{
	int min_ilim = chip->div2_cp_min_ilim_ua;

	/* Use max(dt_min_icl, 1A) for HVDCP3 */
	if (chip->adapter_type == QTI_POWER_SUPPLY_TYPE_USB_HVDCP_3)
		min_ilim = max(chip->div2_cp_min_ilim_ua,
				DEFAULT_HVDCP3_MIN_ICL_UA);
	return min_ilim;
}

static char *div2_cp_get_model_name(struct smb1398_chip *chip)
{
	if (IS_SMB1394(chip->div2_cp_role))
		return "SMB1394";

	if (chip->rev4 > 2)
		return "SMB1398_V3";
	else if (chip->rev4 == 2)
		return "SMB1398_V2";
	else
		return "SMB1398_V1";
}

static int smb1398_toggle_uvlo(struct smb1398_chip *chip)
{
	int rc;

	rc = smb1398_masked_write(chip, PERPH0_SOVP_CFG0_REG,
				CFG_OVP_IGNORE_UVLO, CFG_OVP_IGNORE_UVLO);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't write IGNORE_UVLO rc=%d\n", rc);

	rc = smb1398_masked_write(chip, PERPH0_SOVP_CFG0_REG,
				CFG_OVP_IGNORE_UVLO, 0);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't write IGNORE_UVLO, rc=%d\n", rc);

	return rc;
}

static enum power_supply_property div2_cp_master_props[] = {
	POWER_SUPPLY_PROP_MODEL_NAME,
};

static int div2_cp_master_get_prop(struct power_supply *psy,
				enum power_supply_property prop,
				union power_supply_propval *val)
{
	struct smb1398_chip *chip = power_supply_get_drvdata(psy);
	int rc = 0;

	switch (prop) {
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = div2_cp_get_model_name(chip);
		break;
	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static struct power_supply_desc div2_cp_master_desc = {
	.name			= "charge_pump_master",
	.type			= POWER_SUPPLY_TYPE_MAINS,
	.properties		= div2_cp_master_props,
	.num_properties		= ARRAY_SIZE(div2_cp_master_props),
	.get_property		= div2_cp_master_get_prop,
};

static int smb1398_init_div2_cp_master_psy(struct smb1398_chip *chip)
{
	struct power_supply_config div2_cp_master_psy_cfg = {};
	int rc = 0;

	div2_cp_master_psy_cfg.drv_data = chip;
	div2_cp_master_psy_cfg.of_node = chip->dev->of_node;

	chip->div2_cp_master_psy = devm_power_supply_register(chip->dev,
			&div2_cp_master_desc, &div2_cp_master_psy_cfg);
	if (IS_ERR(chip->div2_cp_master_psy)) {
		rc = PTR_ERR(chip->div2_cp_master_psy);
		dev_err(chip->dev, "Register div2_cp_master power supply failed, rc=%d\n",
				rc);
		return rc;
	}

	return 0;
}

static bool is_psy_voter_available(struct smb1398_chip *chip)
{
	if (!chip->batt_psy) {
		chip->batt_psy = power_supply_get_by_name("battery");
		if (!chip->batt_psy) {
			dev_dbg(chip->dev, "Couldn't find battery psy\n");
			return false;
		}
	}

	if (!chip->usb_psy) {
		chip->usb_psy = power_supply_get_by_name("usb");
		if (!chip->usb_psy) {
			dev_dbg(chip->dev, "Couldn't find usb psy\n");
			return false;
		}
	}

	if (!chip->dc_psy) {
		chip->dc_psy = power_supply_get_by_name("dc");
		if (!chip->dc_psy) {
			dev_dbg(chip->dev, "Couldn't find DC psy\n");
			return false;
		}
	}

	if (!chip->fcc_votable) {
		chip->fcc_votable = find_votable("FCC");
		if (!chip->fcc_votable) {
			dev_dbg(chip->dev, "Couldn't find FCC voltable\n");
			return false;
		}
	}

	if (!chip->fv_votable) {
		chip->fv_votable = find_votable("FV");
		if (!chip->fv_votable) {
			dev_dbg(chip->dev, "Couldn't find FV voltable\n");
			return false;
		}
	}

	if (!chip->usb_icl_votable) {
		chip->usb_icl_votable = find_votable("USB_ICL");
		if (!chip->usb_icl_votable) {
			dev_dbg(chip->dev, "Couldn't find USB_ICL voltable\n");
			return false;
		}
	}

	if (!chip->fcc_main_votable) {
		chip->fcc_main_votable = find_votable("FCC_MAIN");
		if (!chip->fcc_main_votable) {
			dev_dbg(chip->dev, "Couldn't find FCC_MAIN voltable\n");
			return false;
		}
	}

	return true;
}

static bool is_cutoff_soc_reached(struct smb1398_chip *chip)
{
	int rc;
	union power_supply_propval pval = {0};

	if (!chip->batt_psy)
		goto err;

	rc = power_supply_get_property(chip->batt_psy,
			POWER_SUPPLY_PROP_CAPACITY, &pval);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't get battery soc, rc=%d\n", rc);
		goto err;
	}

	if (pval.intval >= chip->max_cutoff_soc)
		return true;
err:
	return false;
}

static bool is_adapter_in_cc_mode(struct smb1398_chip *chip)
{
	int rc, val = 0;

	if (IS_ERR_OR_NULL(chip->smb5_iio_chan_list))
		return false;

	rc = cp_read_iio_prop(chip, QPNP_SMB5, ADAPTER_CC_MODE, &val);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't get ADAPTER_CC_MODE, rc=%d\n", rc);
		return false;
	}

	return !!val;
}

static int smb1398_awake_vote_cb(struct votable *votable,
		void *data, int awake, const char *client)
{
	struct smb1398_chip *chip = (struct smb1398_chip *)data;

	if (awake)
		pm_stay_awake(chip->dev);
	else
		pm_relax(chip->dev);

	return 0;
}

static int smb1398_div2_cp_disable_vote_cb(struct votable *votable,
		void *data, int disable, const char *client)
{
	struct smb1398_chip *chip = (struct smb1398_chip *)data;
	int rc = 0;

	if (!is_psy_voter_available(chip) || chip->in_suspend)
		return -EAGAIN;

	rc = smb1398_div2_cp_switcher_en(chip, !disable);
	if (rc < 0) {
		dev_err(chip->dev, "%s switcher failed, rc=%d\n",
				!!disable ? "disable" : "enable", rc);
		return rc;
	}

	if (is_cps_available(chip))
		vote(chip->div2_cp_slave_disable_votable, MAIN_DISABLE_VOTER,
				!!disable ? true : false, 0);

	if (chip->div2_cp_master_psy && (disable !=  chip->disabled))
		power_supply_changed(chip->div2_cp_master_psy);

	chip->disabled = disable;
	return 0;
}

static int smb1398_div2_cp_slave_disable_vote_cb(struct votable *votable,
		void *data, int disable, const char *client)
{
	struct smb1398_chip *chip = (struct smb1398_chip *)data;
	u16 reg;
	u8 val;
	int rc, ilim_ua, value;

	if (!is_cps_available(chip))
		return -ENODEV;

	reg = MISC_SL_SWITCH_EN_REG;
	val = !!disable ? 0 : EN_SLAVE;
	rc = smb1398_masked_write(chip, reg, EN_SLAVE, val);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't write slave_en, rc=%d\n", rc);
		return rc;
	}

	value = !disable;
	rc = cp_write_iio_prop(chip, CP_SLAVE, CP_ENABLE, value);
	if (rc < 0) {
		dev_err(chip->dev, "%s slave switcher failed, rc=%d\n",
				!!disable ? "disable" : "enable", rc);
		return rc;
	}

	/* Re-distribute ILIM to Master CP when Slave is disabled */
	if (disable && (chip->div2_cp_ilim_votable)) {
		ilim_ua = get_effective_result_locked(
				chip->div2_cp_ilim_votable);

		ilim_ua = (ilim_ua * DIV2_ILIM_CFG_PCT) / 100;

		if (ilim_ua > DIV2_MAX_ILIM_UA)
			ilim_ua = DIV2_MAX_ILIM_UA;

		rc = smb1398_set_iin_ma(chip, ilim_ua / 1000);
		if (rc < 0) {
			dev_err(chip->dev, "Could't set CP master ilim, rc=%d\n",
					rc);
			return rc;
		}
		dev_dbg(chip->dev, "slave disabled, restore master CP ilim to %duA\n",
				ilim_ua);
	}

	return rc;
}

static int smb1398_div2_cp_ilim_vote_cb(struct votable *votable,
		void *data, int ilim_ua, const char *client)
{
	struct smb1398_chip *chip = (struct smb1398_chip *)data;
	int rc = 0, max_ilim_ua, min_ilim_ua, val;
	bool slave_dis, split_ilim = false;

	if (!is_psy_voter_available(chip) || chip->in_suspend)
		return -EAGAIN;

	if (!client)
		return -EINVAL;

	min_ilim_ua = smb1398_div2_cp_get_min_icl(chip);

	ilim_ua = (ilim_ua * DIV2_ILIM_CFG_PCT) / 100;

	max_ilim_ua = is_cps_available(chip) ?
		DIV2_MAX_ILIM_DUAL_CP_UA : DIV2_MAX_ILIM_UA;
	ilim_ua = min(ilim_ua, max_ilim_ua);
	if (ilim_ua < min_ilim_ua) {
		dev_dbg(chip->dev, "ilim %duA is too low to config CP charging\n",
				ilim_ua);
		vote(chip->div2_cp_disable_votable, ILIM_VOTER, true, 0);
	} else {
		if (is_cps_available(chip)) {
			split_ilim = true;
			slave_dis = ilim_ua < (2 * min_ilim_ua);
			vote(chip->div2_cp_slave_disable_votable, ILIM_VOTER,
					slave_dis, 0);
			slave_dis = !!get_effective_result(
					chip->div2_cp_slave_disable_votable);
			if (slave_dis)
				split_ilim = false;
		}

		if (split_ilim) {
			ilim_ua /= 2;
			val = ilim_ua;
			rc = cp_write_iio_prop(chip, CP_SLAVE,
					CP_INPUT_CURRENT_MAX, val);
			if (rc < 0)
				dev_err(chip->dev, "Couldn't set CP slave ilim, rc=%d\n",
						rc);
			dev_dbg(chip->dev, "set CP slave ilim to %duA\n",
					ilim_ua);
		}

		rc = smb1398_set_iin_ma(chip, ilim_ua / 1000);
		if (rc < 0) {
			dev_err(chip->dev, "Couldn't set CP master ilim, rc=%d\n",
					rc);
			return rc;
		}
		dev_dbg(chip->dev, "set CP master ilim to %duA\n", ilim_ua);
		vote(chip->div2_cp_disable_votable, ILIM_VOTER, false, 0);
	}

	return 0;
}

static void smb1398_destroy_votables(struct smb1398_chip *chip)
{
	destroy_votable(chip->awake_votable);
	destroy_votable(chip->div2_cp_disable_votable);
	destroy_votable(chip->div2_cp_ilim_votable);
	destroy_votable(chip->div2_cp_slave_disable_votable);
}

static int smb1398_div2_cp_create_votables(struct smb1398_chip *chip)
{
	int rc;

	chip->awake_votable = create_votable("SMB1398_AWAKE",
			VOTE_SET_ANY, smb1398_awake_vote_cb, chip);
	if (IS_ERR_OR_NULL(chip->awake_votable))
		return PTR_ERR_OR_ZERO(chip->awake_votable);

	chip->div2_cp_disable_votable = create_votable("CP_DISABLE",
			VOTE_SET_ANY, smb1398_div2_cp_disable_vote_cb, chip);
	if (IS_ERR_OR_NULL(chip->div2_cp_disable_votable)) {
		rc = PTR_ERR_OR_ZERO(chip->div2_cp_disable_votable);
		goto destroy;
	}

	chip->div2_cp_slave_disable_votable = create_votable("CP_SLAVE_DISABLE",
			VOTE_SET_ANY, smb1398_div2_cp_slave_disable_vote_cb,
			chip);
	if (IS_ERR_OR_NULL(chip->div2_cp_slave_disable_votable)) {
		rc = PTR_ERR_OR_ZERO(chip->div2_cp_slave_disable_votable);
		goto destroy;
	}

	chip->div2_cp_ilim_votable = create_votable("CP_ILIM",
			VOTE_MIN, smb1398_div2_cp_ilim_vote_cb, chip);
	if (IS_ERR_OR_NULL(chip->div2_cp_ilim_votable)) {
		rc = PTR_ERR_OR_ZERO(chip->div2_cp_ilim_votable);
		goto destroy;
	}

	vote(chip->div2_cp_disable_votable, USER_VOTER, true, 0);
	vote(chip->div2_cp_disable_votable, CUTOFF_SOC_VOTER,
			is_cutoff_soc_reached(chip), 0);

	/*
	 * In case SMB1398 probe happens after FCC value has been configured,
	 * update ilim vote to reflect FCC / 2 value, this is only applicable
	 * when SMB1398 is directly connected to VBAT.
	 */
	if (is_psy_voter_available(chip) &&
		(chip->pl_output_mode != QTI_POWER_SUPPLY_PL_OUTPUT_VPH))
		vote(chip->div2_cp_ilim_votable, FCC_VOTER, true,
			get_effective_result(chip->fcc_votable) / 2);
	return 0;
destroy:
	smb1398_destroy_votables(chip);

	return 0;
}

static irqreturn_t smb1398_default_irq_handler(int irq, void *data)
{
	struct smb1398_chip *chip = data;
	int rc, i;
	bool switcher_en = chip->switcher_en;

	for (i = 0; i < NUM_IRQS; i++) {
		if (irq == chip->irqs[i]) {
			dev_dbg(chip->dev, "IRQ %s triggered\n",
					smb_irqs[i].name);
			chip->div2_irq_status |= 1 << smb_irqs[i].shift;
		}
	}

	rc = smb1398_get_enable_status(chip);
	if (rc < 0)
		goto out;

	if (chip->switcher_en != switcher_en)
		if (chip->fcc_votable)
			rerun_election(chip->fcc_votable);
out:
	if (chip->div2_cp_master_psy)
		power_supply_changed(chip->div2_cp_master_psy);
	return IRQ_HANDLED;
}

static const struct smb_irq smb_irqs[] = {
	/* useful IRQs from perph0 */
	[TEMP_SHDWN_IRQ]	= {
		.name		= "temp-shdwn",
		.handler	= smb1398_default_irq_handler,
		.wake		= true,
		.shift		= 2,
	},
	[DIV2_IREV_LATCH_IRQ]	= {
		.name		= "div2-irev",
		.handler	= smb1398_default_irq_handler,
		.wake		= true,
		.shift		= 3,
	},
	[USB_IN_UVLO_IRQ]	= {
		.name		= "usbin-uv",
		.handler	= smb1398_default_irq_handler,
		.wake		= true,
		.shift		= 1,
	},
	[USB_IN_OVLO_IRQ]	= {
		.name		= "usbin-ov",
		.handler	= smb1398_default_irq_handler,
		.wake		= true,
		.shift		= 1,
	},
	/* useful IRQs from perph1 */
	[DIV2_ILIM_IRQ]		= {
		.name		= "div2-ilim",
		.handler	= smb1398_default_irq_handler,
		.wake		= true,
		.shift		= 6,
	},
	[DIV2_WIN_UV_IRQ]	= {
		.name		= "div2-win-uv",
		.handler	= smb1398_default_irq_handler,
		.wake		= true,
		.shift		= 0,
	},
	[DIV2_WIN_OV_IRQ]	= {
		.name		= "div2-win-ov",
		.handler	= smb1398_default_irq_handler,
		.wake		= true,
		.shift		= 0,
	},
};

static int smb1398_get_irq_index_byname(const char *irq_name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smb_irqs); i++) {
		if (smb_irqs[i].name != NULL)
			if (strcmp(smb_irqs[i].name, irq_name) == 0)
				return i;
	}

	return -ENOENT;
}

static int smb1398_request_interrupt(struct smb1398_chip *chip,
		struct device_node *node, const char *irq_name)
{
	int rc = 0, irq, irq_index;

	irq = of_irq_get_byname(node, irq_name);
	if (irq < 0) {
		dev_err(chip->dev, "Couldn't get irq %s failed\n", irq_name);
		return irq;
	}

	irq_index = smb1398_get_irq_index_byname(irq_name);
	if (irq_index < 0) {
		dev_err(chip->dev, "%s IRQ is not defined\n", irq_name);
		return irq_index;
	}

	if (!smb_irqs[irq_index].handler)
		return 0;

	/*
	 * Do not register temp-shdwn interrupt as it may misfire on toggling
	 * the SMB_EN input.
	 */
	if (irq_index == TEMP_SHDWN_IRQ)
		return 0;

	rc = devm_request_threaded_irq(chip->dev, irq, NULL,
			smb_irqs[irq_index].handler,
			IRQF_ONESHOT, irq_name, chip);
	if (rc < 0) {
		dev_err(chip->dev, "Request interrupt for %s failed, rc=%d\n",
				irq_name, rc);
		return rc;
	}

	chip->irqs[irq_index] = irq;
	if (smb_irqs[irq_index].wake)
		enable_irq_wake(irq);

	return 0;
}

static int smb1398_request_interrupts(struct smb1398_chip *chip)
{
	struct device_node *node = chip->dev->of_node;
	int rc = 0;
	const char *name;
	struct property *prop;

	of_property_for_each_string(node, "interrupt-names", prop, name) {
		rc = smb1398_request_interrupt(chip, node, name);
		if (rc < 0)
			return rc;
	}

	return 0;
}

#define ILIM_NR			10
#define ILIM_DR			8
#define ILIM_FACTOR(ilim)	((ilim * ILIM_NR) / ILIM_DR)

static void smb1398_configure_ilim(struct smb1398_chip *chip, int mode)
{
	int rc, val = 0;

	/* PPS adapter reply on the current advertised by the adapter */
	if ((chip->pl_output_mode == QTI_POWER_SUPPLY_PL_OUTPUT_VPH)
			&& (mode == QTI_POWER_SUPPLY_CP_PPS)) {
		rc = cp_read_iio_prop(chip, QPNP_SMB5, PD_CURRENT_MAX, &val);
		if (rc < 0)
			pr_err("Couldn't get PD CURRENT MAX rc=%d\n", rc);
		else
			vote(chip->div2_cp_ilim_votable, ICL_VOTER,
					true, ILIM_FACTOR(val));
	}

	/* QC3.0/Wireless adapter rely on the settled AICL for USBMID_USBMID */
	if ((chip->pl_input_mode == QTI_POWER_SUPPLY_PL_USBMID_USBMID)
			&& (mode == QTI_POWER_SUPPLY_CP_HVDCP3)) {
		rc = cp_read_iio_prop(chip, QPNP_SMB5, INPUT_CURRENT_SETTLED,
			&val);
		if (rc < 0)
			pr_err("Couldn't get usb aicl rc=%d\n", rc);
		else
			vote(chip->div2_cp_ilim_votable, ICL_VOTER,
					true, val);
	}
}

static void smb1398_status_change_work(struct work_struct *work)
{
	struct smb1398_chip *chip = container_of(work,
			struct smb1398_chip, status_change_work);
	union power_supply_propval pval = {0};
	int rc, val = 0;

	if (!is_psy_voter_available(chip))
		goto out;
	/*
	 * If batt soc is not valid upon bootup, but becomes
	 * valid due to the battery discharging later, remove
	 * vote from CUTOFF_SOC_VOTER.
	 */
	if (!is_cutoff_soc_reached(chip))
		vote(chip->div2_cp_disable_votable, CUTOFF_SOC_VOTER, false, 0);

	rc = power_supply_get_property(chip->usb_psy,
			POWER_SUPPLY_PROP_PRESENT, &pval);
	if (rc < 0) {
		dev_err(chip->dev,
			"Couldn't get USB PRESENT status, rc=%d\n", rc);
		goto out;
	}

	if (chip->usb_present != !!pval.intval) {
		chip->usb_present = !!pval.intval;
		if (!chip->usb_present) /* USB has been removed */
			smb1398_toggle_uvlo(chip);
	}

	rc = cp_read_iio_prop(chip, QPNP_SMB5, SMB_EN_MODE, &val);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't get SMB_EN_MODE, rc=%d\n", rc);
		goto out;
	}

	/* If no CP charging started */
	if (val != QTI_POWER_SUPPLY_CHARGER_SEC_CP) {
		chip->cutoff_soc_checked = false;
		vote(chip->div2_cp_slave_disable_votable, SRC_VOTER, true, 0);
		vote(chip->div2_cp_slave_disable_votable,
				TAPER_VOTER, false, 0);
		vote(chip->div2_cp_disable_votable, TAPER_VOTER, false, 0);
		vote(chip->div2_cp_disable_votable, SRC_VOTER, true, 0);
		vote(chip->div2_cp_disable_votable, CUTOFF_SOC_VOTER, true, 0);
		vote(chip->fcc_votable, CP_VOTER, false, 0);
		vote(chip->div2_cp_ilim_votable, CC_MODE_VOTER, false, 0);
		vote_override(chip->usb_icl_votable,
				TAPER_MAIN_ICL_LIMIT_VOTER, false, 0);
		goto out;
	}

	rc = cp_read_iio_prop(chip, QPNP_SMB5, REAL_TYPE, &chip->adapter_type);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't get REAL_TYPE, rc=%d\n", rc);
		goto out;
	}

	rc = cp_read_iio_prop(chip, QPNP_SMB5, SMB_EN_REASON, &val);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't get SMB_EN_REASON failed, rc=%d\n",
				rc);
		goto out;
	}

	/*
	 * Slave SMB1398 is not required for the power-rating of QC3
	 */
	if (val != QTI_POWER_SUPPLY_CP_HVDCP3)
		vote(chip->div2_cp_slave_disable_votable, SRC_VOTER, false, 0);

	if (val == QTI_POWER_SUPPLY_CP_NONE) {
		vote(chip->div2_cp_disable_votable, SRC_VOTER, true, 0);
		goto out;
	}

	vote(chip->div2_cp_disable_votable, SRC_VOTER, false, 0);
	if (!chip->cutoff_soc_checked) {
		vote(chip->div2_cp_disable_votable, CUTOFF_SOC_VOTER,
				is_cutoff_soc_reached(chip), 0);
		chip->cutoff_soc_checked = true;
	}

	if (val == QTI_POWER_SUPPLY_CP_WIRELESS) {
		/*
		 * Get the max output current from the wireless PSY
		 * and set the DIV2 CP ilim accordingly
		 */
		vote(chip->div2_cp_ilim_votable, ICL_VOTER, false, 0);
		rc = power_supply_get_property(chip->dc_psy,
				POWER_SUPPLY_PROP_CURRENT_MAX, &pval);
		if (rc < 0)
			dev_err(chip->dev, "Couldn't get DC CURRENT_MAX, rc=%d\n",
					rc);
		else
			vote(chip->div2_cp_ilim_votable, WIRELESS_VOTER,
					true, pval.intval);
	} else {
		vote(chip->div2_cp_ilim_votable, WIRELESS_VOTER, false, 0);
		smb1398_configure_ilim(chip, pval.intval);
	}

	/*
	 * Remove CP Taper condition disable vote if float voltage
	 * increased in comparison to voltage at which it entered taper.
	 */
	if (chip->taper_entry_fv < get_effective_result(chip->fv_votable)) {
		vote(chip->div2_cp_slave_disable_votable,
				TAPER_VOTER, false, 0);
		vote(chip->div2_cp_disable_votable, TAPER_VOTER, false, 0);
	}

	/*
	 * all votes that would result in disabling the charge pump have
	 * been cast; ensure the charge pump is still enabled before
	 * continuing.
	 */
	if (get_effective_result(chip->div2_cp_disable_votable))
		goto out;

	rc = power_supply_get_property(chip->batt_psy,
			POWER_SUPPLY_PROP_CHARGE_TYPE, &pval);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't get CHARGE_TYPE, rc=%d\n",
				rc);
		goto out;
	}

	if (pval.intval == POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE) {
		if (!chip->taper_work_running) {
			chip->taper_work_running = true;
			vote(chip->awake_votable, TAPER_VOTER, true, 0);
			queue_work(system_long_wq, &chip->taper_work);
		}
	}
out:
	pm_relax(chip->dev);
	chip->status_change_running = false;
}

static int smb1398_notifier_cb(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct smb1398_chip *chip = container_of(nb, struct smb1398_chip, nb);
	struct power_supply *psy = (struct power_supply *)data;
	unsigned long flags;

	if (event != PSY_EVENT_PROP_CHANGED)
		return NOTIFY_OK;

	if (strcmp(psy->desc->name, "battery") == 0 ||
			strcmp(psy->desc->name, "usb") == 0 ||
			strcmp(psy->desc->name, "cp_slave") == 0) {
		spin_lock_irqsave(&chip->status_change_lock, flags);
		if (!chip->status_change_running) {
			chip->status_change_running = true;
			pm_stay_awake(chip->dev);
			schedule_work(&chip->status_change_work);
		}
		spin_unlock_irqrestore(&chip->status_change_lock, flags);
	}

	return NOTIFY_OK;
}

static void smb1398_taper_work(struct work_struct *work)
{
	struct smb1398_chip *chip = container_of(work,
			struct smb1398_chip, taper_work);
	union power_supply_propval pval = {0};
	int rc, fcc_ua, fv_uv, stepper_ua, main_fcc_ua = 0, min_ilim_ua;
	bool slave_en;

	if (!is_psy_voter_available(chip))
		goto out;

	if (!chip->fcc_main_votable)
		chip->fcc_main_votable = find_votable("FCC_MAIN");

	if (chip->fcc_main_votable)
		main_fcc_ua = get_effective_result(chip->fcc_main_votable);

	min_ilim_ua = smb1398_div2_cp_get_min_icl(chip);

	chip->taper_entry_fv = get_effective_result(chip->fv_votable);
	while (true) {
		rc = power_supply_get_property(chip->batt_psy,
			POWER_SUPPLY_PROP_CHARGE_TYPE, &pval);
		if (rc < 0) {
			dev_err(chip->dev, "Couldn't get CHARGE_TYPE, rc=%d\n",
					rc);
			goto out;
		}

		fv_uv = get_effective_result(chip->fv_votable);
		if (fv_uv > chip->taper_entry_fv) {
			dev_dbg(chip->dev, "Float voltage increased (%d-->%d)uV, exit!\n",
					chip->taper_entry_fv, fv_uv);
			vote(chip->div2_cp_disable_votable, TAPER_VOTER,
					false, 0);
			goto out;
		} else {
			chip->taper_entry_fv = fv_uv;
		}

		if (pval.intval == POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE) {
			stepper_ua = is_adapter_in_cc_mode(chip) ?
				TAPER_STEPPER_UA_IN_CC_MODE :
				TAPER_STEPPER_UA_DEFAULT;
			fcc_ua = get_effective_result(chip->fcc_votable)
				- stepper_ua;
			dev_dbg(chip->dev, "Taper stepper reduce FCC to %d\n",
					fcc_ua);
			vote(chip->fcc_votable, CP_VOTER, true, fcc_ua);
			fcc_ua -= main_fcc_ua;
			/*
			 * If total FCC is less than the minimum ILIM to
			 * keep CP master and slave online, disable CP.
			 */
			if (fcc_ua < (min_ilim_ua * 2)) {
				vote(chip->div2_cp_disable_votable,
						TAPER_VOTER, true, 0);
				/*
				 * When master CP is disabled, reset all votes
				 * on ICL to enable Main charger to pump
				 * charging current.
				 */
				if (chip->usb_icl_votable)
					vote_override(chip->usb_icl_votable,
						TAPER_MAIN_ICL_LIMIT_VOTER,
						false, 0);
				goto out;
			}
			/*
			 * If total FCC is less than the minimum ILIM to keep
			 * slave CP online, disable slave, and set master CP
			 * ILIM to maximum to avoid ILIM IRQ storm.
			 */
			slave_en = !get_effective_result(
					chip->div2_cp_slave_disable_votable);
			if ((fcc_ua < chip->ilim_ua_disable_div2_cp_slave) &&
					slave_en && is_cps_available(chip)) {
				dev_dbg(chip->dev, "Disable slave CP in taper\n");
				vote(chip->div2_cp_slave_disable_votable,
						TAPER_VOTER, true, 0);
				vote_override(chip->div2_cp_ilim_votable,
					CC_MODE_VOTER,
					is_adapter_in_cc_mode(chip),
					DIV2_MAX_ILIM_DUAL_CP_UA);

				if (chip->usb_icl_votable)
					vote_override(chip->usb_icl_votable,
					  TAPER_MAIN_ICL_LIMIT_VOTER,
					  is_adapter_in_cc_mode(chip),
					  chip->cc_mode_taper_main_icl_ua);
			}
		} else {
			dev_dbg(chip->dev, "Not in taper, exit!\n");
		}
		msleep(500);
	}
out:
	dev_dbg(chip->dev, "exit taper work\n");
	vote(chip->fcc_votable, CP_VOTER, false, 0);
	vote(chip->awake_votable, TAPER_VOTER, false, 0);
	chip->taper_work_running = false;
}

static int _smb1398_update_ovp(struct smb1398_chip *chip)
{
	int rc = 0;

	/* Ignore for REV2 and below */
	if (chip->rev4 <= 2)
		return 0;

	rc = smb1398_masked_write(chip, PERPH0_SSUPPLY_CFG0_REG,
			EN_HV_OV_OPTION2_BIT | EN_MV_OV_OPTION2_BIT,
			EN_HV_OV_OPTION2_BIT);
	if (rc < 0) {
		dev_err(chip->dev,
			"Couldn't set PERPH0_SSUPPLY_CFG0_REG rc=%d\n", rc);
		return rc;
	}

	rc = smb1398_masked_write(chip, PERPH1_LOCK_SPARE_REG,
				CFG_LOCK_SPARE1_MASK,
				OVP_14V << CFG_LOCK_SPARE1_SHIFT);
	if (rc < 0) {
		dev_err(chip->dev,
			"Couldn't set PERPH1_LOCK_SPARE_REG rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int _smb1394_update_ovp(struct smb1398_chip *chip)
{
	int rc = 0;

	rc = smb1398_masked_write(chip, PERPH0_SOVP_CFG0_REG,
			CFG_OVP_VSNS_THRESHOLD, CFG_OVP_VSNS_THRESHOLD);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set PERPH0_SOVP_CFG0_REG rc=%d\n",
				rc);
		return rc;
	}

	rc = smb1398_masked_write(chip, PERPH0_OVLO_REF_REG,
			SMB1394_INPUT_OVLO_CONF_MASK,
			SMB1394_INPUT_OVLO_13P04V);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set PERPH0_OVLO_REF rc=%d\n", rc);
		return rc;
	}

	rc = smb1398_masked_write(chip, PERPH0_CFG_SDCDC_REG,
		EN_WIN_OV_RISE_DEB_BIT, 0);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't set PERPH0_CFG_SDCDC_REG rc=%d\n",
			rc);
	return rc;
}

static int smb1398_update_ovp(struct smb1398_chip *chip)
{
	if (IS_SMB1394(chip->div2_cp_role))
		return _smb1394_update_ovp(chip);

	return _smb1398_update_ovp(chip);
}

static int smb1398_div2_cp_hw_init(struct smb1398_chip *chip)
{
	int rc = 0;

	rc = smb1398_update_ovp(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't update OVP threshold rc=%d\n", rc);
		return rc;
	}

	/* Configure window (Vin/2 - Vout) OV level to 500mV */
	rc = smb1398_masked_write(chip, DIV2_PROTECTION_REG,
			DIV2_WIN_OV_SEL_MASK, WIN_OV_500_MV);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set WIN_OV_500_MV rc=%d\n", rc);
		return rc;
	}

	/* Configure window (Vin/2 - Vout) UV level to 10mV */
	rc = smb1398_masked_write(chip, NOLOCK_SPARE_REG,
			DIV2_WIN_UV_SEL_BIT, 0);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set WIN_UV_10_MV rc=%d\n", rc);
		return rc;
	}

	/* Configure master TEMP pin to output Vtemp signal by default */
	rc = smb1398_masked_write(chip, SSUPLY_TEMP_CTRL_REG,
			SEL_OUT_TEMP_MAX_MASK, SEL_OUT_VTEMP);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set SSUPLY_TEMP_CTRL_REG, rc=%d\n",
				rc);
		return rc;
	}

	/* Configure to use Vtemp signal */
	rc = smb1398_masked_write(chip, PERPH0_MISC_CFG2_REG,
			CFG_TEMP_PIN_ITEMP, 0);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set PERPH0_MISC_CFG2_REG, rc=%d\n",
				rc);
		return rc;
	}

	/* Configure IREV threshold to 200mA */
	rc = smb1398_masked_write(chip, PERPH0_DIV2_REF_CFG,
			CFG_IREV_REF_BIT, 0);
	if (rc < 0) {
		pr_err("Couldn't configure IREV threshold rc=%d\n", rc);
		return rc;
	}

	/* Initial configuration needed before enabling DIV2_CP operations */
	rc = smb1398_masked_write(chip, MISC_DIV2_3LVL_CTRL_REG,
				MISC_DIV2_3LVL_CTRL_MASK, 0x04);
	if (rc < 0) {
		dev_err(chip->dev, "set EN_DIV2_CP failed, rc=%d\n", rc);
		return rc;
	}

	rc = smb1398_masked_write(chip, MISC_CFG1_REG, MISC_CFG1_MASK, 0x02);
	if (rc < 0) {
		dev_err(chip->dev, "set OP_MODE_COMBO failed, rc=%d\n", rc);
		return rc;
	}

	/* Do not disable FP_FET during IREV conditions */
	rc = smb1398_masked_write(chip, MISC_CFG0_REG, CFG_DIS_FPF_IREV_BIT, 0);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set CFG_DIS_FPF_IREV_BIT, rc=%d\n",
				rc);
		return rc;
	}

	/* switcher enable controlled by register */
	rc = smb1398_masked_write(chip, MISC_CFG0_REG,
			SW_EN_SWITCHER_BIT, SW_EN_SWITCHER_BIT);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set CFG_EN_SOURCE, rc=%d\n",
				rc);
		return rc;
	}

	if (IS_SMB1394(chip->div2_cp_role)) {
		rc = smb1398_masked_write(chip, PERPH0_SSUPPLY_CFG0_REG,
				CFG_CMP_VOUT_VS_4V_REF_MASK,
				CMP_VOUT_VS_4V_REF_3P2V);
		if (rc < 0) {
			dev_err(chip->dev, "Couldn't set PERPH0_SSUPPLY_CFG0_REG, rc=%d\n",
					rc);
			return rc;
		}
	}

	return rc;
}

#define DIV2_CP_MIN_ILIM_UA 1000000
static int smb1398_div2_cp_parse_dt(struct smb1398_chip *chip)
{
	int rc = 0;

	rc = of_property_match_string(chip->dev->of_node,
			"io-channel-names", "die_temp");
	if (rc < 0) {
		dev_err(chip->dev, "die_temp IIO channel not found\n");
		return rc;
	}

	chip->die_temp_chan = devm_iio_channel_get(chip->dev,
			"die_temp");
	if (IS_ERR(chip->die_temp_chan)) {
		rc = PTR_ERR(chip->die_temp_chan);
		if (rc != -EPROBE_DEFER)
			dev_err(chip->dev, "Couldn't get die_temp_chan, rc=%d\n",
					rc);
		chip->die_temp_chan = NULL;
		return rc;
	}

	of_property_read_u32(chip->dev->of_node, "qcom,div2-cp-min-ilim-ua",
			&chip->div2_cp_min_ilim_ua);
	/*
	 * Set minimum allowed ilim configuration to 1A for DIV2_CP
	 * operation.
	 */
	if (chip->div2_cp_min_ilim_ua < DIV2_CP_MIN_ILIM_UA)
		chip->div2_cp_min_ilim_ua = DIV2_CP_MIN_ILIM_UA;

	chip->max_cutoff_soc = 85;
	of_property_read_u32(chip->dev->of_node, "qcom,max-cutoff-soc",
			&chip->max_cutoff_soc);

	chip->ilim_ua_disable_div2_cp_slave = chip->div2_cp_min_ilim_ua * 3;

	of_property_read_u32(chip->dev->of_node, "qcom,ilim-ua-disable-slave",
					&chip->ilim_ua_disable_div2_cp_slave);

	chip->cc_mode_taper_main_icl_ua = CC_MODE_TAPER_MAIN_ICL_UA;
	of_property_read_u32(chip->dev->of_node,
				"qcom,cc-mode-taper-main-icl-ua",
				&chip->cc_mode_taper_main_icl_ua);

	/* Default parallel output configuration is VPH connection */
	chip->pl_output_mode = QTI_POWER_SUPPLY_PL_OUTPUT_VPH;
	of_property_read_u32(chip->dev->of_node, "qcom,parallel-output-mode",
			&chip->pl_output_mode);

	/* Default parallel input configuration is USBMID connection */
	chip->pl_input_mode = QTI_POWER_SUPPLY_PL_USBMID_USBMID;
	of_property_read_u32(chip->dev->of_node, "qcom,parallel-input-mode",
			&chip->pl_input_mode);

	return 0;
}

static int smb1398_div2_cp_master_probe(struct smb1398_chip *chip)
{
	int rc;

	rc = smb1398_read(chip, REVID_REVISION4, &chip->rev4);
	if (rc < 0) {
		dev_err(chip->dev,
			"Couldn't read REVID_REVISION4 rc=%d\n", rc);
		return rc;
	}

	spin_lock_init(&chip->status_change_lock);
	mutex_init(&chip->die_chan_lock);

	rc = smb1398_div2_cp_parse_dt(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't parse devicetree, rc=%d\n", rc);
		return rc;
	}

	INIT_WORK(&chip->status_change_work, &smb1398_status_change_work);
	INIT_WORK(&chip->taper_work, &smb1398_taper_work);

	rc = smb1398_div2_cp_hw_init(chip);
	if (rc < 0) {
		dev_err(chip->dev, "div2_cp_hw_init failed, rc=%d\n", rc);
		return rc;
	}

	rc = smb1398_div2_cp_create_votables(chip);
	if (rc < 0) {
		dev_err(chip->dev, "smb1398_div2_cp_create_votables failed, rc=%d\n",
				rc);
		return rc;
	}

	rc = smb1398_init_div2_cp_master_psy(chip);
	if (rc > 0) {
		dev_err(chip->dev, "smb1398_init_div2_cp_master_psy failed, rc=%d\n",
				rc);
		goto destroy_votable;
	}

	chip->nb.notifier_call = smb1398_notifier_cb;
	rc = power_supply_reg_notifier(&chip->nb);
	if (rc < 0) {
		dev_err(chip->dev, "register notifier_cb failed, rc=%d\n", rc);
		goto destroy_votable;
	}

	rc = smb1398_request_interrupts(chip);
	if (rc < 0) {
		dev_err(chip->dev, "smb1398_request_interrupts failed, rc=%d\n",
				rc);
		goto destroy_votable;
	}

	rc = device_init_wakeup(chip->dev, true);
	if (rc < 0) {
		dev_err(chip->dev, "init wakeup failed for div2_cp_master device, rc=%d\n",
				rc);
		return rc;
	}

	dev_dbg(chip->dev, "smb1398 DIV2_CP master is probed successfully\n");

	return 0;
destroy_votable:
	mutex_destroy(&chip->die_chan_lock);
	smb1398_destroy_votables(chip);

	return rc;
}

static enum power_supply_property div2_cp_slave_props[] = {
	POWER_SUPPLY_PROP_MODEL_NAME,
};

static int div2_cp_slave_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *val)
{
	struct smb1398_chip *chip = power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = div2_cp_get_model_name(chip);
		break;
	default:
		dev_err(chip->dev, "read div2_cp_slave property %d is not supported\n",
				prop);
		return -EINVAL;
	}

	return 0;
}

static const struct power_supply_desc div2_cps_psy_desc = {
	.name = "cp_slave",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.properties = div2_cp_slave_props,
	.num_properties = ARRAY_SIZE(div2_cp_slave_props),
	.get_property = div2_cp_slave_get_prop,
};

static int smb1398_init_div2_cp_slave_psy(struct smb1398_chip *chip)
{
	int rc = 0;
	struct power_supply_config cps_cfg = {};

	cps_cfg.drv_data = chip;
	cps_cfg.of_node = chip->dev->of_node;

	chip->div2_cp_slave_psy = devm_power_supply_register(chip->dev,
			&div2_cps_psy_desc, &cps_cfg);
	if (IS_ERR(chip->div2_cp_slave_psy)) {
		rc = PTR_ERR(chip->div2_cp_slave_psy);
		dev_err(chip->dev, "register div2_cp_slave_psy failed, rc=%d\n",
				rc);
		return rc;
	}

	return 0;
}

static int smb1398_div2_cp_slave_probe(struct smb1398_chip *chip)
{
	int rc;
	u8 status;

	rc = smb1398_read(chip, REVID_REVISION4, &chip->rev4);
	if (rc < 0) {
		dev_err(chip->dev,
			"Couldn't read REVID_REVISION4 rc=%d\n", rc);
		return rc;
	}

	rc = smb1398_update_ovp(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't update OVP threshold rc=%d\n", rc);
		return rc;
	}

	rc = smb1398_read(chip, MODE_STATUS_REG, &status);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read slave MODE_STATUS_REG, rc=%d\n",
				rc);
		return rc;
	}

	/* Configure window (Vin/2 - Vout) UV level to 10mV */
	rc = smb1398_masked_write(chip, NOLOCK_SPARE_REG,
			DIV2_WIN_UV_SEL_BIT, 0);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set WIN_UV_10_MV rc=%d\n", rc);
		return rc;
	}

	/*
	 * Disable slave WIN_UV detection, otherwise slave might not be
	 * enabled due to WIN_UV until master drawing very high current.
	 */
	rc = smb1398_masked_write(chip, PERPH0_CFG_SDCDC_REG, EN_WIN_UV_BIT, 0);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't disable DIV2_CP WIN_UV, rc=%d\n",
				rc);
		return rc;
	}

	/* Configure slave TEMP pin to HIGH-Z by default */
	rc = smb1398_masked_write(chip, SSUPLY_TEMP_CTRL_REG,
			SEL_OUT_TEMP_MAX_MASK, SEL_OUT_HIGHZ);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set SSUPLY_TEMP_CTRL_REG, rc=%d\n",
				rc);
		return rc;
	}

	/* Configure to use Vtemp */
	rc = smb1398_masked_write(chip, PERPH0_MISC_CFG2_REG,
			CFG_TEMP_PIN_ITEMP, 0);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set PERPH0_MISC_CFG2_REG, rc=%d\n",
				rc);
		return rc;
	}

	/* switcher enable controlled by register */
	rc = smb1398_masked_write(chip, MISC_CFG0_REG,
			SW_EN_SWITCHER_BIT, SW_EN_SWITCHER_BIT);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't set MISC_CFG0_REG, rc=%d\n",
				rc);
		return rc;
	}

	if (IS_SMB1394(chip->div2_cp_role)) {
		rc = smb1398_masked_write(chip, PERPH0_SSUPPLY_CFG0_REG,
				CFG_CMP_VOUT_VS_4V_REF_MASK,
				CMP_VOUT_VS_4V_REF_3P2V);
		if (rc < 0) {
			dev_err(chip->dev, "Couldn't set PERPH0_SSUPPLY_CFG0_REG, rc=%d\n",
					rc);
			return rc;
		}

		rc = smb1398_masked_write(chip, PERPH0_DIV2_SLAVE,
				CFG_EN_SLAVE_OWN_FREQ, CFG_EN_SLAVE_OWN_FREQ);
		if (rc < 0) {
			dev_err(chip->dev, "Couldn't set PERPH0_DIV2_SLAVE, rc=%d\n",
					rc);
			return rc;
		}
	} else {
		/* Enable slave clock on its own */
		rc = smb1398_masked_write(chip, NOLOCK_SPARE_REG,
				EN_SLAVE_OWN_FREQ_BIT, EN_SLAVE_OWN_FREQ_BIT);
		if (rc < 0) {
			dev_err(chip->dev, "Couldn't enable slave clock, rc=%d\n",
					rc);
			return rc;
		}
	}

	rc = smb1398_init_div2_cp_slave_psy(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Initial div2_cp_slave_psy failed, rc=%d\n",
				rc);
		return rc;
	}

	dev_dbg(chip->dev, "smb1398 DIV2_CP slave probe successfully\n");

	return 0;
}

static int smb1398_pre_regulator_iout_vote_cb(struct votable *votable,
		void *data, int iout_ua, const char *client)
{
	struct smb1398_chip *chip = (struct smb1398_chip *)data;
	int rc = 0;

	if (chip->in_suspend)
		return -EAGAIN;

	if (!client)
		return -EINVAL;

	iout_ua = min(iout_ua, MAX_IOUT_UA);
	rc = smb1398_set_ichg_ma(chip, (iout_ua / 1000));
	if (rc < 0)
		return rc;

	dev_dbg(chip->dev, "set iout %duA\n", iout_ua);
	return 0;
}

static int smb1398_pre_regulator_vout_vote_cb(struct votable *votable,
		void *data, int vout_uv, const char *client)
{
	struct smb1398_chip *chip = (struct smb1398_chip *)data;
	int rc = 0;

	if (chip->in_suspend)
		return -EAGAIN;

	if (!client)
		return -EINVAL;

	vout_uv = min(vout_uv, MAX_1S_VOUT_UV);
	rc = smb1398_set_1s_vout_mv(chip, vout_uv / 1000);
	if (rc < 0)
		return rc;

	dev_dbg(chip->dev, "set vout %duV\n", vout_uv);
	return 0;
}

static int smb1398_create_pre_regulator_votables(struct smb1398_chip *chip)
{
	chip->pre_regulator_iout_votable = create_votable("PRE_REGULATOR_IOUT",
			VOTE_MIN, smb1398_pre_regulator_iout_vote_cb, chip);
	if (IS_ERR_OR_NULL(chip->pre_regulator_iout_votable))
		return PTR_ERR_OR_ZERO(chip->pre_regulator_iout_votable);

	chip->pre_regulator_vout_votable = create_votable("PRE_REGULATOR_VOUT",
			VOTE_MIN, smb1398_pre_regulator_vout_vote_cb, chip);

	if (IS_ERR_OR_NULL(chip->pre_regulator_vout_votable)) {
		destroy_votable(chip->pre_regulator_iout_votable);
		return PTR_ERR_OR_ZERO(chip->pre_regulator_vout_votable);
	}

	return 0;
}

static enum power_supply_property pre_regulator_props[] = {
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
};

static int pre_regulator_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct smb1398_chip *chip = power_supply_get_drvdata(psy);
	int rc, iin_ma, iout_ma, vout_mv;

	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		rc = smb1398_get_iin_ma(chip, &iin_ma);
		if (rc < 0)
			return rc;
		pval->intval = iin_ma * 1000;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		if (chip->pre_regulator_iout_votable) {
			pval->intval = get_effective_result(
					chip->pre_regulator_iout_votable);
		} else {
			rc = smb1398_get_ichg_ma(chip, &iout_ma);
			if (rc < 0)
				return rc;
			pval->intval = iout_ma * 1000;
		}
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		if (chip->pre_regulator_vout_votable) {
			pval->intval = get_effective_result(
					chip->pre_regulator_vout_votable);
		} else {
			rc = smb1398_get_1s_vout_mv(chip, &vout_mv);
			if (rc < 0)
				return rc;
			pval->intval = vout_mv * 1000;
		}
		break;
	default:
		dev_err(chip->dev, "read pre_regulator property %d is not supported\n",
				prop);
		return -EINVAL;
	}

	return 0;
}

static int pre_regulator_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	struct smb1398_chip *chip = power_supply_get_drvdata(psy);
	int rc = 0;

	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		rc = smb1398_set_iin_ma(chip, pval->intval / 1000);
		if (rc < 0)
			return rc;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		vote(chip->pre_regulator_iout_votable, CP_VOTER,
				true, pval->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		vote(chip->pre_regulator_vout_votable, CP_VOTER,
				true, pval->intval);
		break;
	default:
		dev_err(chip->dev, "write pre_regulator property %d is not supported\n",
				prop);
		return -EINVAL;
	}

	return rc;
}

static int pre_regulator_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		return 1;
	default:
		break;
	}

	return 0;
}

static const struct power_supply_desc pre_regulator_psy_desc = {
	.name = "pre_regulator",
	.type = POWER_SUPPLY_TYPE_MAINS,
	.properties = pre_regulator_props,
	.num_properties = ARRAY_SIZE(pre_regulator_props),
	.get_property = pre_regulator_get_prop,
	.set_property = pre_regulator_set_prop,
	.property_is_writeable = pre_regulator_is_writeable,
};

static int smb1398_create_pre_regulator_psy(struct smb1398_chip *chip)
{
	struct power_supply_config pre_regulator_psy_cfg = {};
	int rc = 0;

	pre_regulator_psy_cfg.drv_data = chip;
	pre_regulator_psy_cfg.of_node = chip->dev->of_node;

	chip->pre_regulator_psy = devm_power_supply_register(chip->dev,
			&pre_regulator_psy_desc,
			&pre_regulator_psy_cfg);
	if (IS_ERR(chip->pre_regulator_psy)) {
		rc = PTR_ERR(chip->pre_regulator_psy);
		dev_err(chip->dev, "register pre_regulator psy failed, rc=%d\n",
				rc);
		return rc;
	}

	return 0;
}

static int smb1398_pre_regulator_probe(struct smb1398_chip *chip)
{
	int rc = 0;

	rc = smb1398_create_pre_regulator_votables(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Create votable for pre_regulator failed, rc=%d\n",
				rc);
		return rc;
	}

	rc = smb1398_create_pre_regulator_psy(chip);
	if (rc < 0) {
		dev_err(chip->dev, "Create pre-regulator failed, rc=%d\n",
				rc);
		return rc;
	}

	return 0;
}

/* master  IIO configuration */
static const struct cp_iio_prop_channels cp_master_chans[] = {
	SMB1398_CHAN_INDEX("cp_master_cp_status_1", PSY_IIO_CP_STATUS1)
	SMB1398_CHAN_INDEX("cp_master_cp_status_2", PSY_IIO_CP_STATUS2)
	SMB1398_CHAN_INDEX("cp_master_cp_enable", PSY_IIO_CP_ENABLE)
	SMB1398_CHAN_INDEX("cp_master_cp_switcher_en", PSY_IIO_CP_SWITCHER_EN)
	SMB1398_CHAN_TEMP("cp_master_cp_die_temp", PSY_IIO_CP_DIE_TEMP)
	SMB1398_CHAN_CUR("cp_master_cp_isns", PSY_IIO_CP_ISNS)
	SMB1398_CHAN_CUR("cp_master_cp_isns_slave", PSY_IIO_CP_ISNS_SLAVE)
	SMB1398_CHAN_INDEX("cp_master_cp_toggle_switcher",
		PSY_IIO_CP_TOGGLE_SWITCHER)
	SMB1398_CHAN_INDEX("cp_master_cp_irq_status", PSY_IIO_IRQ_STATUS)
	SMB1398_CHAN_CUR("cp_master_cp_ilim", PSY_IIO_CP_ILIM)
	SMB1398_CHAN_INDEX("cp_master_chip_version", PSY_IIO_CHIP_VERSION)
	SMB1398_CHAN_INDEX("cp_master_parallel_mode", PSY_IIO_PARALLEL_MODE)
	SMB1398_CHAN_INDEX("cp_master_parallel_output_mode",
		PSY_IIO_PARALLEL_OUTPUT_MODE)
	SMB1398_CHAN_CUR("cp_master_min_icl", PSY_IIO_MIN_ICL)
};

static int cp_master_iio_set_prop(struct smb1398_chip *chip,
	int channel, int val)
{
	switch (channel) {
	case PSY_IIO_CP_ENABLE:
		vote(chip->div2_cp_disable_votable,
				USER_VOTER, !val, 0);
		break;
	case PSY_IIO_CP_TOGGLE_SWITCHER:
		if (!!val)
			smb1398_toggle_switcher(chip);
		break;
	case PSY_IIO_IRQ_STATUS:
		chip->div2_irq_status = val;
		break;
	case PSY_IIO_CP_ILIM:
		if (chip->div2_cp_ilim_votable)
			vote_override(chip->div2_cp_ilim_votable, CC_MODE_VOTER,
						(val > 0), val);
		break;
	default:
		pr_err("get prop %d is not supported\n", channel);
		return -EINVAL;
	}

	return 0;
}

static int cp_master_iio_get_prop_in_suspend(struct smb1398_chip *chip,
	int channel, int *val)
{
	switch (channel) {
	case PSY_IIO_CP_STATUS1:
		*val = chip->cp_status1;
		break;
	case PSY_IIO_CP_STATUS2:
		*val = chip->cp_status2;
		break;
	case PSY_IIO_CP_ENABLE:
		*val = chip->cp_enable;
		break;
	case PSY_IIO_CP_SWITCHER_EN:
		*val = chip->switcher_en;
		break;
	case PSY_IIO_CP_DIE_TEMP:
		*val = chip->die_temp;
		break;
	case PSY_IIO_CP_ISNS:
		*val = chip->cp_isns_master;
		break;
	case PSY_IIO_CP_ISNS_SLAVE:
		*val = chip->cp_isns_slave;
		break;
	case PSY_IIO_IRQ_STATUS:
		*val = chip->div2_irq_status;
		break;
	case PSY_IIO_CP_ILIM:
		*val = chip->cp_ilim;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cp_master_iio_get_prop(struct smb1398_chip *chip,
	int channel, int *val)
{
	int rc = 0, temp, isns_ua, ilim_ma;
	u8 status;

	/*
	 * Return the cached values when the system is in suspend state
	 * instead of reading the registers to avoid read failures.
	 */
	if (chip->in_suspend) {
		rc = cp_master_iio_get_prop_in_suspend(chip, channel, val);
		if (!rc)
			return IIO_VAL_INT;
		rc = 0;
	}

	switch (channel) {
	case PSY_IIO_CP_STATUS1:
		rc = smb1398_div2_cp_get_status1(chip, &status);
		if (!rc)
			chip->cp_status1 = *val = status;
		break;
	case PSY_IIO_CP_STATUS2:
		rc = smb1398_div2_cp_get_status2(chip, &status);
		if (!rc)
			chip->cp_status2 = *val = status;
		break;
	case PSY_IIO_CP_ENABLE:
		rc = smb1398_get_enable_status(chip);
		if (!rc)
			chip->cp_enable = *val = chip->smb_en &&
				!get_effective_result(
						chip->div2_cp_disable_votable);
		break;
	case PSY_IIO_CP_SWITCHER_EN:
		rc = smb1398_get_enable_status(chip);
		if (!rc)
			*val = chip->switcher_en;
		break;
	case PSY_IIO_CP_ISNS:
		rc = smb1398_div2_cp_get_master_isns(chip, &isns_ua);
		if (rc >= 0)
			chip->cp_isns_master = *val = isns_ua;
		break;
	case PSY_IIO_CP_ISNS_SLAVE:
		rc = smb1398_div2_cp_get_slave_isns(chip, &isns_ua);
		if (rc >= 0)
			chip->cp_isns_slave = *val = isns_ua;
		break;
	case PSY_IIO_CP_TOGGLE_SWITCHER:
		*val = 0;
		break;
	case PSY_IIO_CP_DIE_TEMP:
		rc = smb1398_get_die_temp(chip, &temp);
		if (rc >= 0) {
			*val = temp;
			if (temp <= THERMAL_SUSPEND_DECIDEGC)
				chip->die_temp = temp;
			else if (chip->die_temp == -ENODATA)
				rc = -ENODATA;
			else
				*val = chip->die_temp;
		}
		break;
	case PSY_IIO_IRQ_STATUS:
		*val = chip->div2_irq_status;
		rc = smb1398_div2_cp_get_irq_status(chip, &status);
		if (!rc)
			*val |= status;
		break;
	case PSY_IIO_CP_ILIM:
		if (is_cps_available(chip)) {
			if (chip->div2_cp_ilim_votable)
				*val = get_effective_result(
						chip->div2_cp_ilim_votable);
		} else {
			rc = smb1398_get_iin_ma(chip, &ilim_ma);
			if (!rc)
				*val = (ilim_ma * 1000 * 100)
							/ DIV2_ILIM_CFG_PCT;
		}
		chip->cp_ilim = *val;
		break;
	case PSY_IIO_CHIP_VERSION:
		*val = chip->rev4;
		break;
	case PSY_IIO_PARALLEL_MODE:
		*val = chip->pl_input_mode;
		break;
	case PSY_IIO_PARALLEL_OUTPUT_MODE:
		*val = chip->pl_output_mode;
		break;
	case PSY_IIO_MIN_ICL:
		*val = smb1398_div2_cp_get_min_icl(chip);
		break;
	default:
		pr_err("get prop %d is not supported\n", channel);
		rc = -EINVAL;
		break;
	}

	if (rc < 0 && rc != -ENODATA) {
		pr_err("Couldn't get prop %d rc = %d\n", channel, rc);
		return rc;
	}

	return IIO_VAL_INT;
}

static int cp_master_write_raw(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan, int val, int val2,
			 long mask)
{
	struct smb1398_chip *iio_chip = iio_priv(indio_dev);
	int channel;

	channel = chan->channel;

	return cp_master_iio_set_prop(iio_chip, channel, val);
}

static int cp_master_read_raw(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan, int *val, int *val2,
			 long mask)
{
	struct smb1398_chip *iio_chip = iio_priv(indio_dev);
	int channel;

	channel = chan->channel;

	return cp_master_iio_get_prop(iio_chip, channel, val);
}

/* slave  IIO configuration */
static const struct cp_iio_prop_channels cp_slave_chans[] = {
	SMB1398_CHAN_INDEX("cp_slave_cp_enable", PSY_IIO_CP_ENABLE)
	SMB1398_CHAN_CUR("cp_slave_input_current_max",
		PSY_IIO_CP_INPUT_CURRENT_MAX)
	SMB1398_CHAN_CUR("cp_slave_current_capability",
		PSY_IIO_CURRENT_CAPABILITY)
};

static int cp_slave_iio_set_prop(struct smb1398_chip *chip,
	int channel, int val)
{
	int ilim_ma, rc = 0;
	enum isns_mode mode;

	switch (channel) {
	case PSY_IIO_CP_ENABLE:
		rc = smb1398_div2_cp_switcher_en(chip, !!val);
		break;
	case PSY_IIO_CP_INPUT_CURRENT_MAX:
		ilim_ma = val / 1000;
		rc = smb1398_set_iin_ma(chip, ilim_ma);
		break;
	case PSY_IIO_CURRENT_CAPABILITY:
		mode = (enum isns_mode)val;
		rc = smb1398_div2_cp_isns_mode_control(chip, mode);
		if (rc < 0)
			return rc;
		chip->current_capability = mode;
		break;
	default:
		pr_err("get prop %d is not supported\n", channel);
		rc = -EINVAL;
		break;
	}

	if (rc < 0) {
		pr_err("Couldn't set prop %d rc = %d\n", channel, rc);
		return rc;
	}

	return 0;
}

static int cp_slave_iio_get_prop(struct smb1398_chip *chip,
	int channel, int *val)
{
	switch (channel) {
	case PSY_IIO_CP_ENABLE:
		*val = chip->switcher_en;
		break;
	case PSY_IIO_CP_INPUT_CURRENT_MAX:
		*val = 0;
		if (!chip->div2_cp_ilim_votable)
			chip->div2_cp_ilim_votable = find_votable("CP_ILIM");
		if (chip->div2_cp_ilim_votable)
			*val = get_effective_result_locked(
						chip->div2_cp_ilim_votable);
		break;
	case PSY_IIO_CURRENT_CAPABILITY:
		*val = (int)chip->current_capability;
		break;
	default:
		pr_err("get prop %d is not supported\n", channel);
		return -EINVAL;
	}

	return IIO_VAL_INT;
}

static int cp_slave_write_raw(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan, int val, int val2,
			 long mask)
{
	struct smb1398_chip *iio_chip = iio_priv(indio_dev);
	int channel;

	channel = chan->channel;

	return cp_slave_iio_set_prop(iio_chip, channel, val);
}

static int cp_slave_read_raw(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan, int *val, int *val2,
			 long mask)
{
	struct smb1398_chip *iio_chip = iio_priv(indio_dev);
	int channel;

	channel = chan->channel;

	return cp_slave_iio_get_prop(iio_chip, channel, val);
}

static int cp_fwnode_xlate(struct iio_dev *indio_dev,
				const struct fwnode_reference_args *iiospec)
{
	struct smb1398_chip *iio_chip = iio_priv(indio_dev);
	int i;
	struct iio_chan_spec *iio_chan = iio_chip->cp_iio_chan_ids;

	for (i = 0; i < iio_chip->nchannels; i++) {
		if (iio_chan->channel == iiospec->args[0])
			return i;
		iio_chan++;
	}

	return -EINVAL;
}

static const struct iio_info cp_master_iio_info = {
	.read_raw = cp_master_read_raw,
	.write_raw = cp_master_write_raw,
	.fwnode_xlate = cp_fwnode_xlate,
};

static const struct iio_info cp_slave_iio_info = {
	.read_raw = cp_slave_read_raw,
	.write_raw = cp_slave_write_raw,
	.fwnode_xlate = cp_fwnode_xlate,
};

static int cp_smb5_iio_init(struct smb1398_chip *chip)
{
	int rc = 0;
	struct iio_channel **iio_list;

	if (IS_ERR(chip->smb5_iio_chan_list))
		return -EINVAL;

	iio_list = get_ext_channels(chip->dev,
		cp_smb5_ext_iio_chan, ARRAY_SIZE(cp_smb5_ext_iio_chan));
	if (IS_ERR(iio_list)) {
		rc = PTR_ERR(iio_list);
		if (rc != -EPROBE_DEFER) {
			dev_err(chip->dev, "Failed to get channels, rc=%d\n",
					rc);
			chip->smb5_iio_chan_list = ERR_PTR(-EINVAL);
		}
		return rc;
	}

	chip->smb5_iio_chan_list = iio_list;
	return 0;
}

static int cp_iio_probe_init(struct smb1398_chip *chip,
	struct iio_dev *indio_dev, const struct cp_iio_prop_channels *cp_chans,
	const struct iio_info *cp_iio_info)
{
	int i;
	struct iio_chan_spec *iio_chan;

	chip->cp_iio_chan_ids = devm_kcalloc(chip->dev, chip->nchannels,
		sizeof(*chip->cp_iio_chan_ids), GFP_KERNEL);
	if (!chip->cp_iio_chan_ids)
		return -ENOMEM;

	for (i = 0; i < chip->nchannels; i++) {
		iio_chan = &chip->cp_iio_chan_ids[i];

		iio_chan->channel = cp_chans[i].channel_no;
		iio_chan->datasheet_name =
			cp_chans[i].datasheet_name;
		iio_chan->extend_name = cp_chans[i].datasheet_name;
		iio_chan->info_mask_separate =
			cp_chans[i].info_mask;
		iio_chan->type = cp_chans[i].type;
		iio_chan->address = i;
	}

	if (chip->div2_cp_role == DIV2_CP_MASTER ||
			chip->div2_cp_role == SMB1394_DIV2_CP_PRY) {
		cp_smb5_iio_init(chip);
		indio_dev->name = "smb1396-div2-cp-master";
	} else {
		indio_dev->name = "smb1396-div2-cp-slave";
	}

	indio_dev->info = cp_iio_info;

	return 0;
}

static int smb1398_probe(struct platform_device *pdev)
{
	struct smb1398_chip *chip;
	struct iio_dev *indio_dev;
	int rc = 0;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;

	chip = iio_priv(indio_dev);

	chip->die_temp = -ENODATA;
	chip->dev = &pdev->dev;
	chip->regmap = dev_get_regmap(chip->dev->parent, NULL);
	if (!chip->regmap) {
		dev_err(chip->dev, "Get regmap failed\n");
		return -EINVAL;
	}
	chip->disabled = true;
	platform_set_drvdata(pdev, chip);

	chip->div2_cp_role =  (u32)(unsigned long)of_device_get_match_data(chip->dev);
	switch (chip->div2_cp_role) {
	case DIV2_CP_MASTER:
	case SMB1394_DIV2_CP_PRY:
		chip->nchannels = ARRAY_SIZE(cp_master_chans);
		rc = smb1398_div2_cp_master_probe(chip);
		if (rc < 0) {
			if (rc != -EPROBE_DEFER)
				dev_err(chip->dev, "Couldn't probe SMB1398 master rc= %d\n",
					rc);
			goto cleanup;
		}
		rc = cp_iio_probe_init(chip, indio_dev, cp_master_chans, &cp_master_iio_info);
		break;
	case DIV2_CP_SLAVE:
	case SMB1394_DIV2_CP_SECY:
		chip->nchannels = ARRAY_SIZE(cp_slave_chans);
		rc = smb1398_div2_cp_slave_probe(chip);
		if (rc < 0) {
			if (rc != -EPROBE_DEFER)
				dev_err(chip->dev, "Couldn't probe SMB1398 slave rc= %d\n",
					rc);
			goto cleanup;
		}
		rc = cp_iio_probe_init(chip, indio_dev, cp_slave_chans, &cp_slave_iio_info);
		break;
	case COMBO_PRE_REGULATOR:
		rc = smb1398_pre_regulator_probe(chip);
		break;
	default:
		dev_err(chip->dev, "Couldn't find a match role for %d\n",
				chip->div2_cp_role);
		goto cleanup;
	}

	if (rc < 0) {
		if (rc != -EPROBE_DEFER)
			dev_err(chip->dev, "IIO init failed for %s rc= %d\n",
				!!chip->div2_cp_role ? "slave" : "master", rc);
		goto cleanup;
	}

	/*
	 * This configuration below is applicable to both
	 * master and slave. The individual channel
	 * configurations are done in master/slave
	 * iio_probe_init calls.
	 */

	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = chip->cp_iio_chan_ids;
	indio_dev->num_channels = chip->nchannels;

	rc = devm_iio_device_register(&pdev->dev, indio_dev);
	if (rc) {
		pr_err("iio device register failed rc=%d\n", rc);
		goto cleanup;
	}

	return 0;

cleanup:
	platform_set_drvdata(pdev, NULL);
	return rc;
}

static int smb1398_remove(struct platform_device *pdev)
{
	struct smb1398_chip *chip = platform_get_drvdata(pdev);

	if (chip->div2_cp_role == DIV2_CP_MASTER ||
			chip->div2_cp_role == SMB1394_DIV2_CP_PRY) {
		vote(chip->awake_votable, SHUTDOWN_VOTER, false, 0);
		vote(chip->div2_cp_disable_votable, SHUTDOWN_VOTER, true, 0);
		vote(chip->div2_cp_ilim_votable, SHUTDOWN_VOTER, true, 0);
		cancel_work_sync(&chip->taper_work);
		cancel_work_sync(&chip->status_change_work);
		mutex_destroy(&chip->die_chan_lock);
		smb1398_destroy_votables(chip);
	}

	return 0;
}

static int smb1398_suspend(struct device *dev)
{
	struct smb1398_chip *chip = dev_get_drvdata(dev);

	chip->in_suspend = true;
	return 0;
}

static int smb1398_resume(struct device *dev)
{
	struct smb1398_chip *chip = dev_get_drvdata(dev);

	chip->in_suspend = false;

	if (chip->div2_cp_role == DIV2_CP_MASTER) {
		rerun_election(chip->div2_cp_ilim_votable);
		rerun_election(chip->div2_cp_disable_votable);
	}

	return 0;
}

static void smb1398_shutdown(struct platform_device *pdev)
{
	struct smb1398_chip *chip = platform_get_drvdata(pdev);
	int rc;

	power_supply_unreg_notifier(&chip->nb);

	/* Disable SMB1398 */
	rc = smb1398_div2_cp_switcher_en(chip, 0);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't disable chip rc= %d\n", rc);

	rc = smb1398_toggle_uvlo(chip);
	if (rc < 0)
		dev_err(chip->dev, "Couldn't toggle uvlo rc= %d\n", rc);
}

static const struct dev_pm_ops smb1398_pm_ops = {
	.suspend	= smb1398_suspend,
	.resume		= smb1398_resume,
};

static const struct of_device_id match_table[] = {
	{ .compatible = "qcom,smb1396-div2-cp-master",
	  .data = (void *)DIV2_CP_MASTER,
	},
	{ .compatible = "qcom,smb1396-div2-cp-slave",
	  .data = (void *)DIV2_CP_SLAVE,
	},
	{ .compatible = "qcom,smb1398-pre-regulator",
	  .data = (void *)COMBO_PRE_REGULATOR,
	},
	{ .compatible = "qcom,smb1394-div2-cp-primary",
	  .data = (void *)SMB1394_DIV2_CP_PRY,
	},
	{ .compatible = "qcom,smb1394-div2-cp-secondary",
	  .data = (void *)SMB1394_DIV2_CP_SECY,
	},
	{
	},
};

static struct platform_driver smb1398_driver = {
	.driver	= {
		.name		= "qcom,smb1398-charger",
		.pm		= &smb1398_pm_ops,
		.of_match_table	= match_table,
	},
	.probe	= smb1398_probe,
	.remove	= smb1398_remove,
	.shutdown = smb1398_shutdown,
};
module_platform_driver(smb1398_driver);

MODULE_DESCRIPTION("SMB1398 charger driver");
MODULE_LICENSE("GPL");
