// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spmi.h>
#include <linux/suspend.h>
#include <linux/input/qpnp-power-on.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#define PMIC_VER_8941				0x01
#define PMIC_VERSION_REG			0x0105
#define PMIC_VERSION_REV4_REG			0x0103

#define PMIC8941_V1_REV4			0x01
#define PMIC8941_V2_REV4			0x02
#define PON_PRIMARY				0x01
#define PON_SECONDARY				0x02
#define PON_1REG				0x03
#define PON_GEN2_PRIMARY			0x04
#define PON_GEN2_SECONDARY			0x05
#define PON_GEN3_PBS				0x08
#define PON_GEN3_HLOS				0x09

enum qpnp_pon_version {
	QPNP_PON_GEN1_V1,
	QPNP_PON_GEN1_V2,
	QPNP_PON_GEN2,
	QPNP_PON_GEN3,
};

#define PON_OFFSET(subtype, offset_gen1, offset_gen2) \
	(((subtype == PON_PRIMARY) || \
	(subtype == PON_SECONDARY) || \
	(subtype == PON_1REG)) ? offset_gen1 : offset_gen2)

/* Common PNP register addresses */
#define QPNP_PON_REVISION2(pon)			((pon)->base + 0x01)
#define QPNP_PON_PERPH_SUBTYPE(pon)		((pon)->base + 0x05)

/* PON common register addresses */
#define QPNP_PON_RT_STS(pon)			((pon)->base + 0x10)
#define QPNP_PON_PULL_CTL(pon)			((pon)->base + 0x70)
#define QPNP_PON_DBC_CTL(pon)			((pon)->base + 0x71)
#define QPNP_PON_PBS_DBC_CTL(pon)		((pon)->pbs_base + 0x71)

/* PON/RESET sources register addresses */
#define QPNP_PON_REASON1(pon) \
	((pon)->base + PON_OFFSET((pon)->subtype, 0x8, 0xC0))
#define QPNP_PON_WARM_RESET_REASON1(pon) \
	((pon)->base + PON_OFFSET((pon)->subtype, 0xA, 0xC2))
#define QPNP_POFF_REASON1(pon) \
	((pon)->base + PON_OFFSET((pon)->subtype, 0xC, 0xC5))
#define QPNP_PON_WARM_RESET_REASON2(pon)	((pon)->base + 0xB)
#define QPNP_PON_OFF_REASON(pon)		((pon)->base + 0xC7)
#define QPNP_FAULT_REASON1(pon)			((pon)->base + 0xC8)
#define QPNP_S3_RESET_REASON(pon)		((pon)->base + 0xCA)
#define QPNP_PON_KPDPWR_S1_TIMER(pon)		((pon)->base + 0x40)
#define QPNP_PON_KPDPWR_S2_TIMER(pon)		((pon)->base + 0x41)
#define QPNP_PON_KPDPWR_S2_CNTL(pon)		((pon)->base + 0x42)
#define QPNP_PON_KPDPWR_S2_CNTL2(pon)		((pon)->base + 0x43)
#define QPNP_PON_RESIN_S1_TIMER(pon)		((pon)->base + 0x44)
#define QPNP_PON_RESIN_S2_TIMER(pon)		((pon)->base + 0x45)
#define QPNP_PON_RESIN_S2_CNTL(pon)		((pon)->base + 0x46)
#define QPNP_PON_RESIN_S2_CNTL2(pon)		((pon)->base + 0x47)
#define QPNP_PON_KPDPWR_RESIN_S1_TIMER(pon)	((pon)->base + 0x48)
#define QPNP_PON_KPDPWR_RESIN_S2_TIMER(pon)	((pon)->base + 0x49)
#define QPNP_PON_KPDPWR_RESIN_S2_CNTL(pon)	((pon)->base + 0x4A)
#define QPNP_PON_KPDPWR_RESIN_S2_CNTL2(pon)	((pon)->base + 0x4B)
#define QPNP_PON_PS_HOLD_RST_CTL(pon) \
	((pon)->base + ((pon)->pon_ver != QPNP_PON_GEN3 ? 0x5A : 0x52))
#define QPNP_PON_PS_HOLD_RST_CTL2(pon) \
	((pon)->base + ((pon)->pon_ver != QPNP_PON_GEN3 ? 0x5B : 0x53))
#define QPNP_PON_WD_RST_S2_CTL(pon)		((pon)->base + 0x56)
#define QPNP_PON_WD_RST_S2_CTL2(pon)		((pon)->base + 0x57)
#define QPNP_PON_SW_RST_S2_CTL(pon)		((pon)->base + 0x62)
#define QPNP_PON_SW_RST_S2_CTL2(pon)		((pon)->base + 0x63)
#define QPNP_PON_SW_RST_GO(pon)			((pon)->base + 0x64)
#define QPNP_PON_S3_SRC(pon)			((pon)->base + 0x74)
#define QPNP_PON_S3_DBC_CTL(pon)		((pon)->base + 0x75)
#define QPNP_PON_SMPL_CTL(pon)			((pon)->base + 0x7F)
#define QPNP_PON_TRIGGER_EN(pon)		((pon)->base + 0x80)
#define QPNP_PON_XVDD_RB_SPARE(pon)		((pon)->base + 0x8E)
#define QPNP_PON_SOFT_RB_SPARE(pon)		((pon)->base + 0x8F)
#define QPNP_PON_SEC_ACCESS(pon)		((pon)->base + 0xD0)

#define QPNP_PON_SEC_UNLOCK			0xA5
#define QPNP_PON_SW_RST_GO_VAL			0xA5

#define QPNP_PON_WARM_RESET_TFT			BIT(4)

#define QPNP_PON_RESIN_PULL_UP			BIT(0)
#define QPNP_PON_KPDPWR_PULL_UP			BIT(1)
#define QPNP_PON_CBLPWR_PULL_UP			BIT(2)
#define QPNP_PON_FAULT_PULL_UP			BIT(4)
#define QPNP_PON_S2_CNTL_EN			BIT(7)
#define QPNP_PON_S2_RESET_ENABLE		BIT(7)
#define QPNP_PON_DELAY_BIT_SHIFT		6
#define QPNP_PON_GEN2_DELAY_BIT_SHIFT		14

#define QPNP_PON_S1_TIMER_MASK			(0xF)
#define QPNP_PON_S2_TIMER_MASK			(0x7)
#define QPNP_PON_S2_CNTL_TYPE_MASK		(0xF)

#define QPNP_PON_DBC_DELAY_MASK(pon)	PON_OFFSET((pon)->subtype, 0x7, 0xF)

#define QPNP_PON_KPDPWR_N_SET			BIT(0)
#define QPNP_PON_RESIN_N_SET			BIT(1)
#define QPNP_PON_CBLPWR_N_SET			BIT(2)
#define QPNP_PON_RESIN_BARK_N_SET		BIT(4)
#define QPNP_PON_KPDPWR_RESIN_BARK_N_SET	BIT(5)
#define QPNP_PON_GEN3_RESIN_N_SET		BIT(6)
#define QPNP_PON_GEN3_KPDPWR_N_SET		BIT(7)

#define QPNP_PON_WD_EN				BIT(7)
#define QPNP_PON_RESET_EN			BIT(7)
#define QPNP_PON_POWER_OFF_MASK			0xF
#define QPNP_GEN2_POFF_SEQ			BIT(7)
#define QPNP_GEN2_FAULT_SEQ			BIT(6)
#define QPNP_GEN2_S3_RESET_SEQ			BIT(5)

#define QPNP_PON_S3_SRC_KPDPWR			0
#define QPNP_PON_S3_SRC_RESIN			1
#define QPNP_PON_S3_SRC_KPDPWR_AND_RESIN	2
#define QPNP_PON_S3_SRC_KPDPWR_OR_RESIN		3
#define QPNP_PON_S3_SRC_MASK			0x3
#define QPNP_PON_HARD_RESET_MASK		GENMASK(7, 5)

#define QPNP_PON_UVLO_DLOAD_EN			BIT(7)
#define QPNP_PON_SMPL_EN			BIT(7)

/* Limits */
#define QPNP_PON_S1_TIMER_MAX			10256
#define QPNP_PON_S2_TIMER_MAX			2000
#define QPNP_PON_S3_TIMER_SECS_MAX		128
#define QPNP_PON_S3_DBC_DELAY_MASK		0x07
#define QPNP_PON_RESET_TYPE_MAX			0xF
#define PON_S1_COUNT_MAX			0xF
#define QPNP_PON_MIN_DBC_US			(USEC_PER_SEC / 64)
#define QPNP_PON_MAX_DBC_US			(USEC_PER_SEC * 2)
#define QPNP_PON_GEN2_MIN_DBC_US		62
#define QPNP_PON_GEN2_MAX_DBC_US		(USEC_PER_SEC / 4)

#define QPNP_KEY_STATUS_DELAY			msecs_to_jiffies(250)

#define QPNP_PON_BUFFER_SIZE			9

#define QPNP_POFF_REASON_UVLO			13

enum pon_type {
	PON_KPDPWR	 = PON_POWER_ON_TYPE_KPDPWR,
	PON_RESIN	 = PON_POWER_ON_TYPE_RESIN,
	PON_CBLPWR	 = PON_POWER_ON_TYPE_CBLPWR,
	PON_KPDPWR_RESIN = PON_POWER_ON_TYPE_KPDPWR_RESIN,
};

struct pon_reg {
	unsigned int val;
	u16 addr;
	struct list_head list;
};

struct qpnp_pon_config {
	u32			pon_type;
	u32			support_reset;
	u32			key_code;
	u32			s1_timer;
	u32			s2_timer;
	u32			s2_type;
	bool			pull_up;
	int			state_irq;
	int			bark_irq;
	u16			s2_cntl_addr;
	u16			s2_cntl2_addr;
	bool			old_state;
	bool			use_bark;
	bool			config_reset;
};

struct pon_regulator {
	struct qpnp_pon		*pon;
	struct regulator_dev	*rdev;
	struct regulator_desc	rdesc;
	u32			addr;
	u32			bit;
	bool			enabled;
};

struct qpnp_pon {
	struct device		*dev;
	struct regmap		*regmap;
	struct input_dev	*pon_input;
	struct qpnp_pon_config	*pon_cfg;
	struct pon_regulator	*pon_reg_cfg;
	struct list_head	restore_regs;
	struct list_head	list;
	struct mutex		restore_lock;
	struct delayed_work	bark_work;
	struct dentry		*debugfs;
	u16			base;
	u16			pbs_base;
	u8			subtype;
	u8			pon_ver;
	u8			warm_reset_reason1;
	u8			warm_reset_reason2;
	int			num_pon_config;
	int			num_pon_reg;
	int			pon_trigger_reason;
	int			pon_power_off_reason;
	u32			dbc_time_us;
	u32			uvlo;
	int			warm_reset_poff_type;
	int			hard_reset_poff_type;
	int			shutdown_poff_type;
	int			resin_warm_reset_type;
	int			resin_hard_reset_type;
	int			resin_shutdown_type;
	bool			is_spon;
	bool			store_hard_reset_reason;
	bool			resin_hard_reset_disable;
	bool			resin_shutdown_disable;
	bool			ps_hold_hard_reset_disable;
	bool			ps_hold_shutdown_disable;
	bool			kpdpwr_dbc_enable;
	bool			resin_pon_reset;
	ktime_t			kpdpwr_last_release_time;
	bool			legacy_hard_reset_offset;
};

