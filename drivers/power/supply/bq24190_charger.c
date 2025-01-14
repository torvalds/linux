// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the TI bq24190 battery charger.
 *
 * Author: Mark A. Greer <mgreer@animalcreek.com>
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/power_supply.h>
#include <linux/power/bq24190_charger.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/workqueue.h>
#include <linux/i2c.h>
#include <linux/extcon-provider.h>

#define	BQ24190_MANUFACTURER	"Texas Instruments"

#define BQ24190_REG_ISC		0x00 /* Input Source Control */
#define BQ24190_REG_ISC_EN_HIZ_MASK		BIT(7)
#define BQ24190_REG_ISC_EN_HIZ_SHIFT		7
#define BQ24190_REG_ISC_VINDPM_MASK		(BIT(6) | BIT(5) | BIT(4) | \
						 BIT(3))
#define BQ24190_REG_ISC_VINDPM_SHIFT		3
#define BQ24190_REG_ISC_IINLIM_MASK		(BIT(2) | BIT(1) | BIT(0))
#define BQ24190_REG_ISC_IINLIM_SHIFT		0

#define BQ24190_REG_POC		0x01 /* Power-On Configuration */
#define BQ24190_REG_POC_RESET_MASK		BIT(7)
#define BQ24190_REG_POC_RESET_SHIFT		7
#define BQ24190_REG_POC_WDT_RESET_MASK		BIT(6)
#define BQ24190_REG_POC_WDT_RESET_SHIFT		6
#define BQ24190_REG_POC_CHG_CONFIG_MASK		(BIT(5) | BIT(4))
#define BQ24190_REG_POC_CHG_CONFIG_SHIFT	4
#define BQ24190_REG_POC_CHG_CONFIG_DISABLE	0x0
#define BQ24190_REG_POC_CHG_CONFIG_CHARGE	0x1
#define BQ24190_REG_POC_CHG_CONFIG_OTG		0x2
#define BQ24190_REG_POC_CHG_CONFIG_OTG_ALT	0x3
#define BQ24296_REG_POC_OTG_CONFIG_MASK		BIT(5)
#define BQ24296_REG_POC_OTG_CONFIG_SHIFT	5
#define BQ24296_REG_POC_CHG_CONFIG_MASK		BIT(4)
#define BQ24296_REG_POC_CHG_CONFIG_SHIFT	4
#define BQ24296_REG_POC_OTG_CONFIG_DISABLE	0x0
#define BQ24296_REG_POC_OTG_CONFIG_OTG		0x1
#define BQ24190_REG_POC_SYS_MIN_MASK		(BIT(3) | BIT(2) | BIT(1))
#define BQ24190_REG_POC_SYS_MIN_SHIFT		1
#define BQ24190_REG_POC_SYS_MIN_MIN			3000
#define BQ24190_REG_POC_SYS_MIN_MAX			3700
#define BQ24190_REG_POC_BOOST_LIM_MASK		BIT(0)
#define BQ24190_REG_POC_BOOST_LIM_SHIFT		0

#define BQ24190_REG_CCC		0x02 /* Charge Current Control */
#define BQ24190_REG_CCC_ICHG_MASK		(BIT(7) | BIT(6) | BIT(5) | \
						 BIT(4) | BIT(3) | BIT(2))
#define BQ24190_REG_CCC_ICHG_SHIFT		2
#define BQ24190_REG_CCC_FORCE_20PCT_MASK	BIT(0)
#define BQ24190_REG_CCC_FORCE_20PCT_SHIFT	0

#define BQ24190_REG_PCTCC	0x03 /* Pre-charge/Termination Current Cntl */
#define BQ24190_REG_PCTCC_IPRECHG_MASK		(BIT(7) | BIT(6) | BIT(5) | \
						 BIT(4))
#define BQ24190_REG_PCTCC_IPRECHG_SHIFT		4
#define BQ24190_REG_PCTCC_IPRECHG_MIN			128
#define BQ24190_REG_PCTCC_IPRECHG_MAX			2048
#define BQ24190_REG_PCTCC_ITERM_MASK		(BIT(3) | BIT(2) | BIT(1) | \
						 BIT(0))
#define BQ24190_REG_PCTCC_ITERM_SHIFT		0
#define BQ24190_REG_PCTCC_ITERM_MIN			128
#define BQ24190_REG_PCTCC_ITERM_MAX			2048

#define BQ24190_REG_CVC		0x04 /* Charge Voltage Control */
#define BQ24190_REG_CVC_VREG_MASK		(BIT(7) | BIT(6) | BIT(5) | \
						 BIT(4) | BIT(3) | BIT(2))
#define BQ24190_REG_CVC_VREG_SHIFT		2
#define BQ24190_REG_CVC_BATLOWV_MASK		BIT(1)
#define BQ24190_REG_CVC_BATLOWV_SHIFT		1
#define BQ24190_REG_CVC_VRECHG_MASK		BIT(0)
#define BQ24190_REG_CVC_VRECHG_SHIFT		0

#define BQ24190_REG_CTTC	0x05 /* Charge Term/Timer Control */
#define BQ24190_REG_CTTC_EN_TERM_MASK		BIT(7)
#define BQ24190_REG_CTTC_EN_TERM_SHIFT		7
#define BQ24190_REG_CTTC_TERM_STAT_MASK		BIT(6)
#define BQ24190_REG_CTTC_TERM_STAT_SHIFT	6
#define BQ24190_REG_CTTC_WATCHDOG_MASK		(BIT(5) | BIT(4))
#define BQ24190_REG_CTTC_WATCHDOG_SHIFT		4
#define BQ24190_REG_CTTC_EN_TIMER_MASK		BIT(3)
#define BQ24190_REG_CTTC_EN_TIMER_SHIFT		3
#define BQ24190_REG_CTTC_CHG_TIMER_MASK		(BIT(2) | BIT(1))
#define BQ24190_REG_CTTC_CHG_TIMER_SHIFT	1
#define BQ24190_REG_CTTC_JEITA_ISET_MASK	BIT(0)
#define BQ24190_REG_CTTC_JEITA_ISET_SHIFT	0

#define BQ24190_REG_ICTRC	0x06 /* IR Comp/Thermal Regulation Control */
#define BQ24190_REG_ICTRC_BAT_COMP_MASK		(BIT(7) | BIT(6) | BIT(5))
#define BQ24190_REG_ICTRC_BAT_COMP_SHIFT	5
#define BQ24190_REG_ICTRC_VCLAMP_MASK		(BIT(4) | BIT(3) | BIT(2))
#define BQ24190_REG_ICTRC_VCLAMP_SHIFT		2
#define BQ24190_REG_ICTRC_TREG_MASK		(BIT(1) | BIT(0))
#define BQ24190_REG_ICTRC_TREG_SHIFT		0

#define BQ24190_REG_MOC		0x07 /* Misc. Operation Control */
#define BQ24190_REG_MOC_DPDM_EN_MASK		BIT(7)
#define BQ24190_REG_MOC_DPDM_EN_SHIFT		7
#define BQ24190_REG_MOC_TMR2X_EN_MASK		BIT(6)
#define BQ24190_REG_MOC_TMR2X_EN_SHIFT		6
#define BQ24190_REG_MOC_BATFET_DISABLE_MASK	BIT(5)
#define BQ24190_REG_MOC_BATFET_DISABLE_SHIFT	5
#define BQ24190_REG_MOC_JEITA_VSET_MASK		BIT(4)
#define BQ24190_REG_MOC_JEITA_VSET_SHIFT	4
#define BQ24190_REG_MOC_INT_MASK_MASK		(BIT(1) | BIT(0))
#define BQ24190_REG_MOC_INT_MASK_SHIFT		0

#define BQ24190_REG_SS		0x08 /* System Status */
#define BQ24190_REG_SS_VBUS_STAT_MASK		(BIT(7) | BIT(6))
#define BQ24190_REG_SS_VBUS_STAT_SHIFT		6
#define BQ24190_REG_SS_CHRG_STAT_MASK		(BIT(5) | BIT(4))
#define BQ24190_REG_SS_CHRG_STAT_SHIFT		4
#define BQ24190_REG_SS_DPM_STAT_MASK		BIT(3)
#define BQ24190_REG_SS_DPM_STAT_SHIFT		3
#define BQ24190_REG_SS_PG_STAT_MASK		BIT(2)
#define BQ24190_REG_SS_PG_STAT_SHIFT		2
#define BQ24190_REG_SS_THERM_STAT_MASK		BIT(1)
#define BQ24190_REG_SS_THERM_STAT_SHIFT		1
#define BQ24190_REG_SS_VSYS_STAT_MASK		BIT(0)
#define BQ24190_REG_SS_VSYS_STAT_SHIFT		0

#define BQ24190_REG_F		0x09 /* Fault */
#define BQ24190_REG_F_WATCHDOG_FAULT_MASK	BIT(7)
#define BQ24190_REG_F_WATCHDOG_FAULT_SHIFT	7
#define BQ24190_REG_F_BOOST_FAULT_MASK		BIT(6)
#define BQ24190_REG_F_BOOST_FAULT_SHIFT		6
#define BQ24190_REG_F_CHRG_FAULT_MASK		(BIT(5) | BIT(4))
#define BQ24190_REG_F_CHRG_FAULT_SHIFT		4
#define BQ24190_REG_F_BAT_FAULT_MASK		BIT(3)
#define BQ24190_REG_F_BAT_FAULT_SHIFT		3
#define BQ24190_REG_F_NTC_FAULT_MASK		(BIT(2) | BIT(1) | BIT(0))
#define BQ24190_REG_F_NTC_FAULT_SHIFT		0
#define BQ24296_REG_F_NTC_FAULT_MASK		(BIT(1) | BIT(0))
#define BQ24296_REG_F_NTC_FAULT_SHIFT		0

#define BQ24190_REG_VPRS	0x0A /* Vendor/Part/Revision Status */
#define BQ24190_REG_VPRS_PN_MASK		(BIT(5) | BIT(4) | BIT(3))
#define BQ24190_REG_VPRS_PN_SHIFT		3
#define BQ24190_REG_VPRS_PN_24190		0x4
#define BQ24190_REG_VPRS_PN_24192		0x5 /* Also 24193, 24196 */
#define BQ24190_REG_VPRS_PN_24192I		0x3
#define BQ24296_REG_VPRS_PN_MASK		(BIT(7) | BIT(6) | BIT(5))
#define BQ24296_REG_VPRS_PN_SHIFT		5
#define BQ24296_REG_VPRS_PN_24296		0x1
#define BQ24190_REG_VPRS_TS_PROFILE_MASK	BIT(2)
#define BQ24190_REG_VPRS_TS_PROFILE_SHIFT	2
#define BQ24190_REG_VPRS_DEV_REG_MASK		(BIT(1) | BIT(0))
#define BQ24190_REG_VPRS_DEV_REG_SHIFT		0

