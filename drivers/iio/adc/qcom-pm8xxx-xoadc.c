// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm PM8xxx PMIC XOADC driver
 *
 * These ADCs are known as HK/XO (house keeping / chrystal oscillator)
 * "XO" in "XOADC" means Chrystal Oscillator. It's a bunch of
 * specific-purpose and general purpose ADC converters and channels.
 *
 * Copyright (C) 2017 Linaro Ltd.
 * Author: Linus Walleij <linus.walleij@linaro.org>
 */

#include <linux/iio/adc/qcom-vadc-common.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/regulator/consumer.h>

/*
 * Definitions for the "user processor" registers lifted from the v3.4
 * Qualcomm tree. Their kernel has two out-of-tree drivers for the ADC:
 * drivers/misc/pmic8058-xoadc.c
 * drivers/hwmon/pm8xxx-adc.c
 * None of them contain any complete register specification, so this is
 * a best effort of combining the information.
 */

/* These appear to be "battery monitor" registers */
#define ADC_ARB_BTM_CNTRL1			0x17e
#define ADC_ARB_BTM_CNTRL1_EN_BTM		BIT(0)
#define ADC_ARB_BTM_CNTRL1_SEL_OP_MODE		BIT(1)
#define ADC_ARB_BTM_CNTRL1_MEAS_INTERVAL1	BIT(2)
#define ADC_ARB_BTM_CNTRL1_MEAS_INTERVAL2	BIT(3)
#define ADC_ARB_BTM_CNTRL1_MEAS_INTERVAL3	BIT(4)
#define ADC_ARB_BTM_CNTRL1_MEAS_INTERVAL4	BIT(5)
#define ADC_ARB_BTM_CNTRL1_EOC			BIT(6)
#define ADC_ARB_BTM_CNTRL1_REQ			BIT(7)

#define ADC_ARB_BTM_AMUX_CNTRL			0x17f
#define ADC_ARB_BTM_ANA_PARAM			0x180
#define ADC_ARB_BTM_DIG_PARAM			0x181
#define ADC_ARB_BTM_RSV				0x182
#define ADC_ARB_BTM_DATA1			0x183
#define ADC_ARB_BTM_DATA0			0x184
#define ADC_ARB_BTM_BAT_COOL_THR1		0x185
#define ADC_ARB_BTM_BAT_COOL_THR0		0x186
#define ADC_ARB_BTM_BAT_WARM_THR1		0x187
#define ADC_ARB_BTM_BAT_WARM_THR0		0x188
#define ADC_ARB_BTM_CNTRL2			0x18c

/* Proper ADC registers */

#define ADC_ARB_USRP_CNTRL			0x197
#define ADC_ARB_USRP_CNTRL_EN_ARB		BIT(0)
#define ADC_ARB_USRP_CNTRL_RSV1			BIT(1)
#define ADC_ARB_USRP_CNTRL_RSV2			BIT(2)
#define ADC_ARB_USRP_CNTRL_RSV3			BIT(3)
#define ADC_ARB_USRP_CNTRL_RSV4			BIT(4)
#define ADC_ARB_USRP_CNTRL_RSV5			BIT(5)
#define ADC_ARB_USRP_CNTRL_EOC			BIT(6)
#define ADC_ARB_USRP_CNTRL_REQ			BIT(7)

#define ADC_ARB_USRP_AMUX_CNTRL			0x198
/*
 * The channel mask includes the bits selecting channel mux and prescaler
 * on PM8058, or channel mux and premux on PM8921.
 */
#define ADC_ARB_USRP_AMUX_CNTRL_CHAN_MASK	0xfc
#define ADC_ARB_USRP_AMUX_CNTRL_RSV0		BIT(0)
#define ADC_ARB_USRP_AMUX_CNTRL_RSV1		BIT(1)
/* On PM8058 this is prescaling, on PM8921 this is premux */
#define ADC_ARB_USRP_AMUX_CNTRL_PRESCALEMUX0	BIT(2)
#define ADC_ARB_USRP_AMUX_CNTRL_PRESCALEMUX1	BIT(3)
#define ADC_ARB_USRP_AMUX_CNTRL_SEL0		BIT(4)
#define ADC_ARB_USRP_AMUX_CNTRL_SEL1		BIT(5)
#define ADC_ARB_USRP_AMUX_CNTRL_SEL2		BIT(6)
#define ADC_ARB_USRP_AMUX_CNTRL_SEL3		BIT(7)
#define ADC_AMUX_PREMUX_SHIFT			2
#define ADC_AMUX_SEL_SHIFT			4

/* We know very little about the bits in this register */
#define ADC_ARB_USRP_ANA_PARAM			0x199
#define ADC_ARB_USRP_ANA_PARAM_DIS		0xFE
#define ADC_ARB_USRP_ANA_PARAM_EN		0xFF

#define ADC_ARB_USRP_DIG_PARAM			0x19A
#define ADC_ARB_USRP_DIG_PARAM_SEL_SHIFT0	BIT(0)
#define ADC_ARB_USRP_DIG_PARAM_SEL_SHIFT1	BIT(1)
#define ADC_ARB_USRP_DIG_PARAM_CLK_RATE0	BIT(2)
#define ADC_ARB_USRP_DIG_PARAM_CLK_RATE1	BIT(3)
#define ADC_ARB_USRP_DIG_PARAM_EOC		BIT(4)
/*
 * On a later ADC the decimation factors are defined as
 * 00 = 512, 01 = 1024, 10 = 2048, 11 = 4096 so assume this
 * holds also for this older XOADC.
 */