static struct qpnp_pon *sys_reset_dev;
static struct qpnp_pon *modem_reset_dev;
static DEFINE_SPINLOCK(spon_list_slock);
static LIST_HEAD(spon_dev_list);

static u32 s1_delay[PON_S1_COUNT_MAX + 1] = {
	0, 32, 56, 80, 138, 184, 272, 408, 608, 904, 1352, 2048, 3072, 4480,
	6720, 10256
};

static const char * const qpnp_pon_reason[] = {
	[0] = "Triggered from Hard Reset",
	[1] = "Triggered from SMPL (Sudden Momentary Power Loss)",
	[2] = "Triggered from RTC (RTC Alarm Expiry)",
	[3] = "Triggered from DC (DC Charger Insertion)",
	[4] = "Triggered from USB (USB Charger Insertion)",
	[5] = "Triggered from PON1 (Secondary PMIC)",
	[6] = "Triggered from CBL (External Power Supply)",
	[7] = "Triggered from KPD (Power Key Press)",
};

#define POFF_REASON_FAULT_OFFSET	16
#define POFF_REASON_S3_RESET_OFFSET	32
static const char * const qpnp_poff_reason[] = {
	/* QPNP_PON_GEN1 POFF reasons */
	[0] = "Triggered from SOFT (Software)",
	[1] = "Triggered from PS_HOLD (PS_HOLD/MSM Controlled Shutdown)",
	[2] = "Triggered from PMIC_WD (PMIC Watchdog)",
	[3] = "Triggered from GP1 (Keypad_Reset1)",
	[4] = "Triggered from GP2 (Keypad_Reset2)",
	[5] = "Triggered from KPDPWR_AND_RESIN (Power Key and Reset Line)",
	[6] = "Triggered from RESIN_N (Reset Line/Volume Down Key)",
	[7] = "Triggered from KPDPWR_N (Long Power Key Hold)",
	[8] = "N/A",
	[9] = "N/A",
	[10] = "N/A",
	[11] = "Triggered from CHARGER (Charger ENUM_TIMER, BOOT_DONE)",
	[12] = "Triggered from TFT (Thermal Fault Tolerance)",
	[13] = "Triggered from UVLO (Under Voltage Lock Out)",
	[14] = "Triggered from OTST3 (Over Temperature)",
	[15] = "Triggered from STAGE3 (Stage 3 Reset)",

	/* QPNP_PON_GEN2 FAULT reasons */
	[16] = "Triggered from GP_FAULT0",
	[17] = "Triggered from GP_FAULT1",
	[18] = "Triggered from GP_FAULT2",
	[19] = "Triggered from GP_FAULT3",
	[20] = "Triggered from MBG_FAULT",
	[21] = "Triggered from OVLO (Over Voltage Lock Out)",
	[22] = "Triggered from UVLO (Under Voltage Lock Out)",
	[23] = "Triggered from AVDD_RB",
	[24] = "N/A",
	[25] = "N/A",
	[26] = "N/A",
	[27] = "Triggered from FAULT_FAULT_N",
	[28] = "Triggered from FAULT_PBS_WATCHDOG_TO",
	[29] = "Triggered from FAULT_PBS_NACK",
	[30] = "Triggered from FAULT_RESTART_PON",
	[31] = "Triggered from OTST3 (Over Temperature)",

	/* QPNP_PON_GEN2 S3_RESET reasons */
	[32] = "N/A",
	[33] = "N/A",
	[34] = "N/A",
	[35] = "N/A",
	[36] = "Triggered from S3_RESET_FAULT_N",
	[37] = "Triggered from S3_RESET_PBS_WATCHDOG_TO",
	[38] = "Triggered from S3_RESET_PBS_NACK",
	[39] = "Triggered from S3_RESET_KPDPWR_ANDOR_RESIN",
};

static int qpnp_pon_store_reg(struct qpnp_pon *pon, u16 addr)
{
	int rc;
	unsigned int val;
	struct pon_reg *reg, *pos = NULL;

	mutex_lock(&pon->restore_lock);
	rc = regmap_read(pon->regmap, addr, &val);
	if (rc < 0) {
		dev_info(pon->dev, "Register read failed, addr=0x%04X, rc=%d\n",
			addr, rc);
	} else {
		list_for_each_entry(pos, &pon->restore_regs, list) {
			if (pos->addr == addr) {
				pos->val = val;
				goto done;
			}
		}

		reg = devm_kzalloc(pon->dev, sizeof(*reg), GFP_KERNEL);
		if (!reg) {
			rc = -ENOMEM;
			goto done;
		}

		reg->addr = addr;
		reg->val = val;
		INIT_LIST_HEAD(&reg->list);
		list_add_tail(&reg->list, &pon->restore_regs);
	}

done:
	mutex_unlock(&pon->restore_lock);
	return rc;
}

static int
qpnp_pon_masked_write(struct qpnp_pon *pon, u16 addr, u8 mask, u8 val)
{
	int rc;

	rc = regmap_update_bits(pon->regmap, addr, mask, val);
	if (rc)
		dev_err(pon->dev, "Register write failed, addr=0x%04X, rc=%d\n",
			addr, rc);
	return rc;
}

static int qpnp_pon_write(struct qpnp_pon *pon, u16 addr, u8 val)
{
	int rc;

	rc = regmap_write(pon->regmap, addr, val);
	if (rc)
		dev_err(pon->dev, "Register write failed, addr=0x%04X, rc=%d\n",
			addr, rc);
	return rc;
}

static int
qpnp_pon_masked_write_backup(struct qpnp_pon *pon, u16 addr, u8 mask, u8 val)
{
	int rc;

	rc = qpnp_pon_masked_write(pon, addr, mask, val);
	if (rc < 0)
		return rc;

	return qpnp_pon_store_reg(pon, addr);
}

static int qpnp_pon_read(struct qpnp_pon *pon, u16 addr, unsigned int *val)
{
	int rc;

	rc = regmap_read(pon->regmap, addr, val);
	if (rc)
		dev_err(pon->dev, "Register read failed, addr=0x%04X, rc=%d\n",
			addr, rc);
	return rc;
}

static bool is_pon_gen1(struct qpnp_pon *pon)
{
	return pon->subtype == PON_PRIMARY || pon->subtype == PON_SECONDARY;
}

static bool is_pon_gen2(struct qpnp_pon *pon)
{
	return pon->subtype == PON_GEN2_PRIMARY ||
		pon->subtype == PON_GEN2_SECONDARY;
}

static bool is_pon_gen3(struct qpnp_pon *pon)
{
	return pon->subtype == PON_GEN3_PBS ||
		pon->subtype == PON_GEN3_HLOS;
}

/**
 * qpnp_pon_set_restart_reason() - Store device restart reason in PMIC register
 *
 * Returns 0 if PMIC feature is not available or store restart reason
 * successfully.
 * Returns < 0 for errors
 *
 * This function is used to store device restart reason in PMIC register.
 * It checks here to see if the restart reason register has been specified.
 * If it hasn't, this function should immediately return 0
 */
int qpnp_pon_set_restart_reason(enum pon_restart_reason reason)
{
	int rc = 0;
	struct qpnp_pon *pon = sys_reset_dev;

	if (!pon)
		return 0;

	if (!pon->store_hard_reset_reason)
		return 0;

	if ((is_pon_gen2(pon) || is_pon_gen3(pon)) && !pon->legacy_hard_reset_offset)
		rc = qpnp_pon_masked_write(pon, QPNP_PON_SOFT_RB_SPARE(pon),
					   GENMASK(7, 1), (reason << 1));
	else
		rc = qpnp_pon_masked_write(pon, QPNP_PON_SOFT_RB_SPARE(pon),
					   GENMASK(7, 2), (reason << 2));

	return rc;
}
EXPORT_SYMBOL(qpnp_pon_set_restart_reason);

/*
 * qpnp_pon_check_hard_reset_stored() - Checks if the PMIC need to
 * store hard reset reason.
 *
 * Returns true if reset reason can be stored, false if it cannot be stored
 *
 */
bool qpnp_pon_check_hard_reset_stored(void)
{
	struct qpnp_pon *pon = sys_reset_dev;

	if (!pon)
		return false;

	return pon->store_hard_reset_reason;
}
EXPORT_SYMBOL(qpnp_pon_check_hard_reset_stored);

static int qpnp_pon_set_dbc(struct qpnp_pon *pon, u32 delay)
{
	int rc = 0;
	u32 val;

	if (delay == pon->dbc_time_us)
		return 0;

	if (pon->pon_input)
		mutex_lock(&pon->pon_input->mutex);

	if (is_pon_gen2(pon)) {
		if (delay < QPNP_PON_GEN2_MIN_DBC_US)
			delay = QPNP_PON_GEN2_MIN_DBC_US;
		else if (delay > QPNP_PON_GEN2_MAX_DBC_US)
			delay = QPNP_PON_GEN2_MAX_DBC_US;
		val = (delay << QPNP_PON_GEN2_DELAY_BIT_SHIFT) / USEC_PER_SEC;
	} else {
		if (delay < QPNP_PON_MIN_DBC_US)
			delay = QPNP_PON_MIN_DBC_US;
		else if (delay > QPNP_PON_MAX_DBC_US)
			delay = QPNP_PON_MAX_DBC_US;
		val = (delay << QPNP_PON_DELAY_BIT_SHIFT) / USEC_PER_SEC;
	}

	val = ilog2(val);
	rc = qpnp_pon_masked_write_backup(pon, QPNP_PON_DBC_CTL(pon),
				   QPNP_PON_DBC_DELAY_MASK(pon), val);
	if (!rc)
		pon->dbc_time_us = delay;

	if (pon->pon_input)
		mutex_unlock(&pon->pon_input->mutex);

	return rc;
}

static int qpnp_pon_get_dbc(struct qpnp_pon *pon, u32 *delay)
{
	int rc;
	unsigned int val;

	if (is_pon_gen3(pon) && pon->pbs_base)
		rc = qpnp_pon_read(pon, QPNP_PON_PBS_DBC_CTL(pon), &val);
	else
		rc = qpnp_pon_read(pon, QPNP_PON_DBC_CTL(pon), &val);

	if (rc)
		return rc;

	val &= QPNP_PON_DBC_DELAY_MASK(pon);

	if (is_pon_gen2(pon) || is_pon_gen3(pon))
		*delay = USEC_PER_SEC /
			(1 << (QPNP_PON_GEN2_DELAY_BIT_SHIFT - val));
	else
		*delay = USEC_PER_SEC /
			(1 << (QPNP_PON_DELAY_BIT_SHIFT - val));

	return rc;
}

static ssize_t debounce_us_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct qpnp_pon *pon = dev_get_drvdata(dev);

	return scnprintf(buf, QPNP_PON_BUFFER_SIZE, "%d\n", pon->dbc_time_us);
}

static ssize_t debounce_us_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct qpnp_pon *pon = dev_get_drvdata(dev);
	u32 value;
	int rc;

	if (size > QPNP_PON_BUFFER_SIZE)
		return -EINVAL;

	rc = kstrtou32(buf, 10, &value);
	if (rc)
		return rc;

	rc = qpnp_pon_set_dbc(pon, value);
	if (rc < 0)
		return rc;

	return size;
}
static DEVICE_ATTR_RW(debounce_us);

static int qpnp_pon_reset_config(struct qpnp_pon *pon,
				 enum pon_power_off_type type)
{
	int rc;
	bool disable = false;
	u16 rst_en_reg;

