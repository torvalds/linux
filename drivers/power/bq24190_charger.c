/*
 * Driver for the TI bq24190 battery charger.
 *
 * Author: Mark A. Greer <mgreer@animalcreek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/of_irq.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/power_supply.h>
#include <linux/gpio.h>
#include <linux/i2c.h>

#include <linux/power/bq24190_charger.h>


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
#define BQ24190_REG_POC_SYS_MIN_MASK		(BIT(3) | BIT(2) | BIT(1))
#define BQ24190_REG_POC_SYS_MIN_SHIFT		1
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
#define BQ24190_REG_PCTCC_ITERM_MASK		(BIT(3) | BIT(2) | BIT(1) | \
						 BIT(0))
#define BQ24190_REG_PCTCC_ITERM_SHIFT		0

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

#define BQ24190_REG_VPRS	0x0A /* Vendor/Part/Revision Status */
#define BQ24190_REG_VPRS_PN_MASK		(BIT(5) | BIT(4) | BIT(3))
#define BQ24190_REG_VPRS_PN_SHIFT		3
#define BQ24190_REG_VPRS_PN_24190			0x4
#define BQ24190_REG_VPRS_PN_24192			0x5 /* Also 24193 */
#define BQ24190_REG_VPRS_PN_24192I			0x3
#define BQ24190_REG_VPRS_TS_PROFILE_MASK	BIT(2)
#define BQ24190_REG_VPRS_TS_PROFILE_SHIFT	2
#define BQ24190_REG_VPRS_DEV_REG_MASK		(BIT(1) | BIT(0))
#define BQ24190_REG_VPRS_DEV_REG_SHIFT		0

/*
 * The FAULT register is latched by the bq24190 (except for NTC_FAULT)
 * so the first read after a fault returns the latched value and subsequent
 * reads return the current value.  In order to return the fault status
 * to the user, have the interrupt handler save the reg's value and retrieve
 * it in the appropriate health/status routine.  Each routine has its own
 * flag indicating whether it should use the value stored by the last run
 * of the interrupt handler or do an actual reg read.  That way each routine
 * can report back whatever fault may have occured.
 */
struct bq24190_dev_info {
	struct i2c_client		*client;
	struct device			*dev;
	struct power_supply		*charger;
	struct power_supply		*battery;
	char				model_name[I2C_NAME_SIZE];
	kernel_ulong_t			model;
	unsigned int			gpio_int;
	unsigned int			irq;
	struct mutex			f_reg_lock;
	bool				first_time;
	bool				charger_health_valid;
	bool				battery_health_valid;
	bool				battery_status_valid;
	u8				f_reg;
	u8				ss_reg;
	u8				watchdog;
};

/*
 * The tables below provide a 2-way mapping for the value that goes in
 * the register field and the real-world value that it represents.
 * The index of the array is the value that goes in the register; the
 * number at that index in the array is the real-world value that it
 * represents.
 */
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

/* REG06[1:0] (TREG) in tenths of degrees Celcius */
static const int bq24190_ictrc_treg_values[] = {
	600, 800, 1000, 1200
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

static const struct attribute_group bq24190_sysfs_attr_group = {
	.attrs = bq24190_sysfs_attrs,
};

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
	int ret;
	u8 v;

	info = bq24190_sysfs_field_lookup(attr->attr.name);
	if (!info)
		return -EINVAL;

	ret = bq24190_read_mask(bdi, info->reg, info->mask, info->shift, &v);
	if (ret)
		return ret;

	return scnprintf(buf, PAGE_SIZE, "%hhx\n", v);
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

	ret = bq24190_write_mask(bdi, info->reg, info->mask, info->shift, v);
	if (ret)
		return ret;

	return count;
}

static int bq24190_sysfs_create_group(struct bq24190_dev_info *bdi)
{
	bq24190_sysfs_init_attrs();

	return sysfs_create_group(&bdi->charger->dev.kobj,
			&bq24190_sysfs_attr_group);
}

static void bq24190_sysfs_remove_group(struct bq24190_dev_info *bdi)
{
	sysfs_remove_group(&bdi->charger->dev.kobj, &bq24190_sysfs_attr_group);
}
#else
static int bq24190_sysfs_create_group(struct bq24190_dev_info *bdi)
{
	return 0;
}

static inline void bq24190_sysfs_remove_group(struct bq24190_dev_info *bdi) {}
#endif