/*
 * The tables below provide a 2-way mapping for the value that goes in
 * the register field and the real-world value that it represents.
 * The index of the array is the value that goes in the register; the
 * number at that index in the array is the real-world value that it
 * represents.
 */

/* REG00[2:0] (IINLIM) in uAh */
static const int bq24190_isc_iinlim_values[] = {
	 100000,  150000,  500000,  900000, 1200000, 1500000, 2000000, 3000000
};

/* REG02[7:2] (ICHG) in uAh */
static const int bq24190_ccc_ichg_values[] = {
	 512000,  576000,  640000,  704000,  768000,  832000,  896000,  960000,
	1024000, 1088000, 1152000, 1216000, 1280000, 1344000, 1408000, 1472000,
	1536000, 1600000, 1664000, 1728000, 1792000, 1856000, 1920000, 1984000,
	2048000, 2112000, 2176000, 2240000, 2304000, 2368000, 2432000, 2496000,
	2560000, 2624000, 2688000, 2752000, 2816000, 2880000, 2944000, 3008000,
	3072000, 3136000, 3200000, 3264000, 3328000, 3392000, 3456000, 3520000,
	3584000, 3648000, 3712000, 3776000, 3840000, 3904000, 3968000, 4032000,
	4096000, 4160000, 4224000, 4288000, 4352000, 4416000, 4480000, 4544000
};

/* ICHG higher than 3008mA is not supported in BQ24296 */
#define BQ24296_CCC_ICHG_VALUES_LEN	40

/* REG04[7:2] (VREG) in uV */
static const int bq24190_cvc_vreg_values[] = {
	3504000, 3520000, 3536000, 3552000, 3568000, 3584000, 3600000, 3616000,
	3632000, 3648000, 3664000, 3680000, 3696000, 3712000, 3728000, 3744000,
	3760000, 3776000, 3792000, 3808000, 3824000, 3840000, 3856000, 3872000,
	3888000, 3904000, 3920000, 3936000, 3952000, 3968000, 3984000, 4000000,
	4016000, 4032000, 4048000, 4064000, 4080000, 4096000, 4112000, 4128000,
	4144000, 4160000, 4176000, 4192000, 4208000, 4224000, 4240000, 4256000,
	4272000, 4288000, 4304000, 4320000, 4336000, 4352000, 4368000, 4384000,
	4400000
};

/* REG06[1:0] (TREG) in tenths of degrees Celsius */
static const int bq24190_ictrc_treg_values[] = {
	600, 800, 1000, 1200
};

enum bq24190_chip {
	BQ24190,
	BQ24192,
	BQ24192i,
	BQ24196,
	BQ24296,
};

/*
 * The FAULT register is latched by the bq24190 (except for NTC_FAULT)
 * so the first read after a fault returns the latched value and subsequent
 * reads return the current value.  In order to return the fault status
 * to the user, have the interrupt handler save the reg's value and retrieve
 * it in the appropriate health/status routine.
 */
struct bq24190_dev_info {
	struct i2c_client		*client;
	struct device			*dev;
	struct extcon_dev		*edev;
	struct power_supply		*charger;
	struct power_supply		*battery;
	struct delayed_work		input_current_limit_work;
	char				model_name[I2C_NAME_SIZE];
	bool				initialized;
	bool				irq_event;
	bool				otg_vbus_enabled;
	int				charge_type;
	u16				sys_min;
	u16				iprechg;
	u16				iterm;
	u32				ichg;
	u32				ichg_max;
	u32				vreg;
	u32				vreg_max;
	struct mutex			f_reg_lock;
	u8				f_reg;
	u8				ss_reg;
	u8				watchdog;
	const struct bq24190_chip_info	*info;
};

struct bq24190_chip_info {
	int ichg_array_size;
#ifdef CONFIG_REGULATOR
	const struct regulator_desc *vbus_desc;
#endif
	int (*check_chip)(struct bq24190_dev_info *bdi);
	int (*set_chg_config)(struct bq24190_dev_info *bdi, const u8 chg_config);
	int (*set_otg_vbus)(struct bq24190_dev_info *bdi, bool enable);
	u8 ntc_fault_mask;
	int (*get_ntc_status)(const u8 value);
};

static int bq24190_charger_set_charge_type(struct bq24190_dev_info *bdi,
					   const union power_supply_propval *val);

static const unsigned int bq24190_usb_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_NONE,
};


/*
 * Return the index in 'tbl' of greatest value that is less than or equal to
 * 'val'.  The index range returned is 0 to 'tbl_size' - 1.  Assumes that
 * the values in 'tbl' are sorted from smallest to largest and 'tbl_size'
 * is less than 2^8.
 */
static u8 bq24190_find_idx(const int tbl[], int tbl_size, int v)
{
	int i;

	for (i = 1; i < tbl_size; i++)
		if (v < tbl[i])
			break;

	return i - 1;
}

/* Basic driver I/O routines */

static int bq24190_read(struct bq24190_dev_info *bdi, u8 reg, u8 *data)
{
	int ret;

	ret = i2c_smbus_read_byte_data(bdi->client, reg);
	if (ret < 0)
		return ret;

	*data = ret;
	return 0;
}

static int bq24190_write(struct bq24190_dev_info *bdi, u8 reg, u8 data)
{
	return i2c_smbus_write_byte_data(bdi->client, reg, data);
}

static int bq24190_read_mask(struct bq24190_dev_info *bdi, u8 reg,
		u8 mask, u8 shift, u8 *data)
{
	u8 v;
	int ret;

	ret = bq24190_read(bdi, reg, &v);
	if (ret < 0)
		return ret;

	v &= mask;
	v >>= shift;
	*data = v;

	return 0;
}

static int bq24190_write_mask(struct bq24190_dev_info *bdi, u8 reg,
		u8 mask, u8 shift, u8 data)
{
	u8 v;
	int ret;

	ret = bq24190_read(bdi, reg, &v);
	if (ret < 0)
		return ret;

	v &= ~mask;
	v |= ((data << shift) & mask);

	return bq24190_write(bdi, reg, v);
}

static int bq24190_get_field_val(struct bq24190_dev_info *bdi,
		u8 reg, u8 mask, u8 shift,
		const int tbl[], int tbl_size,
		int *val)
{
	u8 v;
	int ret;

	ret = bq24190_read_mask(bdi, reg, mask, shift, &v);
	if (ret < 0)
		return ret;

	v = (v >= tbl_size) ? (tbl_size - 1) : v;
	*val = tbl[v];

	return 0;
}

static int bq24190_set_field_val(struct bq24190_dev_info *bdi,
		u8 reg, u8 mask, u8 shift,
		const int tbl[], int tbl_size,
		int val)
{
	u8 idx;

	idx = bq24190_find_idx(tbl, tbl_size, val);

	return bq24190_write_mask(bdi, reg, mask, shift, idx);
}

#ifdef CONFIG_SYSFS
/*
 * There are a numerous options that are configurable on the bq24190
 * that go well beyond what the power_supply properties provide access to.
 * Provide sysfs access to them so they can be examined and possibly modified
 * on the fly.  They will be provided for the charger power_supply object only
 * and will be prefixed by 'f_' to make them easier to recognize.
 */