#define ADC_ARB_USRP_DIG_PARAM_DEC_RATE0	BIT(5)
#define ADC_ARB_USRP_DIG_PARAM_DEC_RATE1	BIT(6)
#define ADC_ARB_USRP_DIG_PARAM_EN		BIT(7)
#define ADC_DIG_PARAM_DEC_SHIFT			5

#define ADC_ARB_USRP_RSV			0x19B
#define ADC_ARB_USRP_RSV_RST			BIT(0)
#define ADC_ARB_USRP_RSV_DTEST0			BIT(1)
#define ADC_ARB_USRP_RSV_DTEST1			BIT(2)
#define ADC_ARB_USRP_RSV_OP			BIT(3)
#define ADC_ARB_USRP_RSV_IP_SEL0		BIT(4)
#define ADC_ARB_USRP_RSV_IP_SEL1		BIT(5)
#define ADC_ARB_USRP_RSV_IP_SEL2		BIT(6)
#define ADC_ARB_USRP_RSV_TRM			BIT(7)
#define ADC_RSV_IP_SEL_SHIFT			4

#define ADC_ARB_USRP_DATA0			0x19D
#define ADC_ARB_USRP_DATA1			0x19C

/*
 * Physical channels which MUST exist on all PM variants in order to provide
 * proper reference points for calibration.
 *
 * @PM8XXX_CHANNEL_INTERNAL: 625mV reference channel
 * @PM8XXX_CHANNEL_125V: 1250mV reference channel
 * @PM8XXX_CHANNEL_INTERNAL_2: 325mV reference channel
 * @PM8XXX_CHANNEL_MUXOFF: channel to reduce input load on mux, apparently also
 * measures XO temperature
 */
#define PM8XXX_CHANNEL_INTERNAL		0x0c
#define PM8XXX_CHANNEL_125V		0x0d
#define PM8XXX_CHANNEL_INTERNAL_2	0x0e
#define PM8XXX_CHANNEL_MUXOFF		0x0f

/*
 * PM8058 AMUX premux scaling, two bits. This is done of the channel before
 * reaching the AMUX.
 */
#define PM8058_AMUX_PRESCALE_0 0x0 /* No scaling on the signal */
#define PM8058_AMUX_PRESCALE_1 0x1 /* Unity scaling selected by the user */
#define PM8058_AMUX_PRESCALE_1_DIV3 0x2 /* 1/3 prescaler on the input */

/* Defines reference voltage for the XOADC */
#define AMUX_RSV0 0x0 /* XO_IN/XOADC_GND, special selection to read XO temp */
#define AMUX_RSV1 0x1 /* PMIC_IN/XOADC_GND */
#define AMUX_RSV2 0x2 /* PMIC_IN/BMS_CSP */
#define AMUX_RSV3 0x3 /* not used */
#define AMUX_RSV4 0x4 /* XOADC_GND/XOADC_GND */
#define AMUX_RSV5 0x5 /* XOADC_VREF/XOADC_GND */
#define XOADC_RSV_MAX 5 /* 3 bits 0..7, 3 and 6,7 are invalid */

/**
 * struct xoadc_channel - encodes channel properties and defaults
 * @datasheet_name: the hardwarename of this channel
 * @pre_scale_mux: prescale (PM8058) or premux (PM8921) for selecting
 * this channel. Both this and the amux channel is needed to uniquely
 * identify a channel. Values 0..3.
 * @amux_channel: value of the ADC_ARB_USRP_AMUX_CNTRL register for this
 * channel, bits 4..7, selects the amux, values 0..f
 * @prescale: the channels have hard-coded prescale ratios defined
 * by the hardware, this tells us what it is
 * @type: corresponding IIO channel type, usually IIO_VOLTAGE or
 * IIO_TEMP
 * @scale_fn_type: the liner interpolation etc to convert the
 * ADC code to the value that IIO expects, in uV or millicelsius
 * etc. This scale function can be pretty elaborate if different
 * thermistors are connected or other hardware characteristics are
 * deployed.
 * @amux_ip_rsv: ratiometric scale value used by the analog muxer: this
 * selects the reference voltage for ratiometric scaling
 */
struct xoadc_channel {
	const char *datasheet_name;
	u8 pre_scale_mux:2;
	u8 amux_channel:4;
	const struct u32_fract prescale;
	enum iio_chan_type type;
	enum vadc_scale_fn_type scale_fn_type;
	u8 amux_ip_rsv:3;
};

/**
 * struct xoadc_variant - encodes the XOADC variant characteristics
 * @name: name of this PMIC variant
 * @channels: the hardware channels and respective settings and defaults
 * @broken_ratiometric: if the PMIC has broken ratiometric scaling (this
 * is a known problem on PM8058)
 * @prescaling: this variant uses AMUX bits 2 & 3 for prescaling (PM8058)
 * @second_level_mux: this variant uses AMUX bits 2 & 3 for a second level
 * mux
 */
struct xoadc_variant {
	const char name[16];
	const struct xoadc_channel *channels;
	bool broken_ratiometric;
	bool prescaling;
	bool second_level_mux;
};