	if (pon->pon_ver == QPNP_PON_GEN1_V1)
		rst_en_reg = QPNP_PON_PS_HOLD_RST_CTL(pon);
	else
		rst_en_reg = QPNP_PON_PS_HOLD_RST_CTL2(pon);

	/*
	 * Based on the power-off type set for a PON device through device tree
	 * change the type being configured into PS_HOLD_RST_CTL.
	 */
	switch (type) {
	case PON_POWER_OFF_WARM_RESET:
		if (pon->warm_reset_poff_type != -EINVAL)
			type = pon->warm_reset_poff_type;
		break;
	case PON_POWER_OFF_HARD_RESET:
		if (pon->hard_reset_poff_type != -EINVAL)
			type = pon->hard_reset_poff_type;
		disable = pon->ps_hold_hard_reset_disable;
		break;
	case PON_POWER_OFF_SHUTDOWN:
		if (pon->shutdown_poff_type != -EINVAL)
			type = pon->shutdown_poff_type;
		disable = pon->ps_hold_shutdown_disable;
		break;
	default:
		break;
	}

	rc = qpnp_pon_masked_write(pon, rst_en_reg, QPNP_PON_RESET_EN, 0);
	if (rc)
		return rc;

	/*
	 * Check if ps-hold power off configuration needs to be disabled.
	 * If yes, then return without configuring.
	 */
	if (disable)
		return 0;

	/*
	 * We need 10 sleep clock cycles here. But since the clock is
	 * internally generated, we need to add 50% tolerance to be
	 * conservative.
	 */
	udelay(500);

	rc = qpnp_pon_masked_write(pon, QPNP_PON_PS_HOLD_RST_CTL(pon),
				   QPNP_PON_POWER_OFF_MASK, type);
	if (rc)
		return rc;

	rc = qpnp_pon_masked_write(pon, rst_en_reg, QPNP_PON_RESET_EN,
				   QPNP_PON_RESET_EN);
	if (rc)
		return rc;

	dev_dbg(pon->dev, "ps_hold power off type = 0x%02X\n", type);

	return 0;
}

static int qpnp_resin_pon_reset_config(struct qpnp_pon *pon,
				       enum pon_power_off_type type)
{
	int rc;
	bool disable = false;
	u16 rst_en_reg;

	if (pon->pon_ver == QPNP_PON_GEN1_V1)
		rst_en_reg = QPNP_PON_RESIN_S2_CNTL(pon);
	else
		rst_en_reg = QPNP_PON_RESIN_S2_CNTL2(pon);

	/*
	 * Based on the power-off type set for a PON device through device tree
	 * change the type being configured into PON_RESIN_S2_CTL.
	 */
	switch (type) {
	case PON_POWER_OFF_WARM_RESET:
		if (pon->resin_warm_reset_type != -EINVAL)
			type = pon->resin_warm_reset_type;
		break;
	case PON_POWER_OFF_HARD_RESET:
		if (pon->resin_hard_reset_type != -EINVAL)
			type = pon->resin_hard_reset_type;
		disable = pon->resin_hard_reset_disable;
		break;
	case PON_POWER_OFF_SHUTDOWN:
		if (pon->resin_shutdown_type != -EINVAL)
			type = pon->resin_shutdown_type;
		disable = pon->resin_shutdown_disable;
		break;
	default:
		break;
	}

	rc = qpnp_pon_masked_write(pon, rst_en_reg, QPNP_PON_S2_CNTL_EN, 0);
	if (rc)
		return rc;

	/*
	 * Check if resin power off configuration needs to be disabled.
	 * If yes, then return without configuring.
	 */
	if (disable)
		return 0;

	/*
	 * We need 10 sleep clock cycles here. But since the clock is
	 * internally generated, we need to add 50% tolerance to be
	 * conservative.
	 */
	udelay(500);

	rc = qpnp_pon_masked_write(pon, QPNP_PON_RESIN_S2_CNTL(pon),
				   QPNP_PON_S2_CNTL_TYPE_MASK, type);
	if (rc)
		return rc;

	rc = qpnp_pon_masked_write(pon, rst_en_reg, QPNP_PON_S2_CNTL_EN,
				   QPNP_PON_S2_CNTL_EN);
	if (rc)
		return rc;

	dev_dbg(pon->dev, "resin power off type = 0x%02X\n", type);

	return 0;
}

/**
 * qpnp_pon_system_pwr_off() - Configure system-reset PMIC for shutdown or reset
 * @type: Determines the type of power off to perform - shutdown, reset, etc
 *
 * This function supports configuration of multiple PMICs. In some cases, the
 * PON of secondary PMICs also needs to be configured, so this supports that
 * requirement. Once the system-reset and secondary PMIC is configured properly,
 * the MSM can drop PS_HOLD to activate the specified configuration.
 *
 * Note that this function may be called from atomic context as in the case of
 * the panic notifier path and thus it should not rely on function calls that
 * may sleep.
 */
int qpnp_pon_system_pwr_off(enum pon_power_off_type type)
{
	struct qpnp_pon *pon, *tmp;
	unsigned long flags;
	int rc;

	if (!sys_reset_dev)
		return -ENODEV;

	rc = qpnp_pon_reset_config(sys_reset_dev, type);
	if (rc) {
		dev_err(sys_reset_dev->dev, "Error configuring main PON, rc=%d\n",
			rc);
		return rc;
	}

	/*
	 * Check if a secondary PON device needs to be configured. If it
	 * is available, configure that also as per the requested power-off
	 * type
	 */
	spin_lock_irqsave(&spon_list_slock, flags);
	if (list_empty(&spon_dev_list))
		goto out;

	list_for_each_entry_safe(pon, tmp, &spon_dev_list, list) {
		dev_emerg(pon->dev, "PMIC@SID%d: configuring PON for reset\n",
			  to_spmi_device(pon->dev->parent)->usid);
		rc = qpnp_pon_reset_config(pon, type);
		if (rc) {
			dev_err(pon->dev, "Error configuring secondary PON, rc=%d\n",
				rc);
			goto out;
		}
		if (pon->resin_pon_reset) {
			rc = qpnp_resin_pon_reset_config(pon, type);
			if (rc) {
				dev_err(pon->dev, "Error configuring secondary PON resin, rc=%d\n",
					rc);
				goto out;
			}
		}
	}

out:
	spin_unlock_irqrestore(&spon_list_slock, flags);

	return rc;
}
EXPORT_SYMBOL(qpnp_pon_system_pwr_off);

/**
 * qpnp_pon_modem_pwr_off() - shutdown or reset the modem PMIC
 * @type: Determines the type of power off to perform - shutdown, reset, etc
 *
 * This function causes the immediate shutdown or reset of the primary PMIC
 * for an attached modem chip.
 *
 * Return: 0 for success or < 0 for errors
 */
int qpnp_pon_modem_pwr_off(enum pon_power_off_type type)
{
	struct qpnp_pon *pon = modem_reset_dev;
	int rc;

	if (!modem_reset_dev)
		return -ENODEV;

	rc = qpnp_pon_write(pon, QPNP_PON_SW_RST_S2_CTL2(pon), 0);
	if (rc)
		return rc;

	/* Wait for at least 10 sleep clock cycles. */
	udelay(500);

	rc = qpnp_pon_write(pon, QPNP_PON_SW_RST_S2_CTL(pon), type);
	if (rc)
		return rc;

	rc = qpnp_pon_write(pon, QPNP_PON_SW_RST_S2_CTL2(pon),
				QPNP_PON_RESET_EN);
	if (rc)
		return rc;

	/* Wait for at least 10 sleep clock cycles. */
	udelay(500);

	rc = qpnp_pon_write(pon, QPNP_PON_SW_RST_GO(pon),
				QPNP_PON_SW_RST_GO_VAL);
	if (rc)
		return rc;

	dev_dbg(pon->dev, "modem sw power off type = 0x%02X\n", type);

	return 0;
}
EXPORT_SYMBOL(qpnp_pon_modem_pwr_off);

static int _qpnp_pon_is_warm_reset(struct qpnp_pon *pon)
{
	if (!pon)
		return -ENODEV;

	if (is_pon_gen1(pon) || pon->subtype == PON_1REG)
		return pon->warm_reset_reason1
			|| (pon->warm_reset_reason2 & QPNP_PON_WARM_RESET_TFT);
	else
		return pon->warm_reset_reason1;
}

/**
 * qpnp_pon_is_warm_reset - Checks if the PMIC went through a warm reset.
 *
 * Returns > 0 for warm resets, 0 for not warm reset, < 0 for errors
 *
 * Note that this function will only return the warm vs not-warm reset status
 * of the PMIC that is configured as the system-reset device.
 */
int qpnp_pon_is_warm_reset(void)
{
	if (!sys_reset_dev)
		return -EPROBE_DEFER;

	if (is_pon_gen3(sys_reset_dev))
		return -EPERM;

	return _qpnp_pon_is_warm_reset(sys_reset_dev);
}
EXPORT_SYMBOL(qpnp_pon_is_warm_reset);

/**
 * qpnp_pon_wd_config() - configure the watch dog behavior for warm reset
 * @enable: to enable or disable the PON watch dog
 *
 * Return: 0 for success or < 0 for errors
 */
int qpnp_pon_wd_config(bool enable)
{
	if (!sys_reset_dev)
		return -EPROBE_DEFER;

	if (is_pon_gen3(sys_reset_dev))
		return -EPERM;

	return qpnp_pon_masked_write(sys_reset_dev,
				QPNP_PON_WD_RST_S2_CTL2(sys_reset_dev),
				QPNP_PON_WD_EN, enable ? QPNP_PON_WD_EN : 0);
}
EXPORT_SYMBOL(qpnp_pon_wd_config);

static int qpnp_pon_get_trigger_config(enum pon_trigger_source pon_src,
							bool *enabled)
{
	struct qpnp_pon *pon = sys_reset_dev;
	int val, rc;
	u16 addr;
	u8 mask;

	if (!pon)
		return -ENODEV;

	if (pon_src < PON_SMPL || pon_src > PON_KPDPWR_N) {
		dev_err(pon->dev, "Invalid PON source %d\n", pon_src);
		return -EINVAL;
	}

	addr = QPNP_PON_TRIGGER_EN(pon);
	mask = BIT(pon_src);
	if (is_pon_gen2(pon) && pon_src == PON_SMPL) {
		addr = QPNP_PON_SMPL_CTL(pon);
		mask = QPNP_PON_SMPL_EN;
	}

	rc = qpnp_pon_read(pon, addr, &val);
	if (rc)
		return rc;

	*enabled = !!(val & mask);

	return 0;
}

/**
 * qpnp_pon_trigger_config() - Configures enable state of the PON trigger source
 * @pon_src: PON source to be configured
 * @enable: to enable or disable the PON trigger
 *
 * This function configures the power-on trigger capability of a
 * PON source. If a specific PON trigger is disabled it cannot act
 * as a power-on source to the PMIC.
 */

int qpnp_pon_trigger_config(enum pon_trigger_source pon_src, bool enable)
{
	struct qpnp_pon *pon = sys_reset_dev;
	int rc;

	if (!pon)
		return -EPROBE_DEFER;

	if (pon_src < PON_SMPL || pon_src > PON_KPDPWR_N) {
		dev_err(pon->dev, "Invalid PON source %d\n", pon_src);
		return -EINVAL;
	}

	if (is_pon_gen3(sys_reset_dev))
		return -EPERM;

	if (is_pon_gen2(pon) && pon_src == PON_SMPL)
		rc = qpnp_pon_masked_write_backup(pon, QPNP_PON_SMPL_CTL(pon),
			QPNP_PON_SMPL_EN, enable ? QPNP_PON_SMPL_EN : 0);
	else
		rc = qpnp_pon_masked_write_backup(pon, QPNP_PON_TRIGGER_EN(pon),
				BIT(pon_src), enable ? BIT(pon_src) : 0);

	return rc;
}
EXPORT_SYMBOL(qpnp_pon_trigger_config);