#define BQ24190_SYSFS_FIELD(_name, r, f, m, store)			\
{									\
	.attr	= __ATTR(f_##_name, m, bq24190_sysfs_show, store),	\
	.reg	= BQ24190_REG_##r,					\
	.mask	= BQ24190_REG_##r##_##f##_MASK,				\
	.shift	= BQ24190_REG_##r##_##f##_SHIFT,			\
}

#define BQ24190_SYSFS_FIELD_RW(_name, r, f)				\
		BQ24190_SYSFS_FIELD(_name, r, f, S_IWUSR | S_IRUGO,	\
				bq24190_sysfs_store)

#define BQ24190_SYSFS_FIELD_RO(_name, r, f)				\
		BQ24190_SYSFS_FIELD(_name, r, f, S_IRUGO, NULL)

static ssize_t bq24190_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t bq24190_sysfs_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

struct bq24190_sysfs_field_info {
	struct device_attribute	attr;
	u8	reg;
	u8	mask;
	u8	shift;
};

/* On i386 ptrace-abi.h defines SS that breaks the macro calls below. */
#undef SS

static struct bq24190_sysfs_field_info bq24190_sysfs_field_tbl[] = {
			/*	sysfs name	reg	field in reg */
	BQ24190_SYSFS_FIELD_RW(en_hiz,		ISC,	EN_HIZ),
	BQ24190_SYSFS_FIELD_RW(vindpm,		ISC,	VINDPM),
	BQ24190_SYSFS_FIELD_RW(iinlim,		ISC,	IINLIM),
	BQ24190_SYSFS_FIELD_RW(chg_config,	POC,	CHG_CONFIG),
	BQ24190_SYSFS_FIELD_RW(sys_min,		POC,	SYS_MIN),
	BQ24190_SYSFS_FIELD_RW(boost_lim,	POC,	BOOST_LIM),
	BQ24190_SYSFS_FIELD_RW(ichg,		CCC,	ICHG),
	BQ24190_SYSFS_FIELD_RW(force_20_pct,	CCC,	FORCE_20PCT),
	BQ24190_SYSFS_FIELD_RW(iprechg,		PCTCC,	IPRECHG),
	BQ24190_SYSFS_FIELD_RW(iterm,		PCTCC,	ITERM),
	BQ24190_SYSFS_FIELD_RW(vreg,		CVC,	VREG),
	BQ24190_SYSFS_FIELD_RW(batlowv,		CVC,	BATLOWV),
	BQ24190_SYSFS_FIELD_RW(vrechg,		CVC,	VRECHG),
	BQ24190_SYSFS_FIELD_RW(en_term,		CTTC,	EN_TERM),
	BQ24190_SYSFS_FIELD_RW(term_stat,	CTTC,	TERM_STAT),
	BQ24190_SYSFS_FIELD_RO(watchdog,	CTTC,	WATCHDOG),
	BQ24190_SYSFS_FIELD_RW(en_timer,	CTTC,	EN_TIMER),
	BQ24190_SYSFS_FIELD_RW(chg_timer,	CTTC,	CHG_TIMER),
	BQ24190_SYSFS_FIELD_RW(jeta_iset,	CTTC,	JEITA_ISET),
	BQ24190_SYSFS_FIELD_RW(bat_comp,	ICTRC,	BAT_COMP),
	BQ24190_SYSFS_FIELD_RW(vclamp,		ICTRC,	VCLAMP),
	BQ24190_SYSFS_FIELD_RW(treg,		ICTRC,	TREG),
	BQ24190_SYSFS_FIELD_RW(dpdm_en,		MOC,	DPDM_EN),
	BQ24190_SYSFS_FIELD_RW(tmr2x_en,	MOC,	TMR2X_EN),
	BQ24190_SYSFS_FIELD_RW(batfet_disable,	MOC,	BATFET_DISABLE),
	BQ24190_SYSFS_FIELD_RW(jeita_vset,	MOC,	JEITA_VSET),
	BQ24190_SYSFS_FIELD_RO(int_mask,	MOC,	INT_MASK),
	BQ24190_SYSFS_FIELD_RO(vbus_stat,	SS,	VBUS_STAT),
	BQ24190_SYSFS_FIELD_RO(chrg_stat,	SS,	CHRG_STAT),
	BQ24190_SYSFS_FIELD_RO(dpm_stat,	SS,	DPM_STAT),
	BQ24190_SYSFS_FIELD_RO(pg_stat,		SS,	PG_STAT),
	BQ24190_SYSFS_FIELD_RO(therm_stat,	SS,	THERM_STAT),
	BQ24190_SYSFS_FIELD_RO(vsys_stat,	SS,	VSYS_STAT),
	BQ24190_SYSFS_FIELD_RO(watchdog_fault,	F,	WATCHDOG_FAULT),
	BQ24190_SYSFS_FIELD_RO(boost_fault,	F,	BOOST_FAULT),
	BQ24190_SYSFS_FIELD_RO(chrg_fault,	F,	CHRG_FAULT),
	BQ24190_SYSFS_FIELD_RO(bat_fault,	F,	BAT_FAULT),
	BQ24190_SYSFS_FIELD_RO(ntc_fault,	F,	NTC_FAULT),
	BQ24190_SYSFS_FIELD_RO(pn,		VPRS,	PN),
	BQ24190_SYSFS_FIELD_RO(ts_profile,	VPRS,	TS_PROFILE),
	BQ24190_SYSFS_FIELD_RO(dev_reg,		VPRS,	DEV_REG),
};

static struct attribute *
	bq24190_sysfs_attrs[ARRAY_SIZE(bq24190_sysfs_field_tbl) + 1];

ATTRIBUTE_GROUPS(bq24190_sysfs);

static void bq24190_sysfs_init_attrs(void)
{
	int i, limit = ARRAY_SIZE(bq24190_sysfs_field_tbl);

	for (i = 0; i < limit; i++)
		bq24190_sysfs_attrs[i] = &bq24190_sysfs_field_tbl[i].attr.attr;

	bq24190_sysfs_attrs[limit] = NULL; /* Has additional entry for this */
}

static struct bq24190_sysfs_field_info *bq24190_sysfs_field_lookup(
		const char *name)
{
	int i, limit = ARRAY_SIZE(bq24190_sysfs_field_tbl);

	for (i = 0; i < limit; i++)
		if (!strcmp(name, bq24190_sysfs_field_tbl[i].attr.attr.name))
			break;

	if (i >= limit)
		return NULL;

	return &bq24190_sysfs_field_tbl[i];
}

static ssize_t bq24190_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct bq24190_dev_info *bdi = power_supply_get_drvdata(psy);
	struct bq24190_sysfs_field_info *info;
	ssize_t count;
	int ret;
	u8 v;

	info = bq24190_sysfs_field_lookup(attr->attr.name);
	if (!info)
		return -EINVAL;

	ret = pm_runtime_resume_and_get(bdi->dev);
	if (ret < 0)
		return ret;

	ret = bq24190_read_mask(bdi, info->reg, info->mask, info->shift, &v);
	if (ret)
		count = ret;
	else
		count = sysfs_emit(buf, "%hhx\n", v);

	pm_runtime_mark_last_busy(bdi->dev);
	pm_runtime_put_autosuspend(bdi->dev);

	return count;
}

static ssize_t bq24190_sysfs_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct bq24190_dev_info *bdi = power_supply_get_drvdata(psy);
	struct bq24190_sysfs_field_info *info;
	int ret;
	u8 v;

	info = bq24190_sysfs_field_lookup(attr->attr.name);
	if (!info)
		return -EINVAL;

	ret = kstrtou8(buf, 0, &v);
	if (ret < 0)
		return ret;

	ret = pm_runtime_resume_and_get(bdi->dev);
	if (ret < 0)
		return ret;

	ret = bq24190_write_mask(bdi, info->reg, info->mask, info->shift, v);
	if (ret)
		count = ret;

	pm_runtime_mark_last_busy(bdi->dev);
	pm_runtime_put_autosuspend(bdi->dev);

	return count;
}
#endif

static int bq24190_set_otg_vbus(struct bq24190_dev_info *bdi, bool enable)
{
	union power_supply_propval val = { .intval = bdi->charge_type };
	int ret;

	ret = pm_runtime_resume_and_get(bdi->dev);
	if (ret < 0) {
		dev_warn(bdi->dev, "pm_runtime_get failed: %i\n", ret);
		return ret;
	}

	bdi->otg_vbus_enabled = enable;
	if (enable)
		ret = bq24190_write_mask(bdi, BQ24190_REG_POC,
					 BQ24190_REG_POC_CHG_CONFIG_MASK,
					 BQ24190_REG_POC_CHG_CONFIG_SHIFT,
					 BQ24190_REG_POC_CHG_CONFIG_OTG);
	else
		ret = bq24190_charger_set_charge_type(bdi, &val);

	pm_runtime_mark_last_busy(bdi->dev);
	pm_runtime_put_autosuspend(bdi->dev);

	return ret;
}

static int bq24296_set_otg_vbus(struct bq24190_dev_info *bdi, bool enable)
{
	union power_supply_propval val = { .intval = bdi->charge_type };
	int ret;

	ret = pm_runtime_resume_and_get(bdi->dev);
	if (ret < 0) {
		dev_warn(bdi->dev, "pm_runtime_get failed: %i\n", ret);
		return ret;
	}

	bdi->otg_vbus_enabled = enable;
	if (enable) {
		ret = bq24190_write_mask(bdi, BQ24190_REG_POC,
					 BQ24296_REG_POC_CHG_CONFIG_MASK,
					 BQ24296_REG_POC_CHG_CONFIG_SHIFT,
					 BQ24190_REG_POC_CHG_CONFIG_DISABLE);

		if (ret < 0)
			goto out;

		ret = bq24190_write_mask(bdi, BQ24190_REG_POC,
					 BQ24296_REG_POC_OTG_CONFIG_MASK,
					 BQ24296_REG_POC_OTG_CONFIG_SHIFT,
					 BQ24296_REG_POC_OTG_CONFIG_OTG);
	} else {
		ret = bq24190_write_mask(bdi, BQ24190_REG_POC,
					 BQ24296_REG_POC_OTG_CONFIG_MASK,
					 BQ24296_REG_POC_OTG_CONFIG_SHIFT,
					 BQ24296_REG_POC_OTG_CONFIG_DISABLE);
		if (ret < 0)
			goto out;

		ret = bq24190_charger_set_charge_type(bdi, &val);
	}

out:
	pm_runtime_mark_last_busy(bdi->dev);
	pm_runtime_put_autosuspend(bdi->dev);

	return ret;
}

#ifdef CONFIG_REGULATOR
static int bq24190_vbus_enable(struct regulator_dev *dev)
{
	return bq24190_set_otg_vbus(rdev_get_drvdata(dev), true);
}

static int bq24190_vbus_disable(struct regulator_dev *dev)
{
	return bq24190_set_otg_vbus(rdev_get_drvdata(dev), false);
}

static int bq24190_vbus_is_enabled(struct regulator_dev *dev)
{
	struct bq24190_dev_info *bdi = rdev_get_drvdata(dev);
	int ret;
	u8 val;

	ret = pm_runtime_resume_and_get(bdi->dev);
	if (ret < 0) {
		dev_warn(bdi->dev, "pm_runtime_get failed: %i\n", ret);
		return ret;
	}

	ret = bq24190_read_mask(bdi, BQ24190_REG_POC,
				BQ24190_REG_POC_CHG_CONFIG_MASK,
				BQ24190_REG_POC_CHG_CONFIG_SHIFT, &val);

	pm_runtime_mark_last_busy(bdi->dev);
	pm_runtime_put_autosuspend(bdi->dev);

	if (ret)
		return ret;

	bdi->otg_vbus_enabled = (val == BQ24190_REG_POC_CHG_CONFIG_OTG ||
				 val == BQ24190_REG_POC_CHG_CONFIG_OTG_ALT);
	return bdi->otg_vbus_enabled;
}

static int bq24296_vbus_enable(struct regulator_dev *dev)
{
	return bq24296_set_otg_vbus(rdev_get_drvdata(dev), true);
}

static int bq24296_vbus_disable(struct regulator_dev *dev)
{
	return bq24296_set_otg_vbus(rdev_get_drvdata(dev), false);
}

static int bq24296_vbus_is_enabled(struct regulator_dev *dev)
{
	struct bq24190_dev_info *bdi = rdev_get_drvdata(dev);
	int ret;
	u8 val;

	ret = pm_runtime_resume_and_get(bdi->dev);
	if (ret < 0) {
		dev_warn(bdi->dev, "pm_runtime_get failed: %i\n", ret);
		return ret;
	}

	ret = bq24190_read_mask(bdi, BQ24190_REG_POC,
				BQ24296_REG_POC_OTG_CONFIG_MASK,
				BQ24296_REG_POC_OTG_CONFIG_SHIFT, &val);

	pm_runtime_mark_last_busy(bdi->dev);
	pm_runtime_put_autosuspend(bdi->dev);

	if (ret)
		return ret;

	bdi->otg_vbus_enabled = (val == BQ24296_REG_POC_OTG_CONFIG_OTG);

	return bdi->otg_vbus_enabled;
}

static const struct regulator_ops bq24190_vbus_ops = {
	.enable = bq24190_vbus_enable,
	.disable = bq24190_vbus_disable,
	.is_enabled = bq24190_vbus_is_enabled,
};