/*
 * XOADC_CHAN macro parameters:
 * _dname: the name of the channel
 * _presmux: prescaler (PM8058) or premux (PM8921) setting for this channel
 * _amux: the value in bits 2..7 of the ADC_ARB_USRP_AMUX_CNTRL register
 * for this channel. On some PMICs some of the bits select a prescaler, and
 * on some PMICs some of the bits select various complex multiplex settings.
 * _type: IIO channel type
 * _prenum: prescaler numerator (dividend)
 * _preden: prescaler denominator (divisor)
 * _scale: scaling function type, this selects how the raw valued is mangled
 * to output the actual processed measurement
 * _amip: analog mux input parent when using ratiometric measurements
 */
#define XOADC_CHAN(_dname, _presmux, _amux, _type, _prenum, _preden, _scale, _amip) \
	{								\
		.datasheet_name = __stringify(_dname),			\
		.pre_scale_mux = _presmux,				\
		.amux_channel = _amux,					\
		.prescale = {						\
			.numerator = _prenum, .denominator = _preden,	\
		},							\
		.type = _type,						\
		.scale_fn_type = _scale,				\
		.amux_ip_rsv = _amip,					\
	}

/*
 * Taken from arch/arm/mach-msm/board-9615.c in the vendor tree:
 * TODO: incomplete, needs testing.
 */