/*
 * This function stores the PMIC warm reset reason register values. It also
 * clears these registers if the qcom,clear-warm-reset device tree property
 * is specified.
 */
static int qpnp_pon_store_and_clear_warm_reset(struct qpnp_pon *pon)
{
	int rc;
	uint val;

	rc = qpnp_pon_read(pon, QPNP_PON_WARM_RESET_REASON1(pon), &val);
	if (rc)
		return rc;
	pon->warm_reset_reason1 = (u8)val;

	if (is_pon_gen1(pon) || pon->subtype == PON_1REG) {
		rc = qpnp_pon_read(pon, QPNP_PON_WARM_RESET_REASON2(pon), &val);
		if (rc)
			return rc;
		pon->warm_reset_reason2 = (u8)val;
	}

	if (of_property_read_bool(pon->dev->of_node, "qcom,clear-warm-reset")) {
		rc = regmap_write(pon->regmap, QPNP_PON_WARM_RESET_REASON1(pon),
				  0);
		if (rc) {
			dev_err(pon->dev, "Register write failed, addr=0x%04X, rc=%d\n",
				QPNP_PON_WARM_RESET_REASON1(pon), rc);
			return rc;
		}
		qpnp_pon_store_reg(pon, QPNP_PON_WARM_RESET_REASON1(pon));
	}

	return 0;
}

static struct qpnp_pon_config *qpnp_get_cfg(struct qpnp_pon *pon, u32 pon_type)
{
	int i;

	for (i = 0; i < pon->num_pon_config; i++) {
		if (pon_type == pon->pon_cfg[i].pon_type)
			return &pon->pon_cfg[i];
	}

	return NULL;
}

static int qpnp_pon_input_dispatch(struct qpnp_pon *pon, u32 pon_type)
{
	struct qpnp_pon_config *cfg = NULL;
	u8  pon_rt_bit = 0;
	u32 key_status;
	uint pon_rt_sts;
	u64 elapsed_us;
	int rc;

	cfg = qpnp_get_cfg(pon, pon_type);
	if (!cfg)
		return -EINVAL;

	/* Check if key reporting is supported */
	if (!cfg->key_code)
		return 0;

	if (pon->kpdpwr_dbc_enable && cfg->pon_type == PON_KPDPWR) {
		elapsed_us = ktime_us_delta(ktime_get(),
				pon->kpdpwr_last_release_time);
		if (elapsed_us < pon->dbc_time_us) {
			pr_debug("Ignoring kpdpwr event; within debounce time\n");
			return 0;
		}
	}

	/* Check the RT status to get the current status of the line */
	rc = qpnp_pon_read(pon, QPNP_PON_RT_STS(pon), &pon_rt_sts);
	if (rc)
		return rc;

	switch (cfg->pon_type) {
	case PON_KPDPWR:
		pon_rt_bit = is_pon_gen3(pon)
				? QPNP_PON_GEN3_KPDPWR_N_SET
				: QPNP_PON_KPDPWR_N_SET;
		break;
	case PON_RESIN:
		pon_rt_bit = is_pon_gen3(pon)
				? QPNP_PON_GEN3_RESIN_N_SET
				: QPNP_PON_RESIN_N_SET;
		break;
	case PON_CBLPWR:
		pon_rt_bit = QPNP_PON_CBLPWR_N_SET;
		break;
	case PON_KPDPWR_RESIN:
		pon_rt_bit = QPNP_PON_KPDPWR_RESIN_BARK_N_SET;
		break;
	default:
		return -EINVAL;
	}

	pr_debug("PMIC input: code=%d, status=0x%02X\n", cfg->key_code,
		pon_rt_sts);
	key_status = pon_rt_sts & pon_rt_bit;

	if (pon->kpdpwr_dbc_enable && cfg->pon_type == PON_KPDPWR) {
		if (!key_status)
			pon->kpdpwr_last_release_time = ktime_get();
	}

	/*
	 * Simulate a press event in case release event occurred without a press
	 * event
	 */
	if (!cfg->old_state && !key_status) {
		input_report_key(pon->pon_input, cfg->key_code, 1);
		input_sync(pon->pon_input);
	}

	input_report_key(pon->pon_input, cfg->key_code, key_status);
	input_sync(pon->pon_input);

	cfg->old_state = !!key_status;

	return 0;
}

static irqreturn_t qpnp_kpdpwr_irq(int irq, void *_pon)
{
	int rc;
	struct qpnp_pon *pon = _pon;

	rc = qpnp_pon_input_dispatch(pon, PON_KPDPWR);
	if (rc)
		dev_err(pon->dev, "Unable to send input event, rc=%d\n", rc);

	return IRQ_HANDLED;
}

static irqreturn_t qpnp_kpdpwr_bark_irq(int irq, void *_pon)
{
	return IRQ_HANDLED;
}

static irqreturn_t qpnp_resin_irq(int irq, void *_pon)
{
	int rc;
	struct qpnp_pon *pon = _pon;

	rc = qpnp_pon_input_dispatch(pon, PON_RESIN);
	if (rc)
		dev_err(pon->dev, "Unable to send input event, rc=%d\n", rc);

	return IRQ_HANDLED;
}

static irqreturn_t qpnp_kpdpwr_resin_bark_irq(int irq, void *_pon)
{
	return IRQ_HANDLED;
}

static irqreturn_t qpnp_cblpwr_irq(int irq, void *_pon)
{
	int rc;
	struct qpnp_pon *pon = _pon;

	rc = qpnp_pon_input_dispatch(pon, PON_CBLPWR);
	if (rc)
		dev_err(pon->dev, "Unable to send input event, rc=%d\n", rc);

	return IRQ_HANDLED;
}

static void print_pon_reg(struct qpnp_pon *pon, u16 offset)
{
	int rc;
	u16 addr;
	uint reg;

	addr = pon->base + offset;
	rc = regmap_read(pon->regmap, addr, &reg);
	if (rc)
		dev_emerg(pon->dev, "Register read failed, addr=0x%04X, rc=%d\n",
			  addr, rc);
	else
		dev_emerg(pon->dev, "reg@0x%04X: 0x%02X\n", addr, reg);
}

#define PON_PBL_STATUS			0x7
#define PON_PON_REASON1(subtype)	PON_OFFSET(subtype, 0x8, 0xC0)
#define PON_PON_REASON2			0x9
#define PON_WARM_RESET_REASON1(subtype)	PON_OFFSET(subtype, 0xA, 0xC2)
#define PON_WARM_RESET_REASON2		0xB
#define PON_POFF_REASON1(subtype)	PON_OFFSET(subtype, 0xC, 0xC5)
#define PON_POFF_REASON2		0xD
#define PON_SOFT_RESET_REASON1(subtype)	PON_OFFSET(subtype, 0xE, 0xCB)
#define PON_SOFT_RESET_REASON2		0xF
#define PON_FAULT_REASON1		0xC8
#define PON_FAULT_REASON2		0xC9
#define PON_PMIC_WD_RESET_S1_TIMER	0x54
#define PON_PMIC_WD_RESET_S2_TIMER	0x55

static irqreturn_t qpnp_pmic_wd_bark_irq(int irq, void *_pon)
{
	struct qpnp_pon *pon = _pon;

	print_pon_reg(pon, PON_PBL_STATUS);
	print_pon_reg(pon, PON_PON_REASON1(pon->subtype));
	print_pon_reg(pon, PON_WARM_RESET_REASON1(pon->subtype));
	print_pon_reg(pon, PON_SOFT_RESET_REASON1(pon->subtype));
	print_pon_reg(pon, PON_POFF_REASON1(pon->subtype));
	if (is_pon_gen1(pon) || pon->subtype == PON_1REG) {
		print_pon_reg(pon, PON_PON_REASON2);
		print_pon_reg(pon, PON_WARM_RESET_REASON2);
		print_pon_reg(pon, PON_POFF_REASON2);
		print_pon_reg(pon, PON_SOFT_RESET_REASON2);
	} else {
		print_pon_reg(pon, PON_FAULT_REASON1);
		print_pon_reg(pon, PON_FAULT_REASON2);
	}
	print_pon_reg(pon, PON_PMIC_WD_RESET_S1_TIMER);
	print_pon_reg(pon, PON_PMIC_WD_RESET_S2_TIMER);
	panic("PMIC Watch Dog Triggered");

	return IRQ_HANDLED;
}

static void bark_work_func(struct work_struct *work)
{
	struct qpnp_pon *pon =
			container_of(work, struct qpnp_pon, bark_work.work);
	uint pon_rt_sts = 0;
	struct qpnp_pon_config *cfg;
	int rc;

	cfg = qpnp_get_cfg(pon, PON_RESIN);
	if (!cfg) {
		dev_err(pon->dev, "Invalid config pointer\n");
		return;
	}

	/* Enable reset */
	rc = qpnp_pon_masked_write(pon, cfg->s2_cntl2_addr, QPNP_PON_S2_CNTL_EN,
				   QPNP_PON_S2_CNTL_EN);
	if (rc)
		return;

	/* Wait for bark RT status update delay */
	msleep(100);

	/* Read the bark RT status */
	rc = qpnp_pon_read(pon, QPNP_PON_RT_STS(pon), &pon_rt_sts);
	if (rc)
		return;

	if (!(pon_rt_sts & QPNP_PON_RESIN_BARK_N_SET)) {
		/* Report the key event and enable the bark IRQ */
		input_report_key(pon->pon_input, cfg->key_code, 0);
		input_sync(pon->pon_input);
		enable_irq(cfg->bark_irq);
	} else {
		/* Disable reset */
		rc = qpnp_pon_masked_write(pon, cfg->s2_cntl2_addr,
				QPNP_PON_S2_CNTL_EN, 0);
		if (rc)
			return;

		/* Re-arm the work */
		schedule_delayed_work(&pon->bark_work, QPNP_KEY_STATUS_DELAY);
	}
}

static irqreturn_t qpnp_resin_bark_irq(int irq, void *_pon)
{
	struct qpnp_pon *pon = _pon;
	struct qpnp_pon_config *cfg;
	int rc;

	/* Disable the bark interrupt */
	disable_irq_nosync(irq);

	cfg = qpnp_get_cfg(pon, PON_RESIN);
	if (!cfg) {
		dev_err(pon->dev, "Invalid config pointer\n");
		return IRQ_HANDLED;
	}

	/* Disable reset */
	rc = qpnp_pon_masked_write(pon, cfg->s2_cntl2_addr,
					QPNP_PON_S2_CNTL_EN, 0);
	if (rc)
		return IRQ_HANDLED;

	/* Report the key event */
	input_report_key(pon->pon_input, cfg->key_code, 1);
	input_sync(pon->pon_input);

	/* Schedule work to check the bark status for key-release */
	schedule_delayed_work(&pon->bark_work, QPNP_KEY_STATUS_DELAY);

	return IRQ_HANDLED;
}