/*
 * According to the "Host Mode and default Mode" section of the
 * manual, a write to any register causes the bq24190 to switch
 * from default mode to host mode.  It will switch back to default
 * mode after a WDT timeout unless the WDT is turned off as well.
 * So, by simply turning off the WDT, we accomplish both with the
 * same write.
 */
static int bq24190_set_mode_host(struct bq24190_dev_info *bdi)
{
	int ret;
	u8 v;

	ret = bq24190_read(bdi, BQ24190_REG_CTTC, &v);
	if (ret < 0)
		return ret;

	bdi->watchdog = ((v & BQ24190_REG_CTTC_WATCHDOG_MASK) >>
					BQ24190_REG_CTTC_WATCHDOG_SHIFT);
	v &= ~BQ24190_REG_CTTC_WATCHDOG_MASK;

	return bq24190_write(bdi, BQ24190_REG_CTTC, v);
}

static int bq24190_register_reset(struct bq24190_dev_info *bdi)
{
	int ret, limit = 100;
	u8 v;

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

		if (!v)
			break;

		udelay(10);
	} while (--limit);

	if (!limit)
		return -EIO;

	return 0;
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

	return bq24190_write_mask(bdi, BQ24190_REG_POC,
			BQ24190_REG_POC_CHG_CONFIG_MASK,
			BQ24190_REG_POC_CHG_CONFIG_SHIFT, chg_config);
}

static int bq24190_charger_get_health(struct bq24190_dev_info *bdi,
		union power_supply_propval *val)
{
	u8 v;
	int health, ret;

	mutex_lock(&bdi->f_reg_lock);

	if (bdi->charger_health_valid) {
		v = bdi->f_reg;
		bdi->charger_health_valid = false;
		mutex_unlock(&bdi->f_reg_lock);
	} else {
		mutex_unlock(&bdi->f_reg_lock);

		ret = bq24190_read(bdi, BQ24190_REG_F, &v);
		if (ret < 0)
			return ret;
	}

	if (v & BQ24190_REG_F_BOOST_FAULT_MASK) {
		/*
		 * This could be over-current or over-voltage but there's
		 * no way to tell which.  Return 'OVERVOLTAGE' since there
		 * isn't an 'OVERCURRENT' value defined that we can return
		 * even if it was over-current.
		 */
		health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	} else {
		v &= BQ24190_REG_F_CHRG_FAULT_MASK;
		v >>= BQ24190_REG_F_CHRG_FAULT_SHIFT;

		switch (v) {
		case 0x0: /* Normal */
			health = POWER_SUPPLY_HEALTH_GOOD;
			break;
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
		default:
			health = POWER_SUPPLY_HEALTH_UNKNOWN;
		}
	}

	val->intval = health;

	return 0;
}

static int bq24190_charger_get_online(struct bq24190_dev_info *bdi,
		union power_supply_propval *val)
{
	u8 v;
	int ret;

	ret = bq24190_read_mask(bdi, BQ24190_REG_SS,
			BQ24190_REG_SS_PG_STAT_MASK,
			BQ24190_REG_SS_PG_STAT_SHIFT, &v);
	if (ret < 0)
		return ret;

	val->intval = v;
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
			ARRAY_SIZE(bq24190_ccc_ichg_values), &curr);
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

static int bq24190_charger_get_current_max(struct bq24190_dev_info *bdi,
		union power_supply_propval *val)
{
	int idx = ARRAY_SIZE(bq24190_ccc_ichg_values) - 1;

	val->intval = bq24190_ccc_ichg_values[idx];
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

	return bq24190_set_field_val(bdi, BQ24190_REG_CCC,
			BQ24190_REG_CCC_ICHG_MASK, BQ24190_REG_CCC_ICHG_SHIFT,
			bq24190_ccc_ichg_values,
			ARRAY_SIZE(bq24190_ccc_ichg_values), curr);
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

static int bq24190_charger_get_voltage_max(struct bq24190_dev_info *bdi,
		union power_supply_propval *val)
{
	int idx = ARRAY_SIZE(bq24190_cvc_vreg_values) - 1;

	val->intval = bq24190_cvc_vreg_values[idx];
	return 0;
}

static int bq24190_charger_set_voltage(struct bq24190_dev_info *bdi,
		const union power_supply_propval *val)
{
	return bq24190_set_field_val(bdi, BQ24190_REG_CVC,
			BQ24190_REG_CVC_VREG_MASK, BQ24190_REG_CVC_VREG_SHIFT,
			bq24190_cvc_vreg_values,
			ARRAY_SIZE(bq24190_cvc_vreg_values), val->intval);
}

static int bq24190_charger_get_property(struct power_supply *psy,
		enum power_supply_property psp, union power_supply_propval *val)
{
	struct bq24190_dev_info *bdi = power_supply_get_drvdata(psy);
	int ret;