static const struct xoadc_channel pm8018_xoadc_channels[] = {
	XOADC_CHAN(VCOIN, 0x00, 0x00, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(VBAT, 0x00, 0x01, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(VPH_PWR, 0x00, 0x02, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(DIE_TEMP, 0x00, 0x0b, IIO_TEMP, 1, 1, SCALE_PMIC_THERM, AMUX_RSV1),
	/* Used for battery ID or battery temperature */
	XOADC_CHAN(AMUX8, 0x00, 0x08, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV2),
	XOADC_CHAN(INTERNAL, 0x00, 0x0c, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(125V, 0x00, 0x0d, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(MUXOFF, 0x00, 0x0f, IIO_TEMP, 1, 1, SCALE_XOTHERM, AMUX_RSV0),
	{ }, /* Sentinel */
};

/*
 * Taken from arch/arm/mach-msm/board-8930-pmic.c in the vendor tree:
 * TODO: needs testing.
 */
static const struct xoadc_channel pm8038_xoadc_channels[] = {
	XOADC_CHAN(VCOIN, 0x00, 0x00, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(VBAT, 0x00, 0x01, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(DCIN, 0x00, 0x02, IIO_VOLTAGE, 1, 6, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(ICHG, 0x00, 0x03, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(VPH_PWR, 0x00, 0x04, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(AMUX5, 0x00, 0x05, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(AMUX6, 0x00, 0x06, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(AMUX7, 0x00, 0x07, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	/* AMUX8 used for battery temperature in most cases */
	XOADC_CHAN(AMUX8, 0x00, 0x08, IIO_TEMP, 1, 1, SCALE_THERM_100K_PULLUP, AMUX_RSV2),
	XOADC_CHAN(AMUX9, 0x00, 0x09, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(USB_VBUS, 0x00, 0x0a, IIO_VOLTAGE, 1, 4, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(DIE_TEMP, 0x00, 0x0b, IIO_TEMP, 1, 1, SCALE_PMIC_THERM, AMUX_RSV1),
	XOADC_CHAN(INTERNAL, 0x00, 0x0c, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(125V, 0x00, 0x0d, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(INTERNAL_2, 0x00, 0x0e, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(MUXOFF, 0x00, 0x0f, IIO_TEMP, 1, 1, SCALE_XOTHERM, AMUX_RSV0),
	{ }, /* Sentinel */
};

/*
 * This was created by cross-referencing the vendor tree
 * arch/arm/mach-msm/board-msm8x60.c msm_adc_channels_data[]
 * with the "channel types" (first field) to find the right
 * configuration for these channels on an MSM8x60 i.e. PM8058
 * setup.
 */
static const struct xoadc_channel pm8058_xoadc_channels[] = {
	XOADC_CHAN(VCOIN, 0x00, 0x00, IIO_VOLTAGE, 1, 2, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(VBAT, 0x00, 0x01, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(DCIN, 0x00, 0x02, IIO_VOLTAGE, 1, 10, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(ICHG, 0x00, 0x03, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(VPH_PWR, 0x00, 0x04, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	/*
	 * AMUX channels 5 thru 9 are referred to as MPP5 thru MPP9 in
	 * some code and documentation. But they are really just 5
	 * channels just like any other. They are connected to a switching
	 * matrix where they can be routed to any of the MPPs, not just
	 * 1-to-1 onto MPP5 thru 9, so naming them MPP5 thru MPP9 is
	 * very confusing.
	 */
	XOADC_CHAN(AMUX5, 0x00, 0x05, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(AMUX6, 0x00, 0x06, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(AMUX7, 0x00, 0x07, IIO_VOLTAGE, 1, 2, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(AMUX8, 0x00, 0x08, IIO_VOLTAGE, 1, 2, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(AMUX9, 0x00, 0x09, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(USB_VBUS, 0x00, 0x0a, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(DIE_TEMP, 0x00, 0x0b, IIO_TEMP, 1, 1, SCALE_PMIC_THERM, AMUX_RSV1),
	XOADC_CHAN(INTERNAL, 0x00, 0x0c, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(125V, 0x00, 0x0d, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(INTERNAL_2, 0x00, 0x0e, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(MUXOFF, 0x00, 0x0f, IIO_TEMP, 1, 1, SCALE_XOTHERM, AMUX_RSV0),
	/* There are also "unity" and divided by 3 channels (prescaler) but noone is using them */
	{ }, /* Sentinel */
};

/*
 * The PM8921 has some pre-muxing on its channels, this comes from the vendor tree
 * include/linux/mfd/pm8xxx/pm8xxx-adc.h
 * board-flo-pmic.c (Nexus 7) and board-8064-pmic.c
 */
static const struct xoadc_channel pm8921_xoadc_channels[] = {
	XOADC_CHAN(VCOIN, 0x00, 0x00, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(VBAT, 0x00, 0x01, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(DCIN, 0x00, 0x02, IIO_VOLTAGE, 1, 6, SCALE_DEFAULT, AMUX_RSV1),
	/* channel "ICHG" is reserved and not used on PM8921 */
	XOADC_CHAN(VPH_PWR, 0x00, 0x04, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(IBAT, 0x00, 0x05, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	/* CHAN 6 & 7 (MPP1 & MPP2) are reserved for MPP channels on PM8921 */
	XOADC_CHAN(BATT_THERM, 0x00, 0x08, IIO_TEMP, 1, 1, SCALE_THERM_100K_PULLUP, AMUX_RSV1),
	XOADC_CHAN(BATT_ID, 0x00, 0x09, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(USB_VBUS, 0x00, 0x0a, IIO_VOLTAGE, 1, 4, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(DIE_TEMP, 0x00, 0x0b, IIO_TEMP, 1, 1, SCALE_PMIC_THERM, AMUX_RSV1),
	XOADC_CHAN(INTERNAL, 0x00, 0x0c, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(125V, 0x00, 0x0d, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	/* FIXME: look into the scaling of this temperature */
	XOADC_CHAN(CHG_TEMP, 0x00, 0x0e, IIO_TEMP, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(MUXOFF, 0x00, 0x0f, IIO_TEMP, 1, 1, SCALE_XOTHERM, AMUX_RSV0),
	/* The following channels have premux bit 0 set to 1 (all end in 4) */
	XOADC_CHAN(ATEST_8, 0x01, 0x00, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	/* Set scaling to 1/2 based on the name for these two */
	XOADC_CHAN(USB_SNS_DIV20, 0x01, 0x01, IIO_VOLTAGE, 1, 2, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(DCIN_SNS_DIV20, 0x01, 0x02, IIO_VOLTAGE, 1, 2, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(AMUX3, 0x01, 0x03, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(AMUX4, 0x01, 0x04, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(AMUX5, 0x01, 0x05, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(AMUX6, 0x01, 0x06, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(AMUX7, 0x01, 0x07, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(AMUX8, 0x01, 0x08, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	/* Internal test signals, I think */
	XOADC_CHAN(ATEST_1, 0x01, 0x09, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(ATEST_2, 0x01, 0x0a, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(ATEST_3, 0x01, 0x0b, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(ATEST_4, 0x01, 0x0c, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(ATEST_5, 0x01, 0x0d, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(ATEST_6, 0x01, 0x0e, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(ATEST_7, 0x01, 0x0f, IIO_VOLTAGE, 1, 1, SCALE_DEFAULT, AMUX_RSV1),
	/* The following channels have premux bit 1 set to 1 (all end in 8) */
	/* I guess even ATEST8 will be divided by 3 here */
	XOADC_CHAN(ATEST_8, 0x02, 0x00, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	/* I guess div 2 div 3 becomes div 6 */
	XOADC_CHAN(USB_SNS_DIV20_DIV3, 0x02, 0x01, IIO_VOLTAGE, 1, 6, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(DCIN_SNS_DIV20_DIV3, 0x02, 0x02, IIO_VOLTAGE, 1, 6, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(AMUX3_DIV3, 0x02, 0x03, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(AMUX4_DIV3, 0x02, 0x04, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(AMUX5_DIV3, 0x02, 0x05, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(AMUX6_DIV3, 0x02, 0x06, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(AMUX7_DIV3, 0x02, 0x07, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(AMUX8_DIV3, 0x02, 0x08, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(ATEST_1_DIV3, 0x02, 0x09, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(ATEST_2_DIV3, 0x02, 0x0a, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(ATEST_3_DIV3, 0x02, 0x0b, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(ATEST_4_DIV3, 0x02, 0x0c, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(ATEST_5_DIV3, 0x02, 0x0d, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(ATEST_6_DIV3, 0x02, 0x0e, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	XOADC_CHAN(ATEST_7_DIV3, 0x02, 0x0f, IIO_VOLTAGE, 1, 3, SCALE_DEFAULT, AMUX_RSV1),
	{ }, /* Sentinel */
};

/**
 * struct pm8xxx_chan_info - ADC channel information
 * @name: name of this channel
 * @hwchan: pointer to hardware channel information (muxing & scaling settings)
 * @calibration: whether to use absolute or ratiometric calibration
 * @decimation: 0,1,2,3
 * @amux_ip_rsv: ratiometric scale value if using ratiometric
 * calibration: 0, 1, 2, 4, 5.
 */
struct pm8xxx_chan_info {
	const char *name;
	const struct xoadc_channel *hwchan;
	enum vadc_calibration calibration;
	u8 decimation:2;
	u8 amux_ip_rsv:3;
};

/**
 * struct pm8xxx_xoadc - state container for the XOADC
 * @dev: pointer to device
 * @map: regmap to access registers
 * @variant: XOADC variant characteristics
 * @vref: reference voltage regulator
 * characteristics of the channels, and sensible default settings
 * @nchans: number of channels, configured by the device tree
 * @chans: the channel information per-channel, configured by the device tree
 * @iio_chans: IIO channel specifiers
 * @graph: linear calibration parameters for absolute and
 * ratiometric measurements
 * @complete: completion to indicate end of conversion
 * @lock: lock to restrict access to the hardware to one client at the time
 */
struct pm8xxx_xoadc {
	struct device *dev;
	struct regmap *map;
	const struct xoadc_variant *variant;
	struct regulator *vref;
	unsigned int nchans;
	struct pm8xxx_chan_info *chans;
	struct iio_chan_spec *iio_chans;
	struct vadc_linear_graph graph[2];
	struct completion complete;
	struct mutex lock;
};

static irqreturn_t pm8xxx_eoc_irq(int irq, void *d)
{
	struct iio_dev *indio_dev = d;
	struct pm8xxx_xoadc *adc = iio_priv(indio_dev);

	complete(&adc->complete);

	return IRQ_HANDLED;
}

static struct pm8xxx_chan_info *
pm8xxx_get_channel(struct pm8xxx_xoadc *adc, u8 chan)
{
	int i;

	for (i = 0; i < adc->nchans; i++) {
		struct pm8xxx_chan_info *ch = &adc->chans[i];
		if (ch->hwchan->amux_channel == chan)
			return ch;
	}
	return NULL;
}

static int pm8xxx_read_channel_rsv(struct pm8xxx_xoadc *adc,
				   const struct pm8xxx_chan_info *ch,
				   u8 rsv, u16 *adc_code,
				   bool force_ratiometric)
{
	int ret;
	unsigned int val;
	u8 rsvmask, rsvval;
	u8 lsb, msb;

	dev_dbg(adc->dev, "read channel \"%s\", amux %d, prescale/mux: %d, rsv %d\n",
		ch->name, ch->hwchan->amux_channel, ch->hwchan->pre_scale_mux, rsv);

	mutex_lock(&adc->lock);

	/* Mux in this channel */
	val = ch->hwchan->amux_channel << ADC_AMUX_SEL_SHIFT;
	val |= ch->hwchan->pre_scale_mux << ADC_AMUX_PREMUX_SHIFT;
	ret = regmap_write(adc->map, ADC_ARB_USRP_AMUX_CNTRL, val);
	if (ret)
		goto unlock;

	/* Set up ratiometric scale value, mask off all bits except these */
	rsvmask = (ADC_ARB_USRP_RSV_RST | ADC_ARB_USRP_RSV_DTEST0 |
		   ADC_ARB_USRP_RSV_DTEST1 | ADC_ARB_USRP_RSV_OP);
	if (adc->variant->broken_ratiometric && !force_ratiometric) {
		/*
		 * Apparently the PM8058 has some kind of bug which is
		 * reflected in the vendor tree drivers/misc/pmix8058-xoadc.c
		 * which just hardcodes the RSV selector to SEL1 (0x20) for
		 * most cases and SEL0 (0x10) for the MUXOFF channel only.
		 * If we force ratiometric (currently only done when attempting
		 * to do ratiometric calibration) this doesn't seem to work
		 * very well and I suspect ratiometric conversion is simply
		 * broken or not supported on the PM8058.
		 *
		 * Maybe IO_SEL2 doesn't exist on PM8058 and bits 4 & 5 select
		 * the mode alone.
		 *
		 * Some PM8058 register documentation would be nice to get
		 * this right.
		 */
		if (ch->hwchan->amux_channel == PM8XXX_CHANNEL_MUXOFF)
			rsvval = ADC_ARB_USRP_RSV_IP_SEL0;
		else
			rsvval = ADC_ARB_USRP_RSV_IP_SEL1;
	} else {
		if (rsv == 0xff)
			rsvval = (ch->amux_ip_rsv << ADC_RSV_IP_SEL_SHIFT) |
				ADC_ARB_USRP_RSV_TRM;
		else
			rsvval = (rsv << ADC_RSV_IP_SEL_SHIFT) |
				ADC_ARB_USRP_RSV_TRM;
	}

	ret = regmap_update_bits(adc->map,
				 ADC_ARB_USRP_RSV,
				 ~rsvmask,
				 rsvval);
	if (ret)
		goto unlock;

	ret = regmap_write(adc->map, ADC_ARB_USRP_ANA_PARAM,
			   ADC_ARB_USRP_ANA_PARAM_DIS);
	if (ret)
		goto unlock;

	/* Decimation factor */
	ret = regmap_write(adc->map, ADC_ARB_USRP_DIG_PARAM,
			   ADC_ARB_USRP_DIG_PARAM_SEL_SHIFT0 |
			   ADC_ARB_USRP_DIG_PARAM_SEL_SHIFT1 |
			   ch->decimation << ADC_DIG_PARAM_DEC_SHIFT);
	if (ret)
		goto unlock;

	ret = regmap_write(adc->map, ADC_ARB_USRP_ANA_PARAM,
			   ADC_ARB_USRP_ANA_PARAM_EN);
	if (ret)
		goto unlock;

	/* Enable the arbiter, the Qualcomm code does it twice like this */
	ret = regmap_write(adc->map, ADC_ARB_USRP_CNTRL,
			   ADC_ARB_USRP_CNTRL_EN_ARB);
	if (ret)
		goto unlock;
	ret = regmap_write(adc->map, ADC_ARB_USRP_CNTRL,
			   ADC_ARB_USRP_CNTRL_EN_ARB);
	if (ret)
		goto unlock;


	/* Fire a request! */
	reinit_completion(&adc->complete);
	ret = regmap_write(adc->map, ADC_ARB_USRP_CNTRL,
			   ADC_ARB_USRP_CNTRL_EN_ARB |
			   ADC_ARB_USRP_CNTRL_REQ);
	if (ret)
		goto unlock;

	/* Next the interrupt occurs */
	ret = wait_for_completion_timeout(&adc->complete,
					  VADC_CONV_TIME_MAX_US);
	if (!ret) {
		dev_err(adc->dev, "conversion timed out\n");
		ret = -ETIMEDOUT;
		goto unlock;
	}

	ret = regmap_read(adc->map, ADC_ARB_USRP_DATA0, &val);
	if (ret)
		goto unlock;
	lsb = val;
	ret = regmap_read(adc->map, ADC_ARB_USRP_DATA1, &val);
	if (ret)
		goto unlock;
	msb = val;
	*adc_code = (msb << 8) | lsb;

	/* Turn off the ADC by setting the arbiter to 0 twice */
	ret = regmap_write(adc->map, ADC_ARB_USRP_CNTRL, 0);
	if (ret)
		goto unlock;
	ret = regmap_write(adc->map, ADC_ARB_USRP_CNTRL, 0);
	if (ret)
		goto unlock;

unlock:
	mutex_unlock(&adc->lock);
	return ret;
}

static int pm8xxx_read_channel(struct pm8xxx_xoadc *adc,
			       const struct pm8xxx_chan_info *ch,
			       u16 *adc_code)
{
	/*
	 * Normally we just use the ratiometric scale value (RSV) predefined
	 * for the channel, but during calibration we need to modify this
	 * so this wrapper is a helper hiding the more complex version.
	 */
	return pm8xxx_read_channel_rsv(adc, ch, 0xff, adc_code, false);
}

static int pm8xxx_calibrate_device(struct pm8xxx_xoadc *adc)
{
	const struct pm8xxx_chan_info *ch;
	u16 read_1250v;
	u16 read_0625v;
	u16 read_nomux_rsv5;
	u16 read_nomux_rsv4;
	int ret;

	adc->graph[VADC_CALIB_ABSOLUTE].dx = VADC_ABSOLUTE_RANGE_UV;
	adc->graph[VADC_CALIB_RATIOMETRIC].dx = VADC_RATIOMETRIC_RANGE;

	/* Common reference channel calibration */
	ch = pm8xxx_get_channel(adc, PM8XXX_CHANNEL_125V);
	if (!ch)
		return -ENODEV;
	ret = pm8xxx_read_channel(adc, ch, &read_1250v);
	if (ret) {
		dev_err(adc->dev, "could not read 1.25V reference channel\n");
		return -ENODEV;
	}
	ch = pm8xxx_get_channel(adc, PM8XXX_CHANNEL_INTERNAL);
	if (!ch)
		return -ENODEV;
	ret = pm8xxx_read_channel(adc, ch, &read_0625v);
	if (ret) {
		dev_err(adc->dev, "could not read 0.625V reference channel\n");
		return -ENODEV;
	}
	if (read_1250v == read_0625v) {
		dev_err(adc->dev, "read same ADC code for 1.25V and 0.625V\n");
		return -ENODEV;
	}

	adc->graph[VADC_CALIB_ABSOLUTE].dy = read_1250v - read_0625v;
	adc->graph[VADC_CALIB_ABSOLUTE].gnd = read_0625v;

	dev_info(adc->dev, "absolute calibration dx = %d uV, dy = %d units\n",
		 VADC_ABSOLUTE_RANGE_UV, adc->graph[VADC_CALIB_ABSOLUTE].dy);

	/* Ratiometric calibration */
	ch = pm8xxx_get_channel(adc, PM8XXX_CHANNEL_MUXOFF);
	if (!ch)
		return -ENODEV;
	ret = pm8xxx_read_channel_rsv(adc, ch, AMUX_RSV5,
				      &read_nomux_rsv5, true);
	if (ret) {
		dev_err(adc->dev, "could not read MUXOFF reference channel\n");
		return -ENODEV;
	}
	ret = pm8xxx_read_channel_rsv(adc, ch, AMUX_RSV4,
				      &read_nomux_rsv4, true);
	if (ret) {
		dev_err(adc->dev, "could not read MUXOFF reference channel\n");
		return -ENODEV;
	}
	adc->graph[VADC_CALIB_RATIOMETRIC].dy =
		read_nomux_rsv5 - read_nomux_rsv4;
	adc->graph[VADC_CALIB_RATIOMETRIC].gnd = read_nomux_rsv4;

	dev_info(adc->dev, "ratiometric calibration dx = %d, dy = %d units\n",
		 VADC_RATIOMETRIC_RANGE,
		 adc->graph[VADC_CALIB_RATIOMETRIC].dy);

	return 0;
}

static int pm8xxx_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct pm8xxx_xoadc *adc = iio_priv(indio_dev);
	const struct pm8xxx_chan_info *ch;
	u16 adc_code;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		ch = pm8xxx_get_channel(adc, chan->address);
		if (!ch) {
			dev_err(adc->dev, "no such channel %lu\n",
				chan->address);
			return -EINVAL;
		}
		ret = pm8xxx_read_channel(adc, ch, &adc_code);
		if (ret)
			return ret;

		ret = qcom_vadc_scale(ch->hwchan->scale_fn_type,
				      &adc->graph[ch->calibration],
				      &ch->hwchan->prescale,
				      (ch->calibration == VADC_CALIB_ABSOLUTE),
				      adc_code, val);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_RAW:
		ch = pm8xxx_get_channel(adc, chan->address);
		if (!ch) {
			dev_err(adc->dev, "no such channel %lu\n",
				chan->address);
			return -EINVAL;
		}
		ret = pm8xxx_read_channel(adc, ch, &adc_code);
		if (ret)
			return ret;

		*val = (int)adc_code;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int pm8xxx_fwnode_xlate(struct iio_dev *indio_dev,
			       const struct fwnode_reference_args *iiospec)
{
	struct pm8xxx_xoadc *adc = iio_priv(indio_dev);
	u8 pre_scale_mux;
	u8 amux_channel;
	unsigned int i;

	/*
	 * First cell is prescaler or premux, second cell is analog
	 * mux.
	 */
	if (iiospec->nargs != 2) {
		dev_err(&indio_dev->dev, "wrong number of arguments for %pfwP need 2 got %d\n",
			iiospec->fwnode,
			iiospec->nargs);
		return -EINVAL;
	}
	pre_scale_mux = (u8)iiospec->args[0];
	amux_channel = (u8)iiospec->args[1];
	dev_dbg(&indio_dev->dev, "pre scale/mux: %02x, amux: %02x\n",
		pre_scale_mux, amux_channel);

	/* We need to match exactly on the prescale/premux and channel */
	for (i = 0; i < adc->nchans; i++)
		if (adc->chans[i].hwchan->pre_scale_mux == pre_scale_mux &&
		    adc->chans[i].hwchan->amux_channel == amux_channel)
			return i;

	return -EINVAL;
}

static const struct iio_info pm8xxx_xoadc_info = {
	.fwnode_xlate = pm8xxx_fwnode_xlate,
	.read_raw = pm8xxx_read_raw,
};

static int pm8xxx_xoadc_parse_channel(struct device *dev,
				      struct fwnode_handle *fwnode,
				      const struct xoadc_channel *hw_channels,
				      struct iio_chan_spec *iio_chan,
				      struct pm8xxx_chan_info *ch)
{
	const char *name = fwnode_get_name(fwnode);
	const struct xoadc_channel *hwchan;
	u32 pre_scale_mux, amux_channel, reg[2];
	u32 rsv, dec;
	int ret;
	int chid;

	ret = fwnode_property_read_u32_array(fwnode, "reg", reg,
					     ARRAY_SIZE(reg));
	if (ret) {
		dev_err(dev, "invalid pre scale/mux or amux channel number %s\n",
			name);
		return ret;
	}

	pre_scale_mux = reg[0];
	amux_channel = reg[1];

	/* Find the right channel setting */
	chid = 0;
	hwchan = &hw_channels[0];
	while (hwchan->datasheet_name) {
		if (hwchan->pre_scale_mux == pre_scale_mux &&
		    hwchan->amux_channel == amux_channel)
			break;
		hwchan++;
		chid++;
	}
	/* The sentinel does not have a name assigned */
	if (!hwchan->datasheet_name) {
		dev_err(dev, "could not locate channel %02x/%02x\n",
			pre_scale_mux, amux_channel);
		return -EINVAL;
	}
	ch->name = name;
	ch->hwchan = hwchan;
	/* Everyone seems to use absolute calibration except in special cases */
	ch->calibration = VADC_CALIB_ABSOLUTE;
	/* Everyone seems to use default ("type 2") decimation */
	ch->decimation = VADC_DEF_DECIMATION;

	if (!fwnode_property_read_u32(fwnode, "qcom,ratiometric", &rsv)) {
		ch->calibration = VADC_CALIB_RATIOMETRIC;
		if (rsv > XOADC_RSV_MAX) {
			dev_err(dev, "%s too large RSV value %d\n", name, rsv);
			return -EINVAL;
		}
		if (rsv == AMUX_RSV3) {
			dev_err(dev, "%s invalid RSV value %d\n", name, rsv);
			return -EINVAL;
		}
	}

	/* Optional decimation, if omitted we use the default */
	ret = fwnode_property_read_u32(fwnode, "qcom,decimation", &dec);
	if (!ret) {
		ret = qcom_vadc_decimation_from_dt(dec);
		if (ret < 0) {
			dev_err(dev, "%s invalid decimation %d\n",
				name, dec);
			return ret;
		}
		ch->decimation = ret;
	}

	iio_chan->channel = chid;
	iio_chan->address = hwchan->amux_channel;
	iio_chan->datasheet_name = hwchan->datasheet_name;
	iio_chan->type = hwchan->type;
	/* All channels are raw or processed */
	iio_chan->info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_PROCESSED);
	iio_chan->indexed = 1;

	dev_dbg(dev,
		"channel [PRESCALE/MUX: %02x AMUX: %02x] \"%s\" ref voltage: %d, decimation %d prescale %d/%d, scale function %d\n",
		hwchan->pre_scale_mux, hwchan->amux_channel, ch->name,
		ch->amux_ip_rsv, ch->decimation, hwchan->prescale.numerator,
		hwchan->prescale.denominator, hwchan->scale_fn_type);

	return 0;
}

static int pm8xxx_xoadc_parse_channels(struct pm8xxx_xoadc *adc)
{
	struct pm8xxx_chan_info *ch;
	int ret;
	int i;

	adc->nchans = device_get_child_node_count(adc->dev);
	if (!adc->nchans) {
		dev_err(adc->dev, "no channel children\n");
		return -ENODEV;
	}
	dev_dbg(adc->dev, "found %d ADC channels\n", adc->nchans);

	adc->iio_chans = devm_kcalloc(adc->dev, adc->nchans,
				      sizeof(*adc->iio_chans), GFP_KERNEL);
	if (!adc->iio_chans)
		return -ENOMEM;

	adc->chans = devm_kcalloc(adc->dev, adc->nchans,
				  sizeof(*adc->chans), GFP_KERNEL);
	if (!adc->chans)
		return -ENOMEM;

	i = 0;
	device_for_each_child_node_scoped(adc->dev, child) {
		ch = &adc->chans[i];
		ret = pm8xxx_xoadc_parse_channel(adc->dev, child,
						 adc->variant->channels,
						 &adc->iio_chans[i],
						 ch);
		if (ret)
			return ret;

		i++;
	}

	/* Check for required channels */
	ch = pm8xxx_get_channel(adc, PM8XXX_CHANNEL_125V);
	if (!ch) {
		dev_err(adc->dev, "missing 1.25V reference channel\n");
		return -ENODEV;
	}
	ch = pm8xxx_get_channel(adc, PM8XXX_CHANNEL_INTERNAL);
	if (!ch) {
		dev_err(adc->dev, "missing 0.625V reference channel\n");
		return -ENODEV;
	}
	ch = pm8xxx_get_channel(adc, PM8XXX_CHANNEL_MUXOFF);
	if (!ch) {
		dev_err(adc->dev, "missing MUXOFF reference channel\n");
		return -ENODEV;
	}

	return 0;
}

static int pm8xxx_xoadc_probe(struct platform_device *pdev)
{
	const struct xoadc_variant *variant;
	struct pm8xxx_xoadc *adc;
	struct iio_dev *indio_dev;
	struct regmap *map;
	struct device *dev = &pdev->dev;
	int ret;

	variant = device_get_match_data(dev);
	if (!variant)
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;
	platform_set_drvdata(pdev, indio_dev);

	adc = iio_priv(indio_dev);
	adc->dev = dev;
	adc->variant = variant;
	init_completion(&adc->complete);
	mutex_init(&adc->lock);

	ret = pm8xxx_xoadc_parse_channels(adc);
	if (ret)
		return ret;

	map = dev_get_regmap(dev->parent, NULL);
	if (!map) {
		dev_err(dev, "parent regmap unavailable.\n");
		return -ENODEV;
	}
	adc->map = map;

	/* Bring up regulator */
	adc->vref = devm_regulator_get(dev, "xoadc-ref");
	if (IS_ERR(adc->vref))
		return dev_err_probe(dev, PTR_ERR(adc->vref),
				     "failed to get XOADC VREF regulator\n");
	ret = regulator_enable(adc->vref);
	if (ret) {
		dev_err(dev, "failed to enable XOADC VREF regulator\n");
		return ret;
	}

	ret = devm_request_threaded_irq(dev, platform_get_irq(pdev, 0),
			pm8xxx_eoc_irq, NULL, 0, variant->name, indio_dev);
	if (ret) {
		dev_err(dev, "unable to request IRQ\n");
		goto out_disable_vref;
	}

	indio_dev->name = variant->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &pm8xxx_xoadc_info;
	indio_dev->channels = adc->iio_chans;
	indio_dev->num_channels = adc->nchans;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto out_disable_vref;

	ret = pm8xxx_calibrate_device(adc);
	if (ret)
		goto out_unreg_device;

	dev_info(dev, "%s XOADC driver enabled\n", variant->name);

	return 0;

out_unreg_device:
	iio_device_unregister(indio_dev);
out_disable_vref:
	regulator_disable(adc->vref);

	return ret;
}

static void pm8xxx_xoadc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct pm8xxx_xoadc *adc = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	regulator_disable(adc->vref);
}

static const struct xoadc_variant pm8018_variant = {
	.name = "PM8018-XOADC",
	.channels = pm8018_xoadc_channels,
};

static const struct xoadc_variant pm8038_variant = {
	.name = "PM8038-XOADC",
	.channels = pm8038_xoadc_channels,
};

static const struct xoadc_variant pm8058_variant = {
	.name = "PM8058-XOADC",
	.channels = pm8058_xoadc_channels,
	.broken_ratiometric = true,
	.prescaling = true,
};

static const struct xoadc_variant pm8921_variant = {
	.name = "PM8921-XOADC",
	.channels = pm8921_xoadc_channels,
	.second_level_mux = true,
};

static const struct of_device_id pm8xxx_xoadc_id_table[] = {
	{
		.compatible = "qcom,pm8018-adc",
		.data = &pm8018_variant,
	},
	{
		.compatible = "qcom,pm8038-adc",
		.data = &pm8038_variant,
	},
	{
		.compatible = "qcom,pm8058-adc",
		.data = &pm8058_variant,
	},
	{
		.compatible = "qcom,pm8921-adc",
		.data = &pm8921_variant,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, pm8xxx_xoadc_id_table);

static struct platform_driver pm8xxx_xoadc_driver = {
	.driver		= {
		.name	= "pm8xxx-adc",
		.of_match_table = pm8xxx_xoadc_id_table,
	},
	.probe		= pm8xxx_xoadc_probe,
	.remove		= pm8xxx_xoadc_remove,
};
module_platform_driver(pm8xxx_xoadc_driver);

MODULE_DESCRIPTION("PM8xxx XOADC driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:pm8xxx-xoadc");