static int qpnp_config_pull(struct qpnp_pon *pon, struct qpnp_pon_config *cfg)
{
	u8 pull_bit;

	if (is_pon_gen3(pon)) {
		/* PON_GEN3 doesn't have pull control */
		return 0;
	}

	switch (cfg->pon_type) {
	case PON_KPDPWR:
		pull_bit = QPNP_PON_KPDPWR_PULL_UP;
		break;
	case PON_RESIN:
		pull_bit = QPNP_PON_RESIN_PULL_UP;
		break;
	case PON_CBLPWR:
		pull_bit = QPNP_PON_CBLPWR_PULL_UP;
		break;
	case PON_KPDPWR_RESIN:
		pull_bit = QPNP_PON_KPDPWR_PULL_UP | QPNP_PON_RESIN_PULL_UP;
		break;
	default:
		return -EINVAL;
	}

	return qpnp_pon_masked_write_backup(pon, QPNP_PON_PULL_CTL(pon),
				pull_bit, cfg->pull_up ? pull_bit : 0);
}

static int qpnp_config_reset(struct qpnp_pon *pon, struct qpnp_pon_config *cfg)
{
	u16 s1_timer_addr, s2_timer_addr;
	int rc;
	u8 i;

	switch (cfg->pon_type) {
	case PON_KPDPWR:
		s1_timer_addr = QPNP_PON_KPDPWR_S1_TIMER(pon);
		s2_timer_addr = QPNP_PON_KPDPWR_S2_TIMER(pon);
		break;
	case PON_RESIN:
		s1_timer_addr = QPNP_PON_RESIN_S1_TIMER(pon);
		s2_timer_addr = QPNP_PON_RESIN_S2_TIMER(pon);
		break;
	case PON_KPDPWR_RESIN:
		s1_timer_addr = QPNP_PON_KPDPWR_RESIN_S1_TIMER(pon);
		s2_timer_addr = QPNP_PON_KPDPWR_RESIN_S2_TIMER(pon);
		break;
	default:
		return -EINVAL;
	}

	/* Disable S2 reset */
	rc = qpnp_pon_masked_write_backup(pon, cfg->s2_cntl2_addr,
				QPNP_PON_S2_CNTL_EN, 0);
	if (rc)
		return rc;

	usleep_range(100, 120);

	/* configure s1 timer, s2 timer and reset type */
	for (i = 0; i < PON_S1_COUNT_MAX + 1; i++) {
		if (cfg->s1_timer <= s1_delay[i])
			break;
	}
	rc = qpnp_pon_masked_write_backup(pon, s1_timer_addr,
				QPNP_PON_S1_TIMER_MASK, i);
	if (rc)
		return rc;

	i = 0;
	if (cfg->s2_timer) {
		i = cfg->s2_timer / 10;
		i = ilog2(i + 1);
	}

	rc = qpnp_pon_masked_write_backup(pon, s2_timer_addr,
				QPNP_PON_S2_TIMER_MASK, i);
	if (rc)
		return rc;

	rc = qpnp_pon_masked_write_backup(pon, cfg->s2_cntl_addr,
				QPNP_PON_S2_CNTL_TYPE_MASK, (u8)cfg->s2_type);
	if (rc)
		return rc;

	/* Enable S2 reset */
	return qpnp_pon_masked_write_backup(pon, cfg->s2_cntl2_addr,
				     QPNP_PON_S2_CNTL_EN, QPNP_PON_S2_CNTL_EN);
}

static int
qpnp_pon_request_irqs(struct qpnp_pon *pon, struct qpnp_pon_config *cfg)
{
	int rc;

	switch (cfg->pon_type) {
	case PON_KPDPWR:
		rc = devm_request_irq(pon->dev, cfg->state_irq, qpnp_kpdpwr_irq,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"pon_kpdpwr_status", pon);
		if (rc < 0) {
			dev_err(pon->dev, "IRQ %d request failed, rc=%d\n",
				cfg->state_irq, rc);
			return rc;
		}

		if (cfg->use_bark) {
			rc = devm_request_irq(pon->dev, cfg->bark_irq,
						qpnp_kpdpwr_bark_irq,
						IRQF_TRIGGER_RISING,
						"pon_kpdpwr_bark", pon);
			if (rc < 0) {
				dev_err(pon->dev, "IRQ %d request failed, rc=%d\n",
					cfg->bark_irq, rc);
				return rc;
			}
		}
		break;
	case PON_RESIN:
		rc = devm_request_irq(pon->dev, cfg->state_irq, qpnp_resin_irq,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"pon_resin_status", pon);
		if (rc < 0) {
			dev_err(pon->dev, "IRQ %d request failed, rc=%d\n",
				cfg->state_irq, rc);
			return rc;
		}

		if (cfg->use_bark) {
			rc = devm_request_irq(pon->dev, cfg->bark_irq,
						qpnp_resin_bark_irq,
						IRQF_TRIGGER_RISING,
						"pon_resin_bark", pon);
			if (rc < 0) {
				dev_err(pon->dev, "IRQ %d request failed, rc=%d\n",
					cfg->bark_irq, rc);
				return rc;
			}
		}
		break;
	case PON_CBLPWR:
		rc = devm_request_irq(pon->dev, cfg->state_irq, qpnp_cblpwr_irq,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"pon_cblpwr_status", pon);
		if (rc < 0) {
			dev_err(pon->dev, "IRQ %d request failed, rc=%d\n",
				cfg->state_irq, rc);
			return rc;
		}
		break;
	case PON_KPDPWR_RESIN:
		if (cfg->use_bark) {
			rc = devm_request_irq(pon->dev, cfg->bark_irq,
						qpnp_kpdpwr_resin_bark_irq,
						IRQF_TRIGGER_RISING,
						"pon_kpdpwr_resin_bark", pon);
			if (rc < 0) {
				dev_err(pon->dev, "IRQ %d request failed, rc=%d\n",
					cfg->bark_irq, rc);
				return rc;
			}
		}
		break;
	default:
		return -EINVAL;
	}

	/* Mark the interrupts wakeable if they support linux-key */
	if (cfg->key_code) {
		enable_irq_wake(cfg->state_irq);

		/* Special handling for RESIN due to a hardware bug */
		if (cfg->pon_type == PON_RESIN && cfg->support_reset)
			enable_irq_wake(cfg->bark_irq);
	}

	return 0;
}

static int
qpnp_pon_free_irqs(struct qpnp_pon *pon, struct qpnp_pon_config *cfg)
{
	if (cfg->state_irq > 0)
		devm_free_irq(pon->dev, cfg->state_irq, pon);

	if (cfg->use_bark && cfg->bark_irq > 0)
		devm_free_irq(pon->dev, cfg->bark_irq, pon);

	return 0;
}

static int
qpnp_pon_config_input(struct qpnp_pon *pon, struct qpnp_pon_config *cfg)
{
	if (!pon->pon_input) {
		pon->pon_input = devm_input_allocate_device(pon->dev);
		if (!pon->pon_input)
			return -ENOMEM;

		pon->pon_input->name = "qpnp_pon";
		pon->pon_input->phys = "qpnp_pon/input0";
	}

	input_set_capability(pon->pon_input, EV_KEY, cfg->key_code);

	return 0;
}

static int qpnp_pon_config_kpdpwr_init(struct qpnp_pon *pon,
				       struct platform_device *pdev,
				       struct qpnp_pon_config *cfg,
				       struct device_node *node)
{
	int rc;

	cfg->state_irq = platform_get_irq_byname(pdev, "kpdpwr");
	if (cfg->state_irq < 0) {
		dev_err(pon->dev, "Unable to get kpdpwr irq, rc=%d\n",
			cfg->state_irq);
		return cfg->state_irq;
	}

	rc = of_property_read_u32(node, "qcom,support-reset",
				  &cfg->support_reset);
	if (rc) {
		if (rc != -EINVAL) {
			dev_err(pon->dev, "Unable to read qcom,support-reset, rc=%d\n",
				rc);
			return rc;
		}
	} else {
		cfg->config_reset = true;
	}

	cfg->use_bark = of_property_read_bool(node, "qcom,use-bark");
	if (cfg->use_bark) {
		cfg->bark_irq = platform_get_irq_byname(pdev, "kpdpwr-bark");
		if (cfg->bark_irq < 0) {
			dev_err(pon->dev, "Unable to get kpdpwr-bark irq, rc=%d\n",
				cfg->bark_irq);
			return cfg->bark_irq;
		}
	}

	if (pon->pon_ver == QPNP_PON_GEN1_V1) {
		cfg->s2_cntl_addr = cfg->s2_cntl2_addr =
			QPNP_PON_KPDPWR_S2_CNTL(pon);
	} else {
		cfg->s2_cntl_addr = QPNP_PON_KPDPWR_S2_CNTL(pon);
		cfg->s2_cntl2_addr = QPNP_PON_KPDPWR_S2_CNTL2(pon);
	}

	return 0;
}

static int qpnp_pon_config_resin_init(struct qpnp_pon *pon,
				       struct platform_device *pdev,
				       struct qpnp_pon_config *cfg,
				       struct device_node *node)
{
	unsigned int pmic_type, revid_rev4;
	int rc;

	cfg->state_irq = platform_get_irq_byname(pdev, "resin");
	if (cfg->state_irq < 0) {
		dev_err(pon->dev, "Unable to get resin irq, rc=%d\n",
			cfg->state_irq);
		return cfg->state_irq;
	}

	rc = of_property_read_u32(node, "qcom,support-reset",
				  &cfg->support_reset);
	if (rc) {
		if (rc != -EINVAL) {
			dev_err(pon->dev, "Unable to read qcom,support-reset, rc=%d\n",
				rc);
			return rc;
		}
	} else {
		cfg->config_reset = true;
	}

	cfg->use_bark = of_property_read_bool(node, "qcom,use-bark");

	rc = qpnp_pon_read(pon, PMIC_VERSION_REG, &pmic_type);
	if (rc)
		return rc;

	if (pmic_type == PMIC_VER_8941) {
		rc = qpnp_pon_read(pon, PMIC_VERSION_REV4_REG, &revid_rev4);
		if (rc)
			return rc;

		/*
		 * PM8941 V3 does not have hardware bug. Hence
		 * bark is not required from PMIC versions 3.0.
		 */
		if (!(revid_rev4 == PMIC8941_V1_REV4 ||
		      revid_rev4 == PMIC8941_V2_REV4)) {
			cfg->support_reset = false;
			cfg->use_bark = false;
		}
	}

	if (cfg->use_bark) {
		cfg->bark_irq = platform_get_irq_byname(pdev, "resin-bark");
		if (cfg->bark_irq < 0) {
			dev_err(pon->dev, "Unable to get resin-bark irq, rc=%d\n",
				cfg->bark_irq);
			return cfg->bark_irq;
		}
	}

	if (pon->pon_ver == QPNP_PON_GEN1_V1) {
		cfg->s2_cntl_addr = cfg->s2_cntl2_addr =
			QPNP_PON_RESIN_S2_CNTL(pon);
	} else {
		cfg->s2_cntl_addr = QPNP_PON_RESIN_S2_CNTL(pon);
		cfg->s2_cntl2_addr = QPNP_PON_RESIN_S2_CNTL2(pon);
	}

	return 0;
}

static int qpnp_pon_config_cblpwr_init(struct qpnp_pon *pon,
				       struct platform_device *pdev,
				       struct qpnp_pon_config *cfg,
				       struct device_node *node)
{
	cfg->state_irq = platform_get_irq_byname(pdev, "cblpwr");
	if (cfg->state_irq < 0) {
		dev_err(pon->dev, "Unable to get cblpwr irq, rc=%d\n",
			cfg->state_irq);
		return cfg->state_irq;
	}

	return 0;
}