static const struct regulator_desc bq24190_vbus_desc = {
	.name = "usb_otg_vbus",
	.of_match = "usb-otg-vbus",
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.ops = &bq24190_vbus_ops,
	.fixed_uV = 5000000,
	.n_voltages = 1,
};

static const struct regulator_ops bq24296_vbus_ops = {
	.enable = bq24296_vbus_enable,
	.disable = bq24296_vbus_disable,
	.is_enabled = bq24296_vbus_is_enabled,
};

static const struct regulator_desc bq24296_vbus_desc = {
	.name = "usb_otg_vbus",
	.of_match = "usb-otg-vbus",
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.ops = &bq24296_vbus_ops,
	.fixed_uV = 5000000,
	.n_voltages = 1,
};

static const struct regulator_init_data bq24190_vbus_init_data = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
};

static int bq24190_register_vbus_regulator(struct bq24190_dev_info *bdi)
{
	struct bq24190_platform_data *pdata = bdi->dev->platform_data;
	struct regulator_config cfg = { };
	struct regulator_dev *reg;
	int ret = 0;

	cfg.dev = bdi->dev;
	if (pdata && pdata->regulator_init_data)
		cfg.init_data = pdata->regulator_init_data;
	else
		cfg.init_data = &bq24190_vbus_init_data;
	cfg.driver_data = bdi;
	reg = devm_regulator_register(bdi->dev, bdi->info->vbus_desc, &cfg);
	if (IS_ERR(reg)) {
		ret = PTR_ERR(reg);
		dev_err(bdi->dev, "Can't register regulator: %d\n", ret);
	}

	return ret;
}
#else
static int bq24190_register_vbus_regulator(struct bq24190_dev_info *bdi)
{
	return 0;
}
#endif