	dev_dbg(bdi->dev, "prop: %d\n", psp);

	pm_runtime_get_sync(bdi->dev);

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
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = bq24190_charger_get_current(bdi, val);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		ret = bq24190_charger_get_current_max(bdi, val);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = bq24190_charger_get_voltage(bdi, val);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		ret = bq24190_charger_get_voltage_max(bdi, val);
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

	pm_runtime_put_sync(bdi->dev);
	return ret;
}

static int bq24190_charger_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct bq24190_dev_info *bdi = power_supply_get_drvdata(psy);
	int ret;

	dev_dbg(bdi->dev, "prop: %d\n", psp);

	pm_runtime_get_sync(bdi->dev);

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		ret = bq24190_charger_set_charge_type(bdi, val);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = bq24190_charger_set_current(bdi, val);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = bq24190_charger_set_voltage(bdi, val);
		break;
	default:
		ret = -EINVAL;
	}

	pm_runtime_put_sync(bdi->dev);
	return ret;
}

static int bq24190_charger_property_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = 1;
		break;
	default:
		ret = 0;
	}

	return ret;
}

static enum power_supply_property bq24190_charger_properties[] = {
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
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
};

/* Battery power supply property routines */

static int bq24190_battery_get_status(struct bq24190_dev_info *bdi,
		union power_supply_propval *val)
{
	u8 ss_reg, chrg_fault;
	int status, ret;

	mutex_lock(&bdi->f_reg_lock);

	if (bdi->battery_status_valid) {
		chrg_fault = bdi->f_reg;
		bdi->battery_status_valid = false;
		mutex_unlock(&bdi->f_reg_lock);
	} else {
		mutex_unlock(&bdi->f_reg_lock);

		ret = bq24190_read(bdi, BQ24190_REG_F, &chrg_fault);
		if (ret < 0)
			return ret;
	}

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
	int health, ret;

	mutex_lock(&bdi->f_reg_lock);

	if (bdi->battery_health_valid) {
		v = bdi->f_reg;
		bdi->battery_health_valid = false;
		mutex_unlock(&bdi->f_reg_lock);
	} else {
		mutex_unlock(&bdi->f_reg_lock);

		ret = bq24190_read(bdi, BQ24190_REG_F, &v);
		if (ret < 0)
			return ret;
	}

	if (v & BQ24190_REG_F_BAT_FAULT_MASK) {
		health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	} else {
		v &= BQ24190_REG_F_NTC_FAULT_MASK;
		v >>= BQ24190_REG_F_NTC_FAULT_SHIFT;

		switch (v) {
		case 0x0: /* Normal */
			health = POWER_SUPPLY_HEALTH_GOOD;
			break;
		case 0x1: /* TS1 Cold */
		case 0x3: /* TS2 Cold */
		case 0x5: /* Both Cold */
			health = POWER_SUPPLY_HEALTH_COLD;
			break;
		case 0x2: /* TS1 Hot */
		case 0x4: /* TS2 Hot */
		case 0x6: /* Both Hot */
			health = POWER_SUPPLY_HEALTH_OVERHEAT;
			break;
		default:
			health = POWER_SUPPLY_HEALTH_UNKNOWN;
		}
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

	dev_dbg(bdi->dev, "prop: %d\n", psp);

	pm_runtime_get_sync(bdi->dev);

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

	pm_runtime_put_sync(bdi->dev);
	return ret;
}

static int bq24190_battery_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct bq24190_dev_info *bdi = power_supply_get_drvdata(psy);
	int ret;

	dev_dbg(bdi->dev, "prop: %d\n", psp);

	pm_runtime_put_sync(bdi->dev);

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