static int qpnp_pon_config_kpdpwr_resin_init(struct qpnp_pon *pon,
				       struct platform_device *pdev,
				       struct qpnp_pon_config *cfg,
				       struct device_node *node)
{
	int rc;

	rc = of_property_read_u32(node, "qcom,support-reset",
				  &cfg->support_reset);
	if (rc) {
		if (rc != -EINVAL) {
			dev_err(pon->dev, "Unable to read qcom,support-reset, rc=%d\n",
				rc);
			return rc;
		}
	} else {
		cfg->config_reset = true;
	}

	cfg->use_bark = of_property_read_bool(node, "qcom,use-bark");
	if (cfg->use_bark) {
		cfg->bark_irq = platform_get_irq_byname(pdev,
							"kpdpwr-resin-bark");
		if (cfg->bark_irq < 0) {
			dev_err(pon->dev, "Unable to get kpdpwr-resin-bark irq, rc=%d\n",
				cfg->bark_irq);
			return cfg->bark_irq;
		}
	}

	if (pon->pon_ver == QPNP_PON_GEN1_V1) {
		cfg->s2_cntl_addr = cfg->s2_cntl2_addr =
			QPNP_PON_KPDPWR_RESIN_S2_CNTL(pon);
	} else {
		cfg->s2_cntl_addr = QPNP_PON_KPDPWR_RESIN_S2_CNTL(pon);
		cfg->s2_cntl2_addr = QPNP_PON_KPDPWR_RESIN_S2_CNTL2(pon);
	}

	return 0;
}

static int qpnp_pon_config_parse_reset_info(struct qpnp_pon *pon,
					    struct qpnp_pon_config *cfg,
					    struct device_node *node)
{
	int rc;

	if (!cfg->support_reset)
		return 0;

	/*
	 * Get the reset parameters (bark debounce time and
	 * reset debounce time) for the reset line.
	 */
	rc = of_property_read_u32(node, "qcom,s1-timer", &cfg->s1_timer);
	if (rc) {
		dev_err(pon->dev, "Unable to read s1-timer, rc=%d\n", rc);
		return rc;
	}
	if (cfg->s1_timer > QPNP_PON_S1_TIMER_MAX) {
		dev_err(pon->dev, "Invalid S1 debounce time %u\n",
			cfg->s1_timer);
		return -EINVAL;
	}

	rc = of_property_read_u32(node, "qcom,s2-timer", &cfg->s2_timer);
	if (rc) {
		dev_err(pon->dev, "Unable to read s2-timer, rc=%d\n", rc);
		return rc;
	}
	if (cfg->s2_timer > QPNP_PON_S2_TIMER_MAX) {
		dev_err(pon->dev, "Invalid S2 debounce time %u\n",
			cfg->s2_timer);
		return -EINVAL;
	}

	rc = of_property_read_u32(node, "qcom,s2-type", &cfg->s2_type);
	if (rc) {
		dev_err(pon->dev, "Unable to read s2-type, rc=%d\n", rc);
		return rc;
	}
	if (cfg->s2_type > QPNP_PON_RESET_TYPE_MAX) {
		dev_err(pon->dev, "Invalid reset type specified %u\n",
			cfg->s2_type);
		return -EINVAL;
	}

	return 0;
}

static int qpnp_pon_config_init(struct qpnp_pon *pon,
				struct platform_device *pdev)
{
	int rc = 0, i = 0, pmic_wd_bark_irq;
	struct device_node *cfg_node = NULL;
	struct qpnp_pon_config *cfg;

	if (pon->num_pon_config) {
		pon->pon_cfg = devm_kcalloc(pon->dev, pon->num_pon_config,
					    sizeof(*pon->pon_cfg), GFP_KERNEL);
		if (!pon->pon_cfg)
			return -ENOMEM;
	}

	/* Iterate through the list of pon configs */
	for_each_available_child_of_node(pon->dev->of_node, cfg_node) {
		if (!of_find_property(cfg_node, "qcom,pon-type", NULL))
			continue;

		cfg = &pon->pon_cfg[i++];

		rc = of_property_read_u32(cfg_node, "qcom,pon-type",
					  &cfg->pon_type);
		if (rc) {
			dev_err(pon->dev, "PON type not specified\n");
			return rc;
		}

		switch (cfg->pon_type) {
		case PON_KPDPWR:
			rc = qpnp_pon_config_kpdpwr_init(pon, pdev, cfg,
							 cfg_node);
			if (rc)
				return rc;
			break;
		case PON_RESIN:
			rc = qpnp_pon_config_resin_init(pon, pdev, cfg,
							cfg_node);
			if (rc)
				return rc;
			break;
		case PON_CBLPWR:
			rc = qpnp_pon_config_cblpwr_init(pon, pdev, cfg,
							 cfg_node);
			if (rc)
				return rc;
			break;
		case PON_KPDPWR_RESIN:
			rc = qpnp_pon_config_kpdpwr_resin_init(pon, pdev, cfg,
							       cfg_node);
			if (rc)
				return rc;
			break;
		default:
			dev_err(pon->dev, "PON RESET %u not supported\n",
				cfg->pon_type);
			return -EINVAL;
		}

		rc = qpnp_pon_config_parse_reset_info(pon, cfg, cfg_node);
		if (rc)
			return rc;

		/*
		 * Get the standard key parameters. This might not be
		 * specified if there is no key mapping on the reset line.
		 */
		of_property_read_u32(cfg_node, "linux,code", &cfg->key_code);

		/* Register key configuration */
		if (cfg->key_code) {
			rc = qpnp_pon_config_input(pon, cfg);
			if (rc < 0)
				return rc;
		}

		/* Get the pull-up configuration */
		cfg->pull_up = of_property_read_bool(cfg_node, "qcom,pull-up");
	}

	pmic_wd_bark_irq = platform_get_irq_byname(pdev, "pmic-wd-bark");
	/* Request the pmic-wd-bark irq only if it is defined */
	if (pmic_wd_bark_irq >= 0) {
		rc = devm_request_irq(pon->dev, pmic_wd_bark_irq,
					qpnp_pmic_wd_bark_irq,
					IRQF_TRIGGER_RISING,
					"qpnp_pmic_wd_bark", pon);
		if (rc < 0) {
			dev_err(pon->dev, "Can't request %d IRQ, rc=%d\n",
				pmic_wd_bark_irq, rc);
			return rc;
		}
	}

	/* Register the input device */
	if (pon->pon_input) {
		rc = input_register_device(pon->pon_input);
		if (rc) {
			dev_err(pon->dev, "Can't register pon key: %d\n", rc);
			return rc;
		}
	}

	for (i = 0; i < pon->num_pon_config; i++) {
		cfg = &pon->pon_cfg[i];

		rc = qpnp_config_pull(pon, cfg);
		if (rc)
			return rc;

		if (cfg->config_reset) {
			if (cfg->support_reset) {
				rc = qpnp_config_reset(pon, cfg);
				if (rc)
					return rc;
			} else if (cfg->pon_type != PON_CBLPWR) {
				/* Disable S2 reset */
				rc = qpnp_pon_masked_write_backup(pon,
							cfg->s2_cntl2_addr,
							QPNP_PON_S2_CNTL_EN, 0);
				if (rc)
					return rc;
			}
		}

		rc = qpnp_pon_request_irqs(pon, cfg);
		if (rc)
			return rc;
	}

	device_init_wakeup(pon->dev, true);

	return 0;
}

static int pon_spare_regulator_enable(struct regulator_dev *rdev)
{
	struct pon_regulator *pon_reg = rdev_get_drvdata(rdev);
	u8 value;
	int rc;

	value = BIT(pon_reg->bit) & 0xFF;
	rc = qpnp_pon_masked_write(pon_reg->pon, pon_reg->pon->base +
				pon_reg->addr, value, value);
	if (rc)
		return rc;

	pon_reg->enabled = true;

	return 0;
}

static int pon_spare_regulator_disable(struct regulator_dev *rdev)
{
	struct pon_regulator *pon_reg = rdev_get_drvdata(rdev);
	u8 mask;
	int rc;

	mask = BIT(pon_reg->bit) & 0xFF;
	rc = qpnp_pon_masked_write(pon_reg->pon, pon_reg->pon->base +
				pon_reg->addr, mask, 0);
	if (rc)
		return rc;

	pon_reg->enabled = false;

	return 0;
}

static int pon_spare_regulator_is_enable(struct regulator_dev *rdev)
{
	struct pon_regulator *pon_reg = rdev_get_drvdata(rdev);

	return pon_reg->enabled;
}

static const struct regulator_ops pon_spare_reg_ops = {
	.enable		= pon_spare_regulator_enable,
	.disable	= pon_spare_regulator_disable,
	.is_enabled	= pon_spare_regulator_is_enable,
};

static int pon_regulator_init(struct qpnp_pon *pon)
{
	struct device *dev = pon->dev;
	struct regulator_init_data *init_data;
	struct regulator_config reg_cfg = {};
	struct device_node *node;
	struct pon_regulator *pon_reg;
	int rc, i;

	if (!pon->num_pon_reg)
		return 0;

	pon->pon_reg_cfg = devm_kcalloc(dev, pon->num_pon_reg,
					sizeof(*pon->pon_reg_cfg),
					GFP_KERNEL);
	if (!pon->pon_reg_cfg)
		return -ENOMEM;

	i = 0;
	for_each_available_child_of_node(dev->of_node, node) {
		if (!of_find_property(node, "regulator-name", NULL))
			continue;

		pon_reg = &pon->pon_reg_cfg[i++];
		pon_reg->pon = pon;

		rc = of_property_read_u32(node, "qcom,pon-spare-reg-addr",
					  &pon_reg->addr);
		if (rc) {
			dev_err(dev, "Unable to read address for regulator, rc=%d\n",
				rc);
			return rc;
		}

		rc = of_property_read_u32(node, "qcom,pon-spare-reg-bit",
					  &pon_reg->bit);
		if (rc) {
			dev_err(dev, "Unable to read bit for regulator, rc=%d\n",
				rc);
			return rc;
		}

		init_data = of_get_regulator_init_data(dev, node,
						       &pon_reg->rdesc);
		if (!init_data) {
			dev_err(dev, "regulator init data is missing\n");
			return -ENOMEM;
		}

		if (!init_data->constraints.name) {
			dev_err(dev, "regulator-name is missing\n");
			return -EINVAL;
		}

		pon_reg->rdesc.type = REGULATOR_VOLTAGE;
		pon_reg->rdesc.ops = &pon_spare_reg_ops;
		pon_reg->rdesc.name = init_data->constraints.name;

		reg_cfg.dev = dev;
		reg_cfg.init_data = init_data;
		reg_cfg.driver_data = pon_reg;
		reg_cfg.of_node = node;

		pon_reg->rdev = devm_regulator_register(dev, &pon_reg->rdesc,
							&reg_cfg);
		if (IS_ERR(pon_reg->rdev)) {
			rc = PTR_ERR(pon_reg->rdev);
			pon_reg->rdev = NULL;
			if (rc != -EPROBE_DEFER)
				dev_err(dev, "regulator register failed, rc=%d\n",
					rc);
			return rc;
		}
	}

	return 0;
}

static ssize_t pon_smpl_en_show(struct device *dev, struct device_attribute
		*attr, char *buf)
{
	bool enabled = false;
	int rc;

	rc = qpnp_pon_get_trigger_config(PON_SMPL, &enabled);
	if (rc < 0)
		return rc;

	return scnprintf(buf, QPNP_PON_BUFFER_SIZE, "%d", enabled);
}