static int bq24190_set_config(struct bq24190_dev_info *bdi)
{
	int ret;
	u8 v;

	ret = bq24190_read(bdi, BQ24190_REG_CTTC, &v);
	if (ret < 0)
		return ret;

	bdi->watchdog = ((v & BQ24190_REG_CTTC_WATCHDOG_MASK) >>
					BQ24190_REG_CTTC_WATCHDOG_SHIFT);

	/*
	 * According to the "Host Mode and default Mode" section of the
	 * manual, a write to any register causes the bq24190 to switch
	 * from default mode to host mode.  It will switch back to default
	 * mode after a WDT timeout unless the WDT is turned off as well.
	 * So, by simply turning off the WDT, we accomplish both with the
	 * same write.
	 */
	v &= ~BQ24190_REG_CTTC_WATCHDOG_MASK;

	ret = bq24190_write(bdi, BQ24190_REG_CTTC, v);
	if (ret < 0)
		return ret;

	if (bdi->sys_min) {
		v = bdi->sys_min / 100 - 30; // manual section 9.5.1.2, table 9
		ret = bq24190_write_mask(bdi, BQ24190_REG_POC,
					 BQ24190_REG_POC_SYS_MIN_MASK,
					 BQ24190_REG_POC_SYS_MIN_SHIFT,
					 v);
		if (ret < 0)
			return ret;
	}

	if (bdi->iprechg) {
		v = bdi->iprechg / 128 - 1; // manual section 9.5.1.4, table 11
		ret = bq24190_write_mask(bdi, BQ24190_REG_PCTCC,
					 BQ24190_REG_PCTCC_IPRECHG_MASK,
					 BQ24190_REG_PCTCC_IPRECHG_SHIFT,
					 v);
		if (ret < 0)
			return ret;
	}

	if (bdi->iterm) {
		v = bdi->iterm / 128 - 1; // manual section 9.5.1.4, table 11
		ret = bq24190_write_mask(bdi, BQ24190_REG_PCTCC,
					 BQ24190_REG_PCTCC_ITERM_MASK,
					 BQ24190_REG_PCTCC_ITERM_SHIFT,
					 v);
		if (ret < 0)
			return ret;
	}

	if (bdi->ichg) {
		ret = bq24190_set_field_val(bdi, BQ24190_REG_CCC,
					    BQ24190_REG_CCC_ICHG_MASK,
					    BQ24190_REG_CCC_ICHG_SHIFT,
					    bq24190_ccc_ichg_values,
					    bdi->info->ichg_array_size,
					    bdi->ichg);
		if (ret < 0)
			return ret;
	}

	if (bdi->vreg) {
		ret = bq24190_set_field_val(bdi, BQ24190_REG_CVC,
					    BQ24190_REG_CVC_VREG_MASK,
					    BQ24190_REG_CVC_VREG_SHIFT,
					    bq24190_cvc_vreg_values,
					    ARRAY_SIZE(bq24190_cvc_vreg_values),
					    bdi->vreg);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int bq24190_register_reset(struct bq24190_dev_info *bdi)
{
	int ret, limit = 100;
	u8 v;

	/*
	 * This prop. can be passed on device instantiation from platform code:
	 * struct property_entry pe[] =
	 *   { PROPERTY_ENTRY_BOOL("disable-reset"), ... };
	 * struct i2c_board_info bi =
	 *   { .type = "bq24190", .addr = 0x6b, .properties = pe, .irq = irq };
	 * struct i2c_adapter ad = { ... };
	 * i2c_add_adapter(&ad);
	 * i2c_new_client_device(&ad, &bi);
	 */
	if (device_property_read_bool(bdi->dev, "disable-reset"))
		return 0;

	/* Reset the registers */
	ret = bq24190_write_mask(bdi, BQ24190_REG_POC,
			BQ24190_REG_POC_RESET_MASK,
			BQ24190_REG_POC_RESET_SHIFT,
			0x1);
	if (ret < 0)
		return ret;

	/* Reset bit will be cleared by hardware so poll until it is */
	do {
		ret = bq24190_read_mask(bdi, BQ24190_REG_POC,
				BQ24190_REG_POC_RESET_MASK,
				BQ24190_REG_POC_RESET_SHIFT,
				&v);
		if (ret < 0)
			return ret;

		if (v == 0)
			return 0;

		usleep_range(100, 200);
	} while (--limit);

	return -EIO;
}

/* Charger power supply property routines */

static int bq24190_charger_get_charge_type(struct bq24190_dev_info *bdi,
		union power_supply_propval *val)
{
	u8 v;
	int type, ret;

	ret = bq24190_read_mask(bdi, BQ24190_REG_POC,
			BQ24190_REG_POC_CHG_CONFIG_MASK,
			BQ24190_REG_POC_CHG_CONFIG_SHIFT,
			&v);
	if (ret < 0)
		return ret;

	/* If POC[CHG_CONFIG] (REG01[5:4]) == 0, charge is disabled */
	if (!v) {
		type = POWER_SUPPLY_CHARGE_TYPE_NONE;
	} else {
		ret = bq24190_read_mask(bdi, BQ24190_REG_CCC,
				BQ24190_REG_CCC_FORCE_20PCT_MASK,
				BQ24190_REG_CCC_FORCE_20PCT_SHIFT,
				&v);
		if (ret < 0)
			return ret;

		type = (v) ? POWER_SUPPLY_CHARGE_TYPE_TRICKLE :
			     POWER_SUPPLY_CHARGE_TYPE_FAST;
	}

	val->intval = type;

	return 0;
}

static int bq24190_battery_set_chg_config(struct bq24190_dev_info *bdi,
		const u8 chg_config)
{
	return bq24190_write_mask(bdi, BQ24190_REG_POC,
			BQ24190_REG_POC_CHG_CONFIG_MASK,
			BQ24190_REG_POC_CHG_CONFIG_SHIFT,
			chg_config);
}

static int bq24296_battery_set_chg_config(struct bq24190_dev_info *bdi,
		const u8 chg_config)
{
	return bq24190_write_mask(bdi, BQ24190_REG_POC,
			BQ24296_REG_POC_CHG_CONFIG_MASK,
			BQ24296_REG_POC_CHG_CONFIG_SHIFT,
			chg_config);
}

static int bq24190_charger_set_charge_type(struct bq24190_dev_info *bdi,
		const union power_supply_propval *val)
{
	u8 chg_config, force_20pct, en_term;
	int ret;

	/*
	 * According to the "Termination when REG02[0] = 1" section of
	 * the bq24190 manual, the trickle charge could be less than the
	 * termination current so it recommends turning off the termination
	 * function.
	 *
	 * Note: AFAICT from the datasheet, the user will have to manually
	 * turn off the charging when in 20% mode.  If its not turned off,
	 * there could be battery damage.  So, use this mode at your own risk.
	 */
	switch (val->intval) {
	case POWER_SUPPLY_CHARGE_TYPE_NONE:
		chg_config = 0x0;
		break;
	case POWER_SUPPLY_CHARGE_TYPE_TRICKLE:
		chg_config = 0x1;
		force_20pct = 0x1;
		en_term = 0x0;
		break;
	case POWER_SUPPLY_CHARGE_TYPE_FAST:
		chg_config = 0x1;
		force_20pct = 0x0;
		en_term = 0x1;
		break;
	default:
		return -EINVAL;
	}

	bdi->charge_type = val->intval;
	/*
	 * If the 5V Vbus boost regulator is enabled delay setting
	 * the charge-type until its gets disabled.
	 */
	if (bdi->otg_vbus_enabled)
		return 0;

	if (chg_config) { /* Enabling the charger */
		ret = bq24190_write_mask(bdi, BQ24190_REG_CCC,
				BQ24190_REG_CCC_FORCE_20PCT_MASK,
				BQ24190_REG_CCC_FORCE_20PCT_SHIFT,
				force_20pct);
		if (ret < 0)
			return ret;

		ret = bq24190_write_mask(bdi, BQ24190_REG_CTTC,
				BQ24190_REG_CTTC_EN_TERM_MASK,
				BQ24190_REG_CTTC_EN_TERM_SHIFT,
				en_term);
		if (ret < 0)
			return ret;
	}

	return bdi->info->set_chg_config(bdi, chg_config);
}

static int bq24190_charger_get_ntc_status(u8 value)
{
	int health;

	switch (value >> BQ24190_REG_F_NTC_FAULT_SHIFT & 0x7) {
	case 0x1: /* TS1  Cold */
	case 0x3: /* TS2  Cold */
	case 0x5: /* Both Cold */
		health = POWER_SUPPLY_HEALTH_COLD;
		break;
	case 0x2: /* TS1  Hot */
	case 0x4: /* TS2  Hot */
	case 0x6: /* Both Hot */
		health = POWER_SUPPLY_HEALTH_OVERHEAT;
		break;
	default:
		health = POWER_SUPPLY_HEALTH_UNKNOWN;
	}

	return health;
}

static int bq24296_charger_get_ntc_status(u8 value)
{
	int health;

	switch (value >> BQ24296_REG_F_NTC_FAULT_SHIFT & 0x3) {
	case 0x0: /* Normal */
		health = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case 0x1: /* Hot */
		health = POWER_SUPPLY_HEALTH_OVERHEAT;
		break;
	case 0x2: /* Cold */
		health = POWER_SUPPLY_HEALTH_COLD;
		break;
	default:
		health = POWER_SUPPLY_HEALTH_UNKNOWN;
	}

	return health;
}

static int bq24190_charger_get_health(struct bq24190_dev_info *bdi,
		union power_supply_propval *val)
{
	u8 v;
	int health;

	mutex_lock(&bdi->f_reg_lock);
	v = bdi->f_reg;
	mutex_unlock(&bdi->f_reg_lock);

	if (v & bdi->info->ntc_fault_mask) {
		health = bdi->info->get_ntc_status(v);
	} else if (v & BQ24190_REG_F_BAT_FAULT_MASK) {
		health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	} else if (v & BQ24190_REG_F_CHRG_FAULT_MASK) {
		switch (v >> BQ24190_REG_F_CHRG_FAULT_SHIFT & 0x3) {
		case 0x1: /* Input Fault (VBUS OVP or VBAT<VBUS<3.8V) */
			/*
			 * This could be over-voltage or under-voltage
			 * and there's no way to tell which.  Instead
			 * of looking foolish and returning 'OVERVOLTAGE'
			 * when its really under-voltage, just return
			 * 'UNSPEC_FAILURE'.
			 */
			health = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
			break;
		case 0x2: /* Thermal Shutdown */
			health = POWER_SUPPLY_HEALTH_OVERHEAT;
			break;
		case 0x3: /* Charge Safety Timer Expiration */
			health = POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE;
			break;
		default:  /* prevent compiler warning */
			health = -1;
		}
	} else if (v & BQ24190_REG_F_BOOST_FAULT_MASK) {
		/*
		 * This could be over-current or over-voltage but there's
		 * no way to tell which.  Return 'OVERVOLTAGE' since there
		 * isn't an 'OVERCURRENT' value defined that we can return
		 * even if it was over-current.
		 */
		health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	} else {
		health = POWER_SUPPLY_HEALTH_GOOD;
	}

	val->intval = health;

	return 0;
}

static int bq24190_charger_get_online(struct bq24190_dev_info *bdi,
		union power_supply_propval *val)
{
	u8 pg_stat, batfet_disable;
	int ret;

	ret = bq24190_read_mask(bdi, BQ24190_REG_SS,
			BQ24190_REG_SS_PG_STAT_MASK,
			BQ24190_REG_SS_PG_STAT_SHIFT, &pg_stat);
	if (ret < 0)
		return ret;

	ret = bq24190_read_mask(bdi, BQ24190_REG_MOC,
			BQ24190_REG_MOC_BATFET_DISABLE_MASK,
			BQ24190_REG_MOC_BATFET_DISABLE_SHIFT, &batfet_disable);
	if (ret < 0)
		return ret;

	val->intval = pg_stat && !batfet_disable;

	return 0;
}

static int bq24190_battery_set_online(struct bq24190_dev_info *bdi,
				      const union power_supply_propval *val);
static int bq24190_battery_get_status(struct bq24190_dev_info *bdi,
				      union power_supply_propval *val);
static int bq24190_battery_get_temp_alert_max(struct bq24190_dev_info *bdi,
					      union power_supply_propval *val);
static int bq24190_battery_set_temp_alert_max(struct bq24190_dev_info *bdi,
					      const union power_supply_propval *val);

static int bq24190_charger_set_online(struct bq24190_dev_info *bdi,
				      const union power_supply_propval *val)
{
	return bq24190_battery_set_online(bdi, val);
}

static int bq24190_charger_get_status(struct bq24190_dev_info *bdi,
				      union power_supply_propval *val)
{
	return bq24190_battery_get_status(bdi, val);
}

static int bq24190_charger_get_temp_alert_max(struct bq24190_dev_info *bdi,
					      union power_supply_propval *val)
{
	return bq24190_battery_get_temp_alert_max(bdi, val);
}

static int bq24190_charger_set_temp_alert_max(struct bq24190_dev_info *bdi,
					      const union power_supply_propval *val)
{
	return bq24190_battery_set_temp_alert_max(bdi, val);
}

static int bq24190_charger_get_precharge(struct bq24190_dev_info *bdi,
		union power_supply_propval *val)
{
	u8 v;
	int curr, ret;

	ret = bq24190_read_mask(bdi, BQ24190_REG_PCTCC,
			BQ24190_REG_PCTCC_IPRECHG_MASK,
			BQ24190_REG_PCTCC_IPRECHG_SHIFT, &v);
	if (ret < 0)
		return ret;

	curr = ++v * 128 * 1000;

	ret = bq24190_read_mask(bdi, BQ24190_REG_CCC,
			BQ24190_REG_CCC_FORCE_20PCT_MASK,
			BQ24190_REG_CCC_FORCE_20PCT_SHIFT, &v);
	if (ret < 0)
		return ret;

	/* If FORCE_20PCT is enabled, then current is 50% of IPRECHG value */
	if (v)
		curr /= 2;

	val->intval = curr;

	return 0;
}

static int bq24190_charger_get_charge_term(struct bq24190_dev_info *bdi,
		union power_supply_propval *val)
{
	u8 v;
	int ret;

	ret = bq24190_read_mask(bdi, BQ24190_REG_PCTCC,
			BQ24190_REG_PCTCC_ITERM_MASK,
			BQ24190_REG_PCTCC_ITERM_SHIFT, &v);
	if (ret < 0)
		return ret;

	val->intval = ++v * 128 * 1000;
	return 0;
}

static int bq24190_charger_get_current(struct bq24190_dev_info *bdi,
		union power_supply_propval *val)
{
	u8 v;
	int curr, ret;

	ret = bq24190_get_field_val(bdi, BQ24190_REG_CCC,
			BQ24190_REG_CCC_ICHG_MASK, BQ24190_REG_CCC_ICHG_SHIFT,
			bq24190_ccc_ichg_values,
			bdi->info->ichg_array_size, &curr);
	if (ret < 0)
		return ret;

	ret = bq24190_read_mask(bdi, BQ24190_REG_CCC,
			BQ24190_REG_CCC_FORCE_20PCT_MASK,
			BQ24190_REG_CCC_FORCE_20PCT_SHIFT, &v);
	if (ret < 0)
		return ret;

	/* If FORCE_20PCT is enabled, then current is 20% of ICHG value */
	if (v)
		curr /= 5;

	val->intval = curr;
	return 0;
}

static int bq24190_charger_set_current(struct bq24190_dev_info *bdi,
		const union power_supply_propval *val)
{
	u8 v;
	int ret, curr = val->intval;

	ret = bq24190_read_mask(bdi, BQ24190_REG_CCC,
			BQ24190_REG_CCC_FORCE_20PCT_MASK,
			BQ24190_REG_CCC_FORCE_20PCT_SHIFT, &v);
	if (ret < 0)
		return ret;

	/* If FORCE_20PCT is enabled, have to multiply value passed in by 5 */
	if (v)
		curr *= 5;

	if (curr > bdi->ichg_max)
		return -EINVAL;

	ret = bq24190_set_field_val(bdi, BQ24190_REG_CCC,
			BQ24190_REG_CCC_ICHG_MASK, BQ24190_REG_CCC_ICHG_SHIFT,
			bq24190_ccc_ichg_values,
			bdi->info->ichg_array_size, curr);
	if (ret < 0)
		return ret;

	bdi->ichg = curr;

	return 0;
}

static int bq24190_charger_get_voltage(struct bq24190_dev_info *bdi,
		union power_supply_propval *val)
{
	int voltage, ret;

	ret = bq24190_get_field_val(bdi, BQ24190_REG_CVC,
			BQ24190_REG_CVC_VREG_MASK, BQ24190_REG_CVC_VREG_SHIFT,
			bq24190_cvc_vreg_values,
			ARRAY_SIZE(bq24190_cvc_vreg_values), &voltage);
	if (ret < 0)
		return ret;

	val->intval = voltage;
	return 0;
}

static int bq24190_charger_set_voltage(struct bq24190_dev_info *bdi,
		const union power_supply_propval *val)
{
	int ret;

	if (val->intval > bdi->vreg_max)
		return -EINVAL;

	ret = bq24190_set_field_val(bdi, BQ24190_REG_CVC,
			BQ24190_REG_CVC_VREG_MASK, BQ24190_REG_CVC_VREG_SHIFT,
			bq24190_cvc_vreg_values,
			ARRAY_SIZE(bq24190_cvc_vreg_values), val->intval);
	if (ret < 0)
		return ret;

	bdi->vreg = val->intval;

	return 0;
}

static int bq24190_charger_get_iinlimit(struct bq24190_dev_info *bdi,
		union power_supply_propval *val)
{
	int iinlimit, ret;

	ret = bq24190_get_field_val(bdi, BQ24190_REG_ISC,
			BQ24190_REG_ISC_IINLIM_MASK,
			BQ24190_REG_ISC_IINLIM_SHIFT,
			bq24190_isc_iinlim_values,
			ARRAY_SIZE(bq24190_isc_iinlim_values), &iinlimit);
	if (ret < 0)
		return ret;

	val->intval = iinlimit;
	return 0;
}

static int bq24190_charger_set_iinlimit(struct bq24190_dev_info *bdi,
		const union power_supply_propval *val)
{
	return bq24190_set_field_val(bdi, BQ24190_REG_ISC,
			BQ24190_REG_ISC_IINLIM_MASK,
			BQ24190_REG_ISC_IINLIM_SHIFT,
			bq24190_isc_iinlim_values,
			ARRAY_SIZE(bq24190_isc_iinlim_values), val->intval);
}

static int bq24190_charger_get_property(struct power_supply *psy,
		enum power_supply_property psp, union power_supply_propval *val)
{
	struct bq24190_dev_info *bdi = power_supply_get_drvdata(psy);
	int ret;

	dev_dbg(bdi->dev, "prop: %d\n", psp);

	ret = pm_runtime_resume_and_get(bdi->dev);
	if (ret < 0)
		return ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		ret = bq24190_charger_get_charge_type(bdi, val);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		ret = bq24190_charger_get_health(bdi, val);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		ret = bq24190_charger_get_online(bdi, val);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		ret = bq24190_charger_get_status(bdi, val);
		break;
	case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
		ret =  bq24190_charger_get_temp_alert_max(bdi, val);
		break;
	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
		ret = bq24190_charger_get_precharge(bdi, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		ret = bq24190_charger_get_charge_term(bdi, val);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = bq24190_charger_get_current(bdi, val);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = bdi->ichg_max;
		ret = 0;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = bq24190_charger_get_voltage(bdi, val);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		val->intval = bdi->vreg_max;
		ret = 0;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = bq24190_charger_get_iinlimit(bdi, val);
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_SYSTEM;
		ret = 0;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = bdi->model_name;
		ret = 0;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = BQ24190_MANUFACTURER;
		ret = 0;
		break;
	default:
		ret = -ENODATA;
	}

	pm_runtime_mark_last_busy(bdi->dev);
	pm_runtime_put_autosuspend(bdi->dev);

	return ret;
}

static int bq24190_charger_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct bq24190_dev_info *bdi = power_supply_get_drvdata(psy);
	int ret;

	dev_dbg(bdi->dev, "prop: %d\n", psp);

	ret = pm_runtime_resume_and_get(bdi->dev);
	if (ret < 0)
		return ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = bq24190_charger_set_online(bdi, val);
		break;
	case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
		ret = bq24190_charger_set_temp_alert_max(bdi, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		ret = bq24190_charger_set_charge_type(bdi, val);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = bq24190_charger_set_current(bdi, val);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = bq24190_charger_set_voltage(bdi, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = bq24190_charger_set_iinlimit(bdi, val);
		break;
	default:
		ret = -EINVAL;
	}

	pm_runtime_mark_last_busy(bdi->dev);
	pm_runtime_put_autosuspend(bdi->dev);

	return ret;
}

static int bq24190_charger_property_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return 1;
	default:
		return 0;
	}
}