	pm_runtime_put_sync(bdi->dev);
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

static irqreturn_t bq24190_irq_handler_thread(int irq, void *data)
{
	struct bq24190_dev_info *bdi = data;
	bool alert_userspace = false;
	u8 ss_reg = 0, f_reg = 0;
	int ret;

	pm_runtime_get_sync(bdi->dev);

	ret = bq24190_read(bdi, BQ24190_REG_SS, &ss_reg);
	if (ret < 0) {
		dev_err(bdi->dev, "Can't read SS reg: %d\n", ret);
		goto out;
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

		bdi->ss_reg = ss_reg;
		alert_userspace = true;
	}

	mutex_lock(&bdi->f_reg_lock);

	ret = bq24190_read(bdi, BQ24190_REG_F, &f_reg);
	if (ret < 0) {
		mutex_unlock(&bdi->f_reg_lock);
		dev_err(bdi->dev, "Can't read F reg: %d\n", ret);
		goto out;
	}

	if (f_reg != bdi->f_reg) {
		bdi->f_reg = f_reg;
		bdi->charger_health_valid = true;
		bdi->battery_health_valid = true;
		bdi->battery_status_valid = true;

		alert_userspace = true;
	}

	mutex_unlock(&bdi->f_reg_lock);

	/*
	 * Sometimes bq24190 gives a steady trickle of interrupts even
	 * though the watchdog timer is turned off and neither the STATUS
	 * nor FAULT registers have changed.  Weed out these sprurious
	 * interrupts so userspace isn't alerted for no reason.
	 * In addition, the chip always generates an interrupt after
	 * register reset so we should ignore that one (the very first
	 * interrupt received).
	 */
	if (alert_userspace) {
		if (!bdi->first_time) {
			power_supply_changed(bdi->charger);
			power_supply_changed(bdi->battery);
		} else {
			bdi->first_time = false;
		}
	}

out:
	pm_runtime_put_sync(bdi->dev);

	dev_dbg(bdi->dev, "ss_reg: 0x%02x, f_reg: 0x%02x\n", ss_reg, f_reg);

	return IRQ_HANDLED;
}

static int bq24190_hw_init(struct bq24190_dev_info *bdi)
{
	u8 v;
	int ret;

	pm_runtime_get_sync(bdi->dev);

	/* First check that the device really is what its supposed to be */
	ret = bq24190_read_mask(bdi, BQ24190_REG_VPRS,
			BQ24190_REG_VPRS_PN_MASK,
			BQ24190_REG_VPRS_PN_SHIFT,
			&v);
	if (ret < 0)
		goto out;

	if (v != bdi->model) {
		ret = -ENODEV;
		goto out;
	}

	ret = bq24190_register_reset(bdi);
	if (ret < 0)
		goto out;

	ret = bq24190_set_mode_host(bdi);
out:
	pm_runtime_put_sync(bdi->dev);
	return ret;
}

#ifdef CONFIG_OF
static int bq24190_setup_dt(struct bq24190_dev_info *bdi)
{
	bdi->irq = irq_of_parse_and_map(bdi->dev->of_node, 0);
	if (bdi->irq <= 0)
		return -1;

	return 0;
}
#else
static int bq24190_setup_dt(struct bq24190_dev_info *bdi)
{
	return -1;
}
#endif

static int bq24190_setup_pdata(struct bq24190_dev_info *bdi,
		struct bq24190_platform_data *pdata)
{
	int ret;

	if (!gpio_is_valid(pdata->gpio_int))
		return -1;

	ret = gpio_request(pdata->gpio_int, dev_name(bdi->dev));
	if (ret < 0)
		return -1;

	ret = gpio_direction_input(pdata->gpio_int);
	if (ret < 0)
		goto out;

	bdi->irq = gpio_to_irq(pdata->gpio_int);
	if (!bdi->irq)
		goto out;

	bdi->gpio_int = pdata->gpio_int;
	return 0;

out:
	gpio_free(pdata->gpio_int);
	return -1;
}

static int bq24190_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct device *dev = &client->dev;
	struct bq24190_platform_data *pdata = client->dev.platform_data;
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
	bdi->model = id->driver_data;
	strncpy(bdi->model_name, id->name, I2C_NAME_SIZE);
	mutex_init(&bdi->f_reg_lock);
	bdi->first_time = true;
	bdi->charger_health_valid = false;
	bdi->battery_health_valid = false;
	bdi->battery_status_valid = false;

	i2c_set_clientdata(client, bdi);

	if (dev->of_node)
		ret = bq24190_setup_dt(bdi);
	else
		ret = bq24190_setup_pdata(bdi, pdata);

	if (ret) {
		dev_err(dev, "Can't get irq info\n");
		return -EINVAL;
	}