static ssize_t pon_smpl_en_store(struct device *dev, struct device_attribute
		*attr, const char *buf, size_t count)
{
	bool store_val;

	if (kstrtobool(buf, &store_val)) {
		pr_err("Unable to set smpl_en\n");
		return -EINVAL;
	}

	return qpnp_pon_trigger_config(PON_SMPL, store_val);
}
static DEVICE_ATTR_RW(pon_smpl_en);

static ssize_t
pon_uvlo_dload_show(struct device *dev, struct device_attribute
		*attr, char *buf)
{
	struct qpnp_pon *pon = sys_reset_dev;
	uint reg;
	int rc;

	if (!pon)
		return -ENODEV;

	rc = qpnp_pon_read(pon, QPNP_PON_XVDD_RB_SPARE(pon), &reg);
	if (rc)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%d",
			!!(QPNP_PON_UVLO_DLOAD_EN & reg));
}

static ssize_t
pon_uvlo_dload_store(struct device *dev, struct device_attribute
		*attr, const char *val, size_t count)
{
	struct qpnp_pon *pon = sys_reset_dev;
	uint reg;
	bool store_val;

	if (!pon)
		return -ENODEV;

	if (kstrtobool(val, &store_val)) {
		pr_err("Unable to set dload_on_uvlo\n");
		return -EINVAL;
	}

	reg = store_val ? QPNP_PON_UVLO_DLOAD_EN : 0;

	return qpnp_pon_masked_write_backup(pon, QPNP_PON_XVDD_RB_SPARE(pon),
				   QPNP_PON_UVLO_DLOAD_EN, reg);
}
static DEVICE_ATTR_RW(pon_uvlo_dload);

static struct attribute *pon_attrs[] = {
	&dev_attr_pon_uvlo_dload.attr,
	&dev_attr_pon_smpl_en.attr,
	NULL,
};
ATTRIBUTE_GROUPS(pon);

#if defined(CONFIG_DEBUG_FS)

static int qpnp_pon_debugfs_uvlo_get(void *data, u64 *val)
{
	struct qpnp_pon *pon = data;

	*val = pon->uvlo;

	return 0;
}