static void bq24190_input_current_limit_work(struct work_struct *work)
{
	struct bq24190_dev_info *bdi =
		container_of(work, struct bq24190_dev_info,
			     input_current_limit_work.work);
	union power_supply_propval val;
	int ret;

	ret = power_supply_get_property_from_supplier(bdi->charger,
						      POWER_SUPPLY_PROP_CURRENT_MAX,
						      &val);
	if (ret)
		return;

	bq24190_charger_set_property(bdi->charger,
				     POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
				     &val);
	power_supply_changed(bdi->charger);
}

/* Sync the input-current-limit with our parent supply (if we have one) */
static void bq24190_charger_external_power_changed(struct power_supply *psy)
{
	struct bq24190_dev_info *bdi = power_supply_get_drvdata(psy);

	/*
	 * The Power-Good detection may take up to 220ms, sometimes
	 * the external charger detection is quicker, and the bq24190 will
	 * reset to iinlim based on its own charger detection (which is not
	 * hooked up when using external charger detection) resulting in a
	 * too low default 500mA iinlim. Delay setting the input-current-limit
	 * for 300ms to avoid this.
	 */
	queue_delayed_work(system_wq, &bdi->input_current_limit_work,
			   msecs_to_jiffies(300));
}

static enum power_supply_property bq24190_charger_properties[] = {
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TEMP_ALERT_MAX,
	POWER_SUPPLY_PROP_PRECHARGE_CURRENT,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static char *bq24190_charger_supplied_to[] = {
	"main-battery",
};

static const struct power_supply_desc bq24190_charger_desc = {
	.name			= "bq24190-charger",
	.type			= POWER_SUPPLY_TYPE_USB,
	.properties		= bq24190_charger_properties,
	.num_properties		= ARRAY_SIZE(bq24190_charger_properties),
	.get_property		= bq24190_charger_get_property,
	.set_property		= bq24190_charger_set_property,
	.property_is_writeable	= bq24190_charger_property_is_writeable,
	.external_power_changed	= bq24190_charger_external_power_changed,
};

/* Battery power supply property routines */

static int bq24190_battery_get_status(struct bq24190_dev_info *bdi,
		union power_supply_propval *val)
{
	u8 ss_reg, chrg_fault;
	int status, ret;

	mutex_lock(&bdi->f_reg_lock);
	chrg_fault = bdi->f_reg;
	mutex_unlock(&bdi->f_reg_lock);

	chrg_fault &= BQ24190_REG_F_CHRG_FAULT_MASK;
	chrg_fault >>= BQ24190_REG_F_CHRG_FAULT_SHIFT;

	ret = bq24190_read(bdi, BQ24190_REG_SS, &ss_reg);
	if (ret < 0)
		return ret;

	/*
	 * The battery must be discharging when any of these are true:
	 * - there is no good power source;
	 * - there is a charge fault.
	 * Could also be discharging when in "supplement mode" but
	 * there is no way to tell when its in that mode.
	 */
	if (!(ss_reg & BQ24190_REG_SS_PG_STAT_MASK) || chrg_fault) {
		status = POWER_SUPPLY_STATUS_DISCHARGING;
	} else {
		ss_reg &= BQ24190_REG_SS_CHRG_STAT_MASK;
		ss_reg >>= BQ24190_REG_SS_CHRG_STAT_SHIFT;

		switch (ss_reg) {
		case 0x0: /* Not Charging */
			status = POWER_SUPPLY_STATUS_NOT_CHARGING;
			break;
		case 0x1: /* Pre-charge */
		case 0x2: /* Fast Charging */
			status = POWER_SUPPLY_STATUS_CHARGING;
			break;
		case 0x3: /* Charge Termination Done */
			status = POWER_SUPPLY_STATUS_FULL;
			break;
		default:
			ret = -EIO;
		}
	}

	if (!ret)
		val->intval = status;

	return ret;
}

static int bq24190_battery_get_health(struct bq24190_dev_info *bdi,
		union power_supply_propval *val)
{
	u8 v;
	int health;

	mutex_lock(&bdi->f_reg_lock);
	v = bdi->f_reg;
	mutex_unlock(&bdi->f_reg_lock);

	if (v & BQ24190_REG_F_BAT_FAULT_MASK) {
		health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	} else {
		v &= bdi->info->ntc_fault_mask;

		health = v ? bdi->info->get_ntc_status(v) : POWER_SUPPLY_HEALTH_GOOD;
	}

	val->intval = health;
	return 0;
}

static int bq24190_battery_get_online(struct bq24190_dev_info *bdi,
		union power_supply_propval *val)
{
	u8 batfet_disable;
	int ret;

	ret = bq24190_read_mask(bdi, BQ24190_REG_MOC,
			BQ24190_REG_MOC_BATFET_DISABLE_MASK,
			BQ24190_REG_MOC_BATFET_DISABLE_SHIFT, &batfet_disable);
	if (ret < 0)
		return ret;

	val->intval = !batfet_disable;
	return 0;
}

static int bq24190_battery_set_online(struct bq24190_dev_info *bdi,
		const union power_supply_propval *val)
{
	return bq24190_write_mask(bdi, BQ24190_REG_MOC,
			BQ24190_REG_MOC_BATFET_DISABLE_MASK,
			BQ24190_REG_MOC_BATFET_DISABLE_SHIFT, !val->intval);
}

static int bq24190_battery_get_temp_alert_max(struct bq24190_dev_info *bdi,
		union power_supply_propval *val)
{
	int temp, ret;

	ret = bq24190_get_field_val(bdi, BQ24190_REG_ICTRC,
			BQ24190_REG_ICTRC_TREG_MASK,
			BQ24190_REG_ICTRC_TREG_SHIFT,
			bq24190_ictrc_treg_values,
			ARRAY_SIZE(bq24190_ictrc_treg_values), &temp);
	if (ret < 0)
		return ret;

	val->intval = temp;
	return 0;
}

static int bq24190_battery_set_temp_alert_max(struct bq24190_dev_info *bdi,
		const union power_supply_propval *val)
{
	return bq24190_set_field_val(bdi, BQ24190_REG_ICTRC,
			BQ24190_REG_ICTRC_TREG_MASK,
			BQ24190_REG_ICTRC_TREG_SHIFT,
			bq24190_ictrc_treg_values,
			ARRAY_SIZE(bq24190_ictrc_treg_values), val->intval);
}

static int bq24190_battery_get_property(struct power_supply *psy,
		enum power_supply_property psp, union power_supply_propval *val)
{
	struct bq24190_dev_info *bdi = power_supply_get_drvdata(psy);
	int ret;

	dev_warn(bdi->dev, "warning: /sys/class/power_supply/bq24190-battery is deprecated\n");
	dev_dbg(bdi->dev, "prop: %d\n", psp);

	ret = pm_runtime_resume_and_get(bdi->dev);
	if (ret < 0)
		return ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		ret = bq24190_battery_get_status(bdi, val);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		ret = bq24190_battery_get_health(bdi, val);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		ret = bq24190_battery_get_online(bdi, val);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		/* Could be Li-on or Li-polymer but no way to tell which */
		val->intval = POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
		ret = 0;
		break;
	case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
		ret = bq24190_battery_get_temp_alert_max(bdi, val);
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_SYSTEM;
		ret = 0;
		break;
	default:
		ret = -ENODATA;
	}

	pm_runtime_mark_last_busy(bdi->dev);
	pm_runtime_put_autosuspend(bdi->dev);

	return ret;
}

static int bq24190_battery_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct bq24190_dev_info *bdi = power_supply_get_drvdata(psy);
	int ret;

	dev_warn(bdi->dev, "warning: /sys/class/power_supply/bq24190-battery is deprecated\n");
	dev_dbg(bdi->dev, "prop: %d\n", psp);

	ret = pm_runtime_resume_and_get(bdi->dev);
	if (ret < 0)
		return ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = bq24190_battery_set_online(bdi, val);
		break;
	case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
		ret = bq24190_battery_set_temp_alert_max(bdi, val);
		break;
	default:
		ret = -EINVAL;
	}

	pm_runtime_mark_last_busy(bdi->dev);
	pm_runtime_put_autosuspend(bdi->dev);

	return ret;
}

static int bq24190_battery_property_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_TEMP_ALERT_MAX:
		ret = 1;
		break;
	default:
		ret = 0;
	}

	return ret;
}

static enum power_supply_property bq24190_battery_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_TEMP_ALERT_MAX,
	POWER_SUPPLY_PROP_SCOPE,
};

