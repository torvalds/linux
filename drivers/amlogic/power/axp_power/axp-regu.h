#ifndef _LINUX_AXP_REGU_H_
#define _LINUX_AXP_REGU_H_

#include "axp-mfd.h"


/* AXP18 Regulator Registers */
#define AXP18_RTC			POWER18_STATUS
#define AXP18_ANALOG		POWER18_LDOOUT_VOL
#define AXP18_MOMERY		POWER18_LDOOUT_VOL
#define AXP18_SPDIF			POWER18_SW_CTL
#define AXP18_IO			POWER18_DC12OUT_VOL
#define AXP18_CORE			POWER18_DC12OUT_VOL
#define AXP18_SDRAM			POWER18_DC12OUT_VOL
#define AXP18_SDCARD		POWER18_DC12OUT_VOL

#define AXP18_LDO1EN		POWER18_STATUS
#define AXP18_LDO2EN		POWER18_DCDCCTL
#define AXP18_LDO3EN		POWER18_LDOOUT_VOL
#define AXP18_LDO4EN		POWER18_SW_CTL
#define AXP18_LDO5EN		POWER18_SW_CTL
#define AXP18_DCDC1EN		POWER18_STATUS
#define AXP18_DCDC2EN		POWER18_STATUS
#define AXP18_DCDC3EN		POWER18_DCDCCTL
#define AXP18_SW1EN			POWER18_SW_CTL
#define AXP18_SW2EN			POWER18_SW_CTL

#define AXP18_BUCKMODE		POWER18_DCDCCTL
#define AXP18_BUCKFREQ		POWER18_PEK


/* AXP19 Regulator Registers */
#define AXP19_RTC		    POWER19_STATUS
#define AXP19_ANALOG1		POWER19_LDO24OUT_VOL
#define AXP19_DIGITAL      POWER19_LDO3OUT_VOL
#define AXP19_ANALOG2      POWER19_LDO24OUT_VOL
#define AXP19_LDOIO0       POWER19_GPIO0_VOL
#define AXP19_IO           POWER19_DC1OUT_VOL
#define AXP19_CORE         POWER19_DC2OUT_VOL
#define AXP19_MEMORY       POWER19_DC3OUT_VOL

#define AXP19_LDO1EN		POWER19_STATUS
#define AXP19_LDO2EN		POWER19_LDO24_DC13_CTL
#define AXP19_LDO3EN		POWER19_LDO3_DC2_CTL
#define AXP19_LDO4EN		POWER19_LDO24_DC13_CTL
#define AXP19_LDOIOEN		POWER19_GPIO0_CTL
#define AXP19_DCDC1EN      POWER19_LDO24_DC13_CTL
#define AXP19_DCDC2EN      POWER19_LDO3_DC2_CTL
#define AXP19_DCDC3EN      POWER19_LDO24_DC13_CTL

#define AXP19_BUCKMODE     POWER19_DCDC_MODESET
#define AXP19_BUCKFREQ     POWER19_DCDC_FREQSET

/* AXP20 Regulator Registers */
#define AXP20_LDO1		    POWER20_STATUS
#define AXP20_LDO2		POWER20_LDO24OUT_VOL
#define AXP20_LDO3       POWER20_LDO3OUT_VOL
#define AXP20_LDO4      POWER20_LDO24OUT_VOL
#define AXP20_BUCK2      POWER20_DC2OUT_VOL
#define AXP20_BUCK3       POWER20_DC3OUT_VOL
#define AXP20_LDOIO0		POWER20_GPIO0_VOL

#define AXP20_LDO1EN		POWER20_STATUS
#define AXP20_LDO2EN		POWER20_LDO234_DC23_CTL
#define AXP20_LDO3EN		POWER20_LDO234_DC23_CTL
#define AXP20_LDO4EN		POWER20_LDO234_DC23_CTL
#define AXP20_BUCK2EN      POWER20_LDO234_DC23_CTL
#define AXP20_BUCK3EN      POWER20_LDO234_DC23_CTL
#define AXP20_LDOIOEN		POWER20_GPIO0_CTL


#define AXP20_BUCKMODE     POWER20_DCDC_MODESET
#define AXP20_BUCKFREQ     POWER20_DCDC_FREQSET


#define AXP_LDO(_pmic, _id, min, max, step, vreg, shift, nbits, ereg, ebit, on)	\
{									\
	.desc	= {							\
		.name	= #_pmic"_LDO" #_id,					\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= _pmic##_ID_LDO##_id,				\
		.n_voltages = (step) ? ((max - min) / step + 1) : 1,	\
		.owner	= THIS_MODULE,					\
	},								\
	.min_uV		= (min) * 1000,					\
	.max_uV		= (max) * 1000,					\
	.step_uV	= (step) * 1000,				\
	.vol_reg	= _pmic##_##vreg,				\
	.vol_shift	= (shift),					\
	.vol_nbits	= (nbits),					\
	.enable_reg	= _pmic##_##ereg,				\
	.enable_bit	= (ebit),					\
    .always_on  = on,                        \
}

#define AXP_BUCK(_pmic, _id, min, max, step, vreg, shift, nbits, ereg, ebit, on)	\
{									\
	.desc	= {							\
		.name	= #_pmic"_BUCK" #_id,					\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= _pmic##_ID_BUCK##_id,				\
		.n_voltages = (step) ? ((max - min) / step + 1) : 1,	\
		.owner	= THIS_MODULE,					\
	},								\
	.min_uV		= (min) * 1000,					\
	.max_uV		= (max) * 1000,					\
	.step_uV	= (step) * 1000,				\
	.vol_reg	= _pmic##_##vreg,				\
	.vol_shift	= (shift),					\
	.vol_nbits	= (nbits),					\
	.enable_reg	= _pmic##_##ereg,				\
	.enable_bit	= (ebit),					\
    .always_on  = on,                       \
}

#define AXP_SW(_pmic, _id, min, max, step, vreg, shift, nbits, ereg, ebit, on)	\
{									\
	.desc	= {							\
		.name	= #_pmic"_SW" #_id,					\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= _pmic##_ID_SW##_id,				\
		.n_voltages = (step) ? ((max - min) / step + 1) : 1,	\
		.owner	= THIS_MODULE,					\
	},								\
	.min_uV		= (min) * 1000,					\
	.max_uV		= (max) * 1000,					\
	.step_uV	= (step) * 1000,				\
	.vol_reg	= _pmic##_##vreg,				\
	.vol_shift	= (shift),					\
	.vol_nbits	= (nbits),					\
	.enable_reg	= _pmic##_##ereg,				\
	.enable_bit	= (ebit),					\
    .always_on  = on,                       \
}

#define AXP_REGU_ATTR(_name)					\
{									\
	.attr = { .name = #_name,.mode = 0644 },					\
	.show =  _name##_show,				\
	.store = _name##_store, \
}

struct axp_regulator_info {
	struct regulator_desc desc;

	int	min_uV;
	int	max_uV;
	int	step_uV;
	int	vol_reg;
	int	vol_shift;
	int	vol_nbits;
	int	enable_reg;
	int	enable_bit;
    int always_on;
};

#endif