static int qpnp_pon_debugfs_uvlo_set(void *data, u64 val)
{
	struct qpnp_pon *pon = data;

	if (pon->pon_trigger_reason == PON_SMPL ||
	    pon->pon_power_off_reason == QPNP_POFF_REASON_UVLO)
		panic("UVLO occurred");
	pon->uvlo = val;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(qpnp_pon_debugfs_uvlo_fops, qpnp_pon_debugfs_uvlo_get,
			qpnp_pon_debugfs_uvlo_set, "0x%02llx\n");

static void qpnp_pon_debugfs_init(struct qpnp_pon *pon)
{
	struct dentry *ent;

	pon->debugfs = debugfs_create_dir(dev_name(pon->dev), NULL);
	if (!pon->debugfs) {
		dev_err(pon->dev, "Unable to create debugfs directory\n");
	} else {
		ent = debugfs_create_file_unsafe("uvlo_panic", 0644,
				pon->debugfs, pon, &qpnp_pon_debugfs_uvlo_fops);
		if (!ent)
			dev_err(pon->dev, "Unable to create uvlo_panic debugfs file\n");
	}
}

static void qpnp_pon_debugfs_remove(struct qpnp_pon *pon)
{
	debugfs_remove_recursive(pon->debugfs);
}

#else

static void qpnp_pon_debugfs_init(struct qpnp_pon *pon)
{}

static void qpnp_pon_debugfs_remove(struct qpnp_pon *pon)
{}
#endif

static int qpnp_pon_read_gen2_pon_off_reason(struct qpnp_pon *pon, u16 *reason,
					int *reason_index_offset)
{
	unsigned int reg, reg1;
	u8 buf[2];
	int rc;

	rc = qpnp_pon_read(pon, QPNP_PON_OFF_REASON(pon), &reg);
	if (rc)
		return rc;

	if (reg & QPNP_GEN2_POFF_SEQ) {
		rc = qpnp_pon_read(pon, QPNP_POFF_REASON1(pon), &reg1);
		if (rc)
			return rc;
		*reason = (u8)reg1;
		*reason_index_offset = 0;
	} else if (reg & QPNP_GEN2_FAULT_SEQ) {
		rc = regmap_bulk_read(pon->regmap, QPNP_FAULT_REASON1(pon), buf,
				      2);
		if (rc) {
			dev_err(pon->dev, "Register read failed, addr=0x%04X, rc=%d\n",
				QPNP_FAULT_REASON1(pon), rc);
			return rc;
		}
		*reason = buf[0] | (u16)(buf[1] << 8);
		*reason_index_offset = POFF_REASON_FAULT_OFFSET;
	} else if (reg & QPNP_GEN2_S3_RESET_SEQ) {
		rc = qpnp_pon_read(pon, QPNP_S3_RESET_REASON(pon), &reg1);
		if (rc)
			return rc;
		*reason = (u8)reg1;
		*reason_index_offset = POFF_REASON_S3_RESET_OFFSET;
	}

	return 0;
}

static int qpnp_pon_configure_s3_reset(struct qpnp_pon *pon)
{
	struct device *dev = pon->dev;
	const char *src_name;
	u32 debounce;
	u8 src_val;
	int rc;

	/* Program S3 reset debounce time */
	rc = of_property_read_u32(dev->of_node, "qcom,s3-debounce", &debounce);
	if (!rc) {
		if (debounce > QPNP_PON_S3_TIMER_SECS_MAX) {
			dev_err(dev, "S3 debounce time %u greater than max supported %u\n",
				debounce, QPNP_PON_S3_TIMER_SECS_MAX);
			return -EINVAL;
		}

		if (debounce != 0)
			debounce = ilog2(debounce);

		/* S3 debounce is a SEC_ACCESS register */
		rc = qpnp_pon_masked_write_backup(pon, QPNP_PON_SEC_ACCESS(pon),
					0xFF, QPNP_PON_SEC_UNLOCK);
		if (rc)
			return rc;

		rc = qpnp_pon_masked_write_backup(pon, QPNP_PON_S3_DBC_CTL(pon),
					QPNP_PON_S3_DBC_DELAY_MASK, debounce);
		if (rc)
			return rc;
	}

	/* Program S3 reset source */
	rc = of_property_read_string(dev->of_node, "qcom,s3-src", &src_name);
	if (!rc) {
		if (!strcmp(src_name, "kpdpwr")) {
			src_val = QPNP_PON_S3_SRC_KPDPWR;
		} else if (!strcmp(src_name, "resin")) {
			src_val = QPNP_PON_S3_SRC_RESIN;
		} else if (!strcmp(src_name, "kpdpwr-or-resin")) {
			src_val = QPNP_PON_S3_SRC_KPDPWR_OR_RESIN;
		} else if (!strcmp(src_name, "kpdpwr-and-resin")) {
			src_val = QPNP_PON_S3_SRC_KPDPWR_AND_RESIN;
		} else {
			dev_err(dev, "Unknown S3 reset source: %s\n",
				src_name);
			return -EINVAL;
		}

		/*
		 * S3 source is a write once register. If the register has
		 * been configured by the bootloader then this operation will
		 * not have an effect.
		 */
		rc = qpnp_pon_masked_write_backup(pon, QPNP_PON_S3_SRC(pon),
					QPNP_PON_S3_SRC_MASK, src_val);
		if (rc)
			return rc;
	}

	return 0;
}

static int qpnp_pon_read_hardware_info(struct qpnp_pon *pon, bool sys_reset)
{
	struct device *dev = pon->dev;
	unsigned int reg = 0;
	u8 buf[2];
	int reason_index_offset = 0;
	unsigned int pon_sts = 0;
	bool cold_boot;
	u16 poff_sts = 0;
	int rc, index;

	/* Read PON_PERPH_SUBTYPE register to get PON type */
	rc = qpnp_pon_read(pon, QPNP_PON_PERPH_SUBTYPE(pon), &reg);
	if (rc)
		return rc;
	pon->subtype = reg;

	/* Check if it is rev B */
	rc = qpnp_pon_read(pon, QPNP_PON_REVISION2(pon), &reg);
	if (rc)
		return rc;
	pon->pon_ver = reg;

	if (is_pon_gen1(pon)) {
		if (pon->pon_ver == 0)
			pon->pon_ver = QPNP_PON_GEN1_V1;
		else
			pon->pon_ver = QPNP_PON_GEN1_V2;
	} else if (is_pon_gen2(pon)) {
		pon->pon_ver = QPNP_PON_GEN2;
	} else if (is_pon_gen3(pon)) {
		pon->pon_ver = QPNP_PON_GEN3;
	} else if (pon->subtype == PON_1REG) {
		pon->pon_ver = QPNP_PON_GEN1_V2;
	} else {
		dev_err(dev, "Invalid PON_PERPH_SUBTYPE 0x%02X\n",
			pon->subtype);
		return -EINVAL;
	}
	dev_dbg(dev, "pon_subtype=0x%02X, pon_version=0x%02X\n", pon->subtype,
		pon->pon_ver);

	if (is_pon_gen3(pon)) {
		/* PON_GEN3 does not have PON/POFF reason registers */
		return 0;
	}

	rc = qpnp_pon_store_and_clear_warm_reset(pon);
	if (rc)
		return rc;

	/* PON reason */
	rc = qpnp_pon_read(pon, QPNP_PON_REASON1(pon), &pon_sts);
	if (rc)
		return rc;

	index = ffs(pon_sts) - 1;
	cold_boot = sys_reset_dev ? !_qpnp_pon_is_warm_reset(sys_reset_dev)
				  : !_qpnp_pon_is_warm_reset(pon);
	if (index >= ARRAY_SIZE(qpnp_pon_reason) || index < 0) {
		dev_info(dev, "PMIC@SID%d Power-on reason: Unknown and '%s' boot\n",
			 to_spmi_device(dev->parent)->usid,
			 cold_boot ? "cold" : "warm");
	} else {
		pon->pon_trigger_reason = index;
		dev_info(dev, "PMIC@SID%d Power-on reason: %s and '%s' boot\n",
			 to_spmi_device(dev->parent)->usid,
			 qpnp_pon_reason[index],
			 cold_boot ? "cold" : "warm");
	}

	/* POFF reason */
	if (!is_pon_gen1(pon) && pon->subtype != PON_1REG) {
		rc = qpnp_pon_read_gen2_pon_off_reason(pon, &poff_sts,
							&reason_index_offset);
		if (rc)
			return rc;
	} else {
		rc = regmap_bulk_read(pon->regmap, QPNP_POFF_REASON1(pon), buf,
				      2);
		if (rc) {
			dev_err(dev, "Register read failed, addr=0x%04X, rc=%d\n",
				QPNP_POFF_REASON1(pon), rc);
			return rc;
		}
		poff_sts = buf[0] | (u16)(buf[1] << 8);
	}
	index = ffs(poff_sts) - 1 + reason_index_offset;
	if (index >= ARRAY_SIZE(qpnp_poff_reason) || index < 0 ||
					index < reason_index_offset) {
		dev_info(dev, "PMIC@SID%d: Unknown power-off reason\n",
			 to_spmi_device(dev->parent)->usid);
	} else {
		pon->pon_power_off_reason = index;
		dev_info(dev, "PMIC@SID%d: Power-off reason: %s\n",
			 to_spmi_device(dev->parent)->usid,
			 qpnp_poff_reason[index]);
	}

	if ((pon->pon_trigger_reason == PON_SMPL ||
		pon->pon_power_off_reason == QPNP_POFF_REASON_UVLO) &&
	    of_property_read_bool(dev->of_node, "qcom,uvlo-panic")) {
		panic("UVLO occurred");
	}

	return 0;
}

static int qpnp_pon_parse_power_off_type(struct qpnp_pon *pon,
					 const char *prop, int *val)
{
	u32 type = 0;
	int rc;

	rc = of_property_read_u32(pon->dev->of_node, prop, &type);
	if (rc) {
		if (rc != -EINVAL) {
			dev_err(pon->dev, "Unable to read property %s, rc=%d\n",
				prop, rc);
			return rc;
		}
		*val = -EINVAL;
		return 0;
	}

	if (type >= PON_POWER_OFF_MAX_TYPE) {
		dev_err(pon->dev, "Invalid property value %s=%u\n", prop, type);
		return -EINVAL;
	}

	*val = type;

	return 0;
}

static int qpnp_pon_parse_dt_power_off_config(struct qpnp_pon *pon)
{
	struct device_node *node = pon->dev->of_node;
	int rc;

	rc = qpnp_pon_parse_power_off_type(pon, "qcom,warm-reset-poweroff-type",
					   &pon->warm_reset_poff_type);
	if (rc)
		return rc;

	rc = qpnp_pon_parse_power_off_type(pon, "qcom,hard-reset-poweroff-type",
					   &pon->hard_reset_poff_type);
	if (rc)
		return rc;

	rc = qpnp_pon_parse_power_off_type(pon, "qcom,shutdown-poweroff-type",
					   &pon->shutdown_poff_type);
	if (rc)
		return rc;

	rc = qpnp_pon_parse_power_off_type(pon, "qcom,resin-warm-reset-type",
					   &pon->resin_warm_reset_type);
	if (rc)
		return rc;

	rc = qpnp_pon_parse_power_off_type(pon, "qcom,resin-hard-reset-type",
					   &pon->resin_hard_reset_type);
	if (rc)
		return rc;

	rc = qpnp_pon_parse_power_off_type(pon, "qcom,resin-shutdown-type",
					   &pon->resin_shutdown_type);
	if (rc)
		return rc;

	pon->ps_hold_hard_reset_disable = of_property_read_bool(node,
					"qcom,ps-hold-hard-reset-disable");
	pon->ps_hold_shutdown_disable = of_property_read_bool(node,
					"qcom,ps-hold-shutdown-disable");
	pon->resin_hard_reset_disable = of_property_read_bool(node,
					"qcom,resin-hard-reset-disable");
	pon->resin_shutdown_disable = of_property_read_bool(node,
					"qcom,resin-shutdown-disable");
	pon->resin_pon_reset = of_property_read_bool(node,
					"qcom,resin-pon-reset");

	return 0;
}

static int qpnp_pon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node;
	struct qpnp_pon *pon;
	unsigned long flags;
	u32 delay;
	const __be32 *addr;
	bool sys_reset, modem_reset;
	int rc;

	pon = devm_kzalloc(dev, sizeof(*pon), GFP_KERNEL);
	if (!pon)
		return -ENOMEM;
	pon->dev = dev;

	pon->regmap = dev_get_regmap(dev->parent, NULL);
	if (!pon->regmap) {
		dev_err(dev, "Parent regmap missing\n");
		return -ENODEV;
	}

	addr = of_get_address(dev->of_node, 0, NULL, NULL);
	if (!addr) {
		dev_err(dev, "reg property missing\n");
		return -EINVAL;
	}
	pon->base = be32_to_cpu(*addr);

	addr = of_get_address(dev->of_node, 1, NULL, NULL);
	if (addr)
		pon->pbs_base = be32_to_cpu(*addr);

	sys_reset = of_property_read_bool(dev->of_node, "qcom,system-reset");
	if (sys_reset && sys_reset_dev) {
		dev_err(dev, "qcom,system-reset property must only be specified for one PMIC PON device in the system\n");
		return -EINVAL;
	}

	modem_reset = of_property_read_bool(dev->of_node, "qcom,modem-reset");
	if (modem_reset && modem_reset_dev) {
		dev_err(dev, "qcom,modem-reset property must only be specified for one PMIC PON device in the system\n");
		return -EINVAL;
	} else if (modem_reset && sys_reset) {
		dev_err(dev, "qcom,modem-reset and qcom,system-reset properties cannot be supported together for one PMIC PON device\n");
		return -EINVAL;
	}

	INIT_LIST_HEAD(&pon->restore_regs);
	mutex_init(&pon->restore_lock);

	/* Get the total number of pon configurations and regulators */
	for_each_available_child_of_node(dev->of_node, node) {
		if (of_find_property(node, "regulator-name", NULL)) {
			pon->num_pon_reg++;
		} else if (of_find_property(node, "qcom,pon-type", NULL)) {
			pon->num_pon_config++;
		} else {
			dev_err(dev, "Unknown sub-node found %s\n", node->name);
			return -EINVAL;
		}
	}
	dev_dbg(dev, "PON@SID%d: num_pon_config=%d, num_pon_reg=%d\n",
		to_spmi_device(dev->parent)->usid, pon->num_pon_config,
		pon->num_pon_reg);

	rc = qpnp_pon_read_hardware_info(pon, sys_reset);
	if (rc)
		return rc;

	rc = pon_regulator_init(pon);
	if (rc)
		return rc;

	rc = qpnp_pon_configure_s3_reset(pon);
	if (rc)
		return rc;

	dev_set_drvdata(dev, pon);

	INIT_DELAYED_WORK(&pon->bark_work, bark_work_func);

	rc = qpnp_pon_parse_dt_power_off_config(pon);
	if (rc)
		return rc;

	rc = of_property_read_u32(dev->of_node, "qcom,pon-dbc-delay", &delay);
	if (!rc) {
		rc = qpnp_pon_set_dbc(pon, delay);
		if (rc)
			return rc;
	} else if (rc != -EINVAL) {
		dev_err(dev, "Unable to read debounce delay, rc=%d\n", rc);
		return rc;
	}

	rc = qpnp_pon_get_dbc(pon, &pon->dbc_time_us);
	if (rc)
		return rc;

	pon->kpdpwr_dbc_enable = of_property_read_bool(dev->of_node,
						"qcom,kpdpwr-sw-debounce");

	pon->store_hard_reset_reason = of_property_read_bool(dev->of_node,
					"qcom,store-hard-reset-reason");

	pon->legacy_hard_reset_offset = of_property_read_bool(pdev->dev.of_node,
					"qcom,use-legacy-hard-reset-offset");

	if (of_property_read_bool(dev->of_node, "qcom,secondary-pon-reset")) {
		if (sys_reset) {
			dev_err(dev, "qcom,system-reset property shouldn't be used along with qcom,secondary-pon-reset property\n");
			return -EINVAL;
		} else if (modem_reset) {
			dev_err(dev, "qcom,modem-reset property shouldn't be used along with qcom,secondary-pon-reset property\n");
			return -EINVAL;
		}
		spin_lock_irqsave(&spon_list_slock, flags);
		list_add(&pon->list, &spon_dev_list);
		spin_unlock_irqrestore(&spon_list_slock, flags);
		pon->is_spon = true;
	}

	/* Register the PON configurations */
	rc = qpnp_pon_config_init(pon, pdev);
	if (rc)
		return rc;

	rc = device_create_file(dev, &dev_attr_debounce_us);
	if (rc) {
		dev_err(dev, "sysfs debounce file creation failed, rc=%d\n",
			rc);
		return rc;
	}

	if (sys_reset)
		sys_reset_dev = pon;
	if (modem_reset)
		modem_reset_dev = pon;

	qpnp_pon_debugfs_init(pon);

	rc = sysfs_create_groups(&pon->dev->kobj, pon_groups);
	if (rc < 0) {
		pr_err("Failed to create sysfs files rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int qpnp_pon_remove(struct platform_device *pdev)
{
	struct qpnp_pon *pon = platform_get_drvdata(pdev);
	unsigned long flags;

	device_remove_file(&pdev->dev, &dev_attr_debounce_us);

	cancel_delayed_work_sync(&pon->bark_work);

	qpnp_pon_debugfs_remove(pon);
	if (pon->is_spon) {
		spin_lock_irqsave(&spon_list_slock, flags);
		list_del(&pon->list);
		spin_unlock_irqrestore(&spon_list_slock, flags);
	}
	mutex_destroy(&pon->restore_lock);

	sysfs_remove_groups(&pon->dev->kobj, pon_groups);
	return 0;
}

#ifdef CONFIG_PM
static int qpnp_pon_restore(struct device *dev)
{
	int i, rc = 0;
	struct qpnp_pon_config *cfg;
	struct qpnp_pon *pon = dev_get_drvdata(dev);
	struct pon_reg *pos = NULL;

	list_for_each_entry(pos, &pon->restore_regs, list) {
		rc = regmap_write(pon->regmap, pos->addr, pos->val);
		if (rc < 0) {
			dev_err(dev, "Failed to restore reg addr=0x%04X rc=%d\n",
				pos->addr, rc);
			return rc;
		}
	}

	for (i = 0; i < pon->num_pon_config; i++) {
		cfg = &pon->pon_cfg[i];
		rc = qpnp_pon_request_irqs(pon, cfg);
		if (rc < 0)
			return rc;
	}

	return rc;
}

static int qpnp_pon_freeze(struct device *dev)
{
	int i, rc = 0;
	struct qpnp_pon_config *cfg;
	struct qpnp_pon *pon = dev_get_drvdata(dev);

	for (i = 0; i < pon->num_pon_config; i++) {
		cfg = &pon->pon_cfg[i];
		rc = qpnp_pon_free_irqs(pon, cfg);
		if (rc < 0)
			return rc;
	}

	return rc;
}

static int qpnp_pon_suspend(struct device *dev)
{
#ifdef CONFIG_DEEPSLEEP
	if (pm_suspend_via_firmware() == PM_SUSPEND_MEM)
		return qpnp_pon_freeze(dev);
#endif

	return 0;
}

static int qpnp_pon_resume(struct device *dev)
{
#ifdef CONFIG_DEEPSLEEP
	if (pm_suspend_via_firmware() == PM_SUSPEND_MEM)
		return qpnp_pon_restore(dev);
#endif

	return 0;
}

static const struct dev_pm_ops qpnp_pon_pm_ops = {
	.freeze = qpnp_pon_freeze,
	.restore = qpnp_pon_restore,
	.suspend = qpnp_pon_suspend,
	.resume = qpnp_pon_resume,
};
#endif

static const struct of_device_id qpnp_pon_match_table[] = {
	{ .compatible = "qcom,qpnp-power-on" },
	{}
};

static struct platform_driver qpnp_pon_driver = {
	.driver = {
		.name = "qcom,qpnp-power-on",
		.of_match_table = qpnp_pon_match_table,
#ifdef CONFIG_PM
		.pm = &qpnp_pon_pm_ops,
#endif
	},
	.probe = qpnp_pon_probe,
	.remove = qpnp_pon_remove,
};

static int __init qpnp_pon_init(void)
{
	return platform_driver_register(&qpnp_pon_driver);
}
subsys_initcall(qpnp_pon_init);

static void __exit qpnp_pon_exit(void)
{
	return platform_driver_unregister(&qpnp_pon_driver);
}
module_exit(qpnp_pon_exit);

MODULE_DESCRIPTION("QPNP PMIC Power-on driver");
MODULE_LICENSE("GPL");