static const struct power_supply_desc bq24190_battery_desc = {
	.name			= "bq24190-battery",
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.properties		= bq24190_battery_properties,
	.num_properties		= ARRAY_SIZE(bq24190_battery_properties),
	.get_property		= bq24190_battery_get_property,
	.set_property		= bq24190_battery_set_property,
	.property_is_writeable	= bq24190_battery_property_is_writeable,
};

static int bq24190_configure_usb_otg(struct bq24190_dev_info *bdi, u8 ss_reg)
{
	bool otg_enabled;
	int ret;

	otg_enabled = !!(ss_reg & BQ24190_REG_SS_VBUS_STAT_MASK);
	ret = extcon_set_state_sync(bdi->edev, EXTCON_USB, otg_enabled);
	if (ret < 0)
		dev_err(bdi->dev, "Can't set extcon state to %d: %d\n",
			otg_enabled, ret);

	return ret;
}

static void bq24190_check_status(struct bq24190_dev_info *bdi)
{
	const u8 battery_mask_ss = BQ24190_REG_SS_CHRG_STAT_MASK;
	u8 battery_mask_f = BQ24190_REG_F_BAT_FAULT_MASK;
	bool alert_charger = false, alert_battery = false;
	u8 ss_reg = 0, f_reg = 0;
	int i, ret;

	battery_mask_f |= bdi->info->ntc_fault_mask;

	ret = bq24190_read(bdi, BQ24190_REG_SS, &ss_reg);
	if (ret < 0) {
		dev_err(bdi->dev, "Can't read SS reg: %d\n", ret);
		return;
	}

	i = 0;
	do {
		ret = bq24190_read(bdi, BQ24190_REG_F, &f_reg);
		if (ret < 0) {
			dev_err(bdi->dev, "Can't read F reg: %d\n", ret);
			return;
		}
	} while (f_reg && ++i < 2);

	/* ignore over/under voltage fault after disconnect */
	if (f_reg == (1 << BQ24190_REG_F_CHRG_FAULT_SHIFT) &&
	    !(ss_reg & BQ24190_REG_SS_PG_STAT_MASK))
		f_reg = 0;

	if (f_reg != bdi->f_reg) {
		dev_warn(bdi->dev,
			"Fault: boost %d, charge %d, battery %d, ntc %d\n",
			!!(f_reg & BQ24190_REG_F_BOOST_FAULT_MASK),
			!!(f_reg & BQ24190_REG_F_CHRG_FAULT_MASK),
			!!(f_reg & BQ24190_REG_F_BAT_FAULT_MASK),
			!!(f_reg & bdi->info->ntc_fault_mask));

		mutex_lock(&bdi->f_reg_lock);
		if ((bdi->f_reg & battery_mask_f) != (f_reg & battery_mask_f))
			alert_battery = true;
		if ((bdi->f_reg & ~battery_mask_f) != (f_reg & ~battery_mask_f))
			alert_charger = true;
		bdi->f_reg = f_reg;
		mutex_unlock(&bdi->f_reg_lock);
	}

	if (ss_reg != bdi->ss_reg) {
		/*
		 * The device is in host mode so when PG_STAT goes from 1->0
		 * (i.e., power removed) HIZ needs to be disabled.
		 */
		if ((bdi->ss_reg & BQ24190_REG_SS_PG_STAT_MASK) &&
				!(ss_reg & BQ24190_REG_SS_PG_STAT_MASK)) {
			ret = bq24190_write_mask(bdi, BQ24190_REG_ISC,
					BQ24190_REG_ISC_EN_HIZ_MASK,
					BQ24190_REG_ISC_EN_HIZ_SHIFT,
					0);
			if (ret < 0)
				dev_err(bdi->dev, "Can't access ISC reg: %d\n",
					ret);
		}

		if ((bdi->ss_reg & battery_mask_ss) != (ss_reg & battery_mask_ss))
			alert_battery = true;
		if ((bdi->ss_reg & ~battery_mask_ss) != (ss_reg & ~battery_mask_ss))
			alert_charger = true;
		bdi->ss_reg = ss_reg;
	}

	if (alert_charger || alert_battery) {
		power_supply_changed(bdi->charger);
		bq24190_configure_usb_otg(bdi, ss_reg);
	}
	if (alert_battery && bdi->battery)
		power_supply_changed(bdi->battery);

	dev_dbg(bdi->dev, "ss_reg: 0x%02x, f_reg: 0x%02x\n", ss_reg, f_reg);
}

static irqreturn_t bq24190_irq_handler_thread(int irq, void *data)
{
	struct bq24190_dev_info *bdi = data;
	int error;

	bdi->irq_event = true;
	error = pm_runtime_resume_and_get(bdi->dev);
	if (error < 0) {
		dev_warn(bdi->dev, "pm_runtime_get failed: %i\n", error);
		return IRQ_NONE;
	}
	bq24190_check_status(bdi);
	pm_runtime_mark_last_busy(bdi->dev);
	pm_runtime_put_autosuspend(bdi->dev);
	bdi->irq_event = false;

	return IRQ_HANDLED;
}

static int bq24190_check_chip(struct bq24190_dev_info *bdi)
{
	u8 v;
	int ret;

	ret = bq24190_read_mask(bdi, BQ24190_REG_VPRS,
			BQ24190_REG_VPRS_PN_MASK,
			BQ24190_REG_VPRS_PN_SHIFT,
			&v);
	if (ret < 0)
		return ret;

	switch (v) {
	case BQ24190_REG_VPRS_PN_24190:
	case BQ24190_REG_VPRS_PN_24192:
	case BQ24190_REG_VPRS_PN_24192I:
		break;
	default:
		dev_err(bdi->dev, "Error unknown model: 0x%02x\n", v);
		return -ENODEV;
	}

	return 0;
}

static int bq24296_check_chip(struct bq24190_dev_info *bdi)
{
	u8 v;
	int ret;

	ret = bq24190_read_mask(bdi, BQ24190_REG_VPRS,
			BQ24296_REG_VPRS_PN_MASK,
			BQ24296_REG_VPRS_PN_SHIFT,
			&v);
	if (ret < 0)
		return ret;

	switch (v) {
	case BQ24296_REG_VPRS_PN_24296:
		break;
	default:
		dev_err(bdi->dev, "Error unknown model: 0x%02x\n", v);
		return -ENODEV;
	}

	return 0;
}

static int bq24190_hw_init(struct bq24190_dev_info *bdi)
{
	int ret;

	ret = bdi->info->check_chip(bdi);
	if (ret < 0)
		return ret;

	ret = bq24190_register_reset(bdi);
	if (ret < 0)
		return ret;

	ret = bq24190_set_config(bdi);
	if (ret < 0)
		return ret;

	return bq24190_read(bdi, BQ24190_REG_SS, &bdi->ss_reg);
}

static int bq24190_get_config(struct bq24190_dev_info *bdi)
{
	const char * const s = "ti,system-minimum-microvolt";
	struct power_supply_battery_info *info;
	int v, idx;

	idx = bdi->info->ichg_array_size - 1;

	bdi->ichg_max = bq24190_ccc_ichg_values[idx];

	idx = ARRAY_SIZE(bq24190_cvc_vreg_values) - 1;
	bdi->vreg_max = bq24190_cvc_vreg_values[idx];

	if (device_property_read_u32(bdi->dev, s, &v) == 0) {
		v /= 1000;
		if (v >= BQ24190_REG_POC_SYS_MIN_MIN
		 && v <= BQ24190_REG_POC_SYS_MIN_MAX)
			bdi->sys_min = v;
		else
			dev_warn(bdi->dev, "invalid value for %s: %u\n", s, v);
	}

	if (!power_supply_get_battery_info(bdi->charger, &info)) {
		v = info->precharge_current_ua / 1000;
		if (v >= BQ24190_REG_PCTCC_IPRECHG_MIN
		 && v <= BQ24190_REG_PCTCC_IPRECHG_MAX)
			bdi->iprechg = v;
		else
			dev_warn(bdi->dev, "invalid value for battery:precharge-current-microamp: %d\n",
				 v);

		v = info->charge_term_current_ua / 1000;
		if (v >= BQ24190_REG_PCTCC_ITERM_MIN
		 && v <= BQ24190_REG_PCTCC_ITERM_MAX)
			bdi->iterm = v;
		else
			dev_warn(bdi->dev, "invalid value for battery:charge-term-current-microamp: %d\n",
				 v);

		/* These are optional, so no warning when not set */
		v = info->constant_charge_current_max_ua;
		if (v >= bq24190_ccc_ichg_values[0] && v <= bdi->ichg_max)
			bdi->ichg = bdi->ichg_max = v;

		v = info->constant_charge_voltage_max_uv;
		if (v >= bq24190_cvc_vreg_values[0] && v <= bdi->vreg_max)
			bdi->vreg = bdi->vreg_max = v;
	}

	return 0;
}

static const struct bq24190_chip_info bq24190_chip_info_tbl[] = {
	[BQ24190] = {
		.ichg_array_size = ARRAY_SIZE(bq24190_ccc_ichg_values),
#ifdef CONFIG_REGULATOR
		.vbus_desc = &bq24190_vbus_desc,
#endif
		.check_chip = bq24190_check_chip,
		.set_chg_config = bq24190_battery_set_chg_config,
		.ntc_fault_mask = BQ24190_REG_F_NTC_FAULT_MASK,
		.get_ntc_status = bq24190_charger_get_ntc_status,
		.set_otg_vbus = bq24190_set_otg_vbus,
	},
	[BQ24192] = {
		.ichg_array_size = ARRAY_SIZE(bq24190_ccc_ichg_values),
#ifdef CONFIG_REGULATOR
		.vbus_desc = &bq24190_vbus_desc,
#endif
		.check_chip = bq24190_check_chip,
		.set_chg_config = bq24190_battery_set_chg_config,
		.ntc_fault_mask = BQ24190_REG_F_NTC_FAULT_MASK,
		.get_ntc_status = bq24190_charger_get_ntc_status,
		.set_otg_vbus = bq24190_set_otg_vbus,
	},
	[BQ24192i] = {
		.ichg_array_size = ARRAY_SIZE(bq24190_ccc_ichg_values),
#ifdef CONFIG_REGULATOR
		.vbus_desc = &bq24190_vbus_desc,
#endif
		.check_chip = bq24190_check_chip,
		.set_chg_config = bq24190_battery_set_chg_config,
		.ntc_fault_mask = BQ24190_REG_F_NTC_FAULT_MASK,
		.get_ntc_status = bq24190_charger_get_ntc_status,
		.set_otg_vbus = bq24190_set_otg_vbus,
	},
	[BQ24196] = {
		.ichg_array_size = ARRAY_SIZE(bq24190_ccc_ichg_values),
#ifdef CONFIG_REGULATOR
		.vbus_desc = &bq24190_vbus_desc,
#endif
		.check_chip = bq24190_check_chip,
		.set_chg_config = bq24190_battery_set_chg_config,
		.ntc_fault_mask = BQ24190_REG_F_NTC_FAULT_MASK,
		.get_ntc_status = bq24190_charger_get_ntc_status,
		.set_otg_vbus = bq24190_set_otg_vbus,
	},
	[BQ24296] = {
		.ichg_array_size = BQ24296_CCC_ICHG_VALUES_LEN,
#ifdef CONFIG_REGULATOR
		.vbus_desc = &bq24296_vbus_desc,
#endif
		.check_chip = bq24296_check_chip,
		.set_chg_config = bq24296_battery_set_chg_config,
		.ntc_fault_mask = BQ24296_REG_F_NTC_FAULT_MASK,
		.get_ntc_status = bq24296_charger_get_ntc_status,
		.set_otg_vbus = bq24296_set_otg_vbus,
	},
};