	ret = devm_request_threaded_irq(dev, bdi->irq, NULL,
			bq24190_irq_handler_thread,
			IRQF_TRIGGER_RISING | IRQF_ONESHOT,
			"bq24190-charger", bdi);
	if (ret < 0) {
		dev_err(dev, "Can't set up irq handler\n");
		goto out1;
	}

	pm_runtime_enable(dev);
	pm_runtime_resume(dev);

	ret = bq24190_hw_init(bdi);
	if (ret < 0) {
		dev_err(dev, "Hardware init failed\n");
		goto out2;
	}

	charger_cfg.drv_data = bdi;
	charger_cfg.supplied_to = bq24190_charger_supplied_to;
	charger_cfg.num_supplicants = ARRAY_SIZE(bq24190_charger_supplied_to),
	bdi->charger = power_supply_register(dev, &bq24190_charger_desc,
						&charger_cfg);
	if (IS_ERR(bdi->charger)) {
		dev_err(dev, "Can't register charger\n");
		ret = PTR_ERR(bdi->charger);
		goto out2;
	}

	battery_cfg.drv_data = bdi;
	bdi->battery = power_supply_register(dev, &bq24190_battery_desc,
						&battery_cfg);
	if (IS_ERR(bdi->battery)) {
		dev_err(dev, "Can't register battery\n");
		ret = PTR_ERR(bdi->battery);
		goto out3;
	}

	ret = bq24190_sysfs_create_group(bdi);
	if (ret) {
		dev_err(dev, "Can't create sysfs entries\n");
		goto out4;
	}

	return 0;

out4:
	power_supply_unregister(bdi->battery);
out3:
	power_supply_unregister(bdi->charger);
out2:
	pm_runtime_disable(dev);
out1:
	if (bdi->gpio_int)
		gpio_free(bdi->gpio_int);

	return ret;
}

static int bq24190_remove(struct i2c_client *client)
{
	struct bq24190_dev_info *bdi = i2c_get_clientdata(client);

	pm_runtime_get_sync(bdi->dev);
	bq24190_register_reset(bdi);
	pm_runtime_put_sync(bdi->dev);

	bq24190_sysfs_remove_group(bdi);
	power_supply_unregister(bdi->battery);
	power_supply_unregister(bdi->charger);
	pm_runtime_disable(bdi->dev);

	if (bdi->gpio_int)
		gpio_free(bdi->gpio_int);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int bq24190_pm_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bq24190_dev_info *bdi = i2c_get_clientdata(client);

	pm_runtime_get_sync(bdi->dev);
	bq24190_register_reset(bdi);
	pm_runtime_put_sync(bdi->dev);

	return 0;
}

static int bq24190_pm_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bq24190_dev_info *bdi = i2c_get_clientdata(client);

	bdi->charger_health_valid = false;
	bdi->battery_health_valid = false;
	bdi->battery_status_valid = false;

	pm_runtime_get_sync(bdi->dev);
	bq24190_register_reset(bdi);
	pm_runtime_put_sync(bdi->dev);

	/* Things may have changed while suspended so alert upper layer */
	power_supply_changed(bdi->charger);
	power_supply_changed(bdi->battery);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(bq24190_pm_ops, bq24190_pm_suspend, bq24190_pm_resume);

/*
 * Only support the bq24190 right now.  The bq24192, bq24192i, and bq24193
 * are similar but not identical so the driver needs to be extended to
 * support them.
 */
static const struct i2c_device_id bq24190_i2c_ids[] = {
	{ "bq24190", BQ24190_REG_VPRS_PN_24190 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, bq24190_i2c_ids);

#ifdef CONFIG_OF
static const struct of_device_id bq24190_of_match[] = {
	{ .compatible = "ti,bq24190", },
	{ },
};
MODULE_DEVICE_TABLE(of, bq24190_of_match);
#else
static const struct of_device_id bq24190_of_match[] = {
	{ },
};
#endif

static struct i2c_driver bq24190_driver = {
	.probe		= bq24190_probe,
	.remove		= bq24190_remove,
	.id_table	= bq24190_i2c_ids,
	.driver = {
		.name		= "bq24190-charger",
		.pm		= &bq24190_pm_ops,
		.of_match_table	= of_match_ptr(bq24190_of_match),
	},
};
module_i2c_driver(bq24190_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark A. Greer <mgreer@animalcreek.com>");
MODULE_ALIAS("i2c:bq24190-charger");
MODULE_DESCRIPTION("TI BQ24190 Charger Driver");