static int bq24190_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct i2c_adapter *adapter = client->adapter;
	struct device *dev = &client->dev;
	struct power_supply_config charger_cfg = {}, battery_cfg = {};
	struct bq24190_dev_info *bdi;
	int ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(dev, "No support for SMBUS_BYTE_DATA\n");
		return -ENODEV;
	}

	bdi = devm_kzalloc(dev, sizeof(*bdi), GFP_KERNEL);
	if (!bdi) {
		dev_err(dev, "Can't alloc bdi struct\n");
		return -ENOMEM;
	}

	bdi->client = client;
	bdi->dev = dev;
	strscpy(bdi->model_name, id->name, sizeof(bdi->model_name));
	bdi->info = i2c_get_match_data(client);
	mutex_init(&bdi->f_reg_lock);
	bdi->charge_type = POWER_SUPPLY_CHARGE_TYPE_FAST;
	bdi->f_reg = 0;
	bdi->ss_reg = BQ24190_REG_SS_VBUS_STAT_MASK; /* impossible state */
	INIT_DELAYED_WORK(&bdi->input_current_limit_work,
			  bq24190_input_current_limit_work);

	i2c_set_clientdata(client, bdi);

	if (client->irq <= 0) {
		dev_err(dev, "Can't get irq info\n");
		return -EINVAL;
	}

	bdi->edev = devm_extcon_dev_allocate(dev, bq24190_usb_extcon_cable);
	if (IS_ERR(bdi->edev))
		return PTR_ERR(bdi->edev);

	ret = devm_extcon_dev_register(dev, bdi->edev);
	if (ret < 0)
		return ret;

	pm_runtime_enable(dev);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, 600);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "pm_runtime_get failed: %i\n", ret);
		goto out_pmrt;
	}

#ifdef CONFIG_SYSFS
	bq24190_sysfs_init_attrs();
	charger_cfg.attr_grp = bq24190_sysfs_groups;
#endif

	charger_cfg.drv_data = bdi;
	charger_cfg.of_node = dev->of_node;
	charger_cfg.supplied_to = bq24190_charger_supplied_to;
	charger_cfg.num_supplicants = ARRAY_SIZE(bq24190_charger_supplied_to);
	bdi->charger = power_supply_register(dev, &bq24190_charger_desc,
						&charger_cfg);
	if (IS_ERR(bdi->charger)) {
		dev_err(dev, "Can't register charger\n");
		ret = PTR_ERR(bdi->charger);
		goto out_pmrt;
	}

	/* the battery class is deprecated and will be removed. */
	/* in the interim, this property hides it.              */
	if (!device_property_read_bool(dev, "omit-battery-class")) {
		battery_cfg.drv_data = bdi;
		bdi->battery = power_supply_register(dev, &bq24190_battery_desc,
						     &battery_cfg);
		if (IS_ERR(bdi->battery)) {
			dev_err(dev, "Can't register battery\n");
			ret = PTR_ERR(bdi->battery);
			goto out_charger;
		}
	}

	ret = bq24190_get_config(bdi);
	if (ret < 0) {
		dev_err(dev, "Can't get devicetree config\n");
		goto out_charger;
	}

	ret = bq24190_hw_init(bdi);
	if (ret < 0) {
		dev_err(dev, "Hardware init failed\n");
		goto out_charger;
	}

	ret = bq24190_configure_usb_otg(bdi, bdi->ss_reg);
	if (ret < 0)
		goto out_charger;

	bdi->initialized = true;

	ret = devm_request_threaded_irq(dev, client->irq, NULL,
			bq24190_irq_handler_thread,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			"bq24190-charger", bdi);
	if (ret < 0) {
		dev_err(dev, "Can't set up irq handler\n");
		goto out_charger;
	}

	ret = bq24190_register_vbus_regulator(bdi);
	if (ret < 0)
		goto out_charger;

	enable_irq_wake(client->irq);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;

out_charger:
	if (!IS_ERR_OR_NULL(bdi->battery))
		power_supply_unregister(bdi->battery);
	power_supply_unregister(bdi->charger);

out_pmrt:
	pm_runtime_put_sync(dev);
	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_disable(dev);
	return ret;
}

static void bq24190_remove(struct i2c_client *client)
{
	struct bq24190_dev_info *bdi = i2c_get_clientdata(client);
	int error;

	cancel_delayed_work_sync(&bdi->input_current_limit_work);
	error = pm_runtime_resume_and_get(bdi->dev);
	if (error < 0)
		dev_warn(bdi->dev, "pm_runtime_get failed: %i\n", error);

	bq24190_register_reset(bdi);
	if (bdi->battery)
		power_supply_unregister(bdi->battery);
	power_supply_unregister(bdi->charger);
	if (error >= 0)
		pm_runtime_put_sync(bdi->dev);
	pm_runtime_dont_use_autosuspend(bdi->dev);
	pm_runtime_disable(bdi->dev);
}

static void bq24190_shutdown(struct i2c_client *client)
{
	struct bq24190_dev_info *bdi = i2c_get_clientdata(client);

	/* Turn off 5V boost regulator on shutdown */
	bdi->info->set_otg_vbus(bdi, false);
}

static __maybe_unused int bq24190_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bq24190_dev_info *bdi = i2c_get_clientdata(client);

	if (!bdi->initialized)
		return 0;

	dev_dbg(bdi->dev, "%s\n", __func__);

	return 0;
}

static __maybe_unused int bq24190_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bq24190_dev_info *bdi = i2c_get_clientdata(client);

	if (!bdi->initialized)
		return 0;

	if (!bdi->irq_event) {
		dev_dbg(bdi->dev, "checking events on possible wakeirq\n");
		bq24190_check_status(bdi);
	}

	return 0;
}

static __maybe_unused int bq24190_pm_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bq24190_dev_info *bdi = i2c_get_clientdata(client);
	int error;

	error = pm_runtime_resume_and_get(bdi->dev);
	if (error < 0)
		dev_warn(bdi->dev, "pm_runtime_get failed: %i\n", error);

	bq24190_register_reset(bdi);

	if (error >= 0) {
		pm_runtime_mark_last_busy(bdi->dev);
		pm_runtime_put_autosuspend(bdi->dev);
	}

	return 0;
}

static __maybe_unused int bq24190_pm_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bq24190_dev_info *bdi = i2c_get_clientdata(client);
	int error;

	bdi->f_reg = 0;
	bdi->ss_reg = BQ24190_REG_SS_VBUS_STAT_MASK; /* impossible state */

	error = pm_runtime_resume_and_get(bdi->dev);
	if (error < 0)
		dev_warn(bdi->dev, "pm_runtime_get failed: %i\n", error);

	bq24190_register_reset(bdi);
	bq24190_set_config(bdi);
	bq24190_read(bdi, BQ24190_REG_SS, &bdi->ss_reg);

	if (error >= 0) {
		pm_runtime_mark_last_busy(bdi->dev);
		pm_runtime_put_autosuspend(bdi->dev);
	}

	/* Things may have changed while suspended so alert upper layer */
	power_supply_changed(bdi->charger);
	if (bdi->battery)
		power_supply_changed(bdi->battery);

	return 0;
}

static const struct dev_pm_ops bq24190_pm_ops = {
	SET_RUNTIME_PM_OPS(bq24190_runtime_suspend, bq24190_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(bq24190_pm_suspend, bq24190_pm_resume)
};

static const struct i2c_device_id bq24190_i2c_ids[] = {
	{ "bq24190", (kernel_ulong_t)&bq24190_chip_info_tbl[BQ24190] },
	{ "bq24192", (kernel_ulong_t)&bq24190_chip_info_tbl[BQ24192] },
	{ "bq24192i", (kernel_ulong_t)&bq24190_chip_info_tbl[BQ24192i] },
	{ "bq24196", (kernel_ulong_t)&bq24190_chip_info_tbl[BQ24196] },
	{ "bq24296", (kernel_ulong_t)&bq24190_chip_info_tbl[BQ24296] },
	{ },
};
MODULE_DEVICE_TABLE(i2c, bq24190_i2c_ids);

static const struct of_device_id bq24190_of_match[] = {
	{ .compatible = "ti,bq24190", .data = &bq24190_chip_info_tbl[BQ24190] },
	{ .compatible = "ti,bq24192", .data = &bq24190_chip_info_tbl[BQ24192] },
	{ .compatible = "ti,bq24192i", .data = &bq24190_chip_info_tbl[BQ24192i] },
	{ .compatible = "ti,bq24196", .data = &bq24190_chip_info_tbl[BQ24196] },
	{ .compatible = "ti,bq24296", .data = &bq24190_chip_info_tbl[BQ24296] },
	{ },
};
MODULE_DEVICE_TABLE(of, bq24190_of_match);

static struct i2c_driver bq24190_driver = {
	.probe		= bq24190_probe,
	.remove		= bq24190_remove,
	.shutdown	= bq24190_shutdown,
	.id_table	= bq24190_i2c_ids,
	.driver = {
		.name		= "bq24190-charger",
		.pm		= &bq24190_pm_ops,
		.of_match_table	= bq24190_of_match,
	},
};
module_i2c_driver(bq24190_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark A. Greer <mgreer@animalcreek.com>");
MODULE_DESCRIPTION("TI BQ24190 Charger Driver");
