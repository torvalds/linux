// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MT6359 PMIC AUXADC IIO driver
 *
 * Copyright (c) 2021 MediaTek Inc.
 * Copyright (c) 2024 Collabora Ltd
 * Author: AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include <linux/iio/iio.h>

#include <linux/mfd/mt6397/core.h>

#include <dt-bindings/iio/adc/mediatek,mt6357-auxadc.h>
#include <dt-bindings/iio/adc/mediatek,mt6358-auxadc.h>
#include <dt-bindings/iio/adc/mediatek,mt6359-auxadc.h>
#include <dt-bindings/iio/adc/mediatek,mt6363-auxadc.h>

#define AUXADC_AVG_TIME_US		10
#define AUXADC_POLL_DELAY_US		100
#define AUXADC_TIMEOUT_US		32000
#define IMP_STOP_DELAY_US		150
#define IMP_POLL_DELAY_US		1000

/* For PMIC_RG_RESET_VAL and MT6358_IMP0_CLEAR, the bits specific purpose is unknown. */
#define PMIC_RG_RESET_VAL		(BIT(0) | BIT(3))
#define PMIC_AUXADC_RDY_BIT		BIT(15)
#define MT6357_IMP_ADC_NUM		30
#define MT6358_IMP_ADC_NUM		28

#define MT6358_DCM_CK_SW_EN		GENMASK(1, 0)
#define MT6358_IMP0_CLEAR		(BIT(14) | BIT(7))
#define MT6358_IMP0_IRQ_RDY		BIT(8)
#define MT6358_IMP1_AUTOREPEAT_EN	BIT(15)

#define MT6359_IMP0_CONV_EN		BIT(0)
#define MT6359_IMP1_IRQ_RDY		BIT(15)

#define MT6363_EXT_CHAN_MASK		GENMASK(2, 0)
#define MT6363_EXT_PURES_MASK		GENMASK(4, 3)
  #define MT6363_PULLUP_RES_100K	0
  #define MT6363_PULLUP_RES_30K		1
  #define MT6363_PULLUP_RES_OPEN	3

enum mtk_pmic_auxadc_regs {
	PMIC_AUXADC_ADC0,
	PMIC_AUXADC_DCM_CON,
	PMIC_AUXADC_IMP0,
	PMIC_AUXADC_IMP1,
	PMIC_AUXADC_IMP3,
	PMIC_AUXADC_RQST0,
	PMIC_AUXADC_RQST1,
	PMIC_AUXADC_RQST3,
	PMIC_AUXADC_SDMADC_CON0,
	PMIC_HK_TOP_WKEY,
	PMIC_HK_TOP_RST_CON0,
	PMIC_FGADC_R_CON0,
	PMIC_AUXADC_REGS_MAX
};

enum mtk_pmic_auxadc_channels {
	PMIC_AUXADC_CHAN_BATADC,
	PMIC_AUXADC_CHAN_ISENSE,
	PMIC_AUXADC_CHAN_VCDT,
	PMIC_AUXADC_CHAN_BAT_TEMP,
	PMIC_AUXADC_CHAN_BATID,
	PMIC_AUXADC_CHAN_CHIP_TEMP,
	PMIC_AUXADC_CHAN_VCORE_TEMP,
	PMIC_AUXADC_CHAN_VPROC_TEMP,
	PMIC_AUXADC_CHAN_VGPU_TEMP,
	PMIC_AUXADC_CHAN_ACCDET,
	PMIC_AUXADC_CHAN_VDCXO,
	PMIC_AUXADC_CHAN_TSX_TEMP,
	PMIC_AUXADC_CHAN_HPOFS_CAL,
	PMIC_AUXADC_CHAN_DCXO_TEMP,
	PMIC_AUXADC_CHAN_VTREF,
	PMIC_AUXADC_CHAN_VBIF,
	PMIC_AUXADC_CHAN_VSYSSNS,
	PMIC_AUXADC_CHAN_VIN1,
	PMIC_AUXADC_CHAN_VIN2,
	PMIC_AUXADC_CHAN_VIN3,
	PMIC_AUXADC_CHAN_VIN4,
	PMIC_AUXADC_CHAN_VIN5,
	PMIC_AUXADC_CHAN_VIN6,
	PMIC_AUXADC_CHAN_VIN7,
	PMIC_AUXADC_CHAN_IBAT,
	PMIC_AUXADC_CHAN_VBAT,
	PMIC_AUXADC_CHAN_MAX
};

/**
 * struct mt6359_auxadc - Main driver structure
 * @dev:           Device pointer
 * @regmap:        Regmap from SoC PMIC Wrapper
 * @chip_info:     PMIC specific chip info
 * @lock:          Mutex to serialize AUXADC reading vs configuration
 * @timed_out:     Signals whether the last read timed out
 */
struct mt6359_auxadc {
	struct device *dev;
	struct regmap *regmap;
	const struct mtk_pmic_auxadc_info *chip_info;
	struct mutex lock;
	bool timed_out;
};

/**
 * struct mtk_pmic_auxadc_chan - PMIC AUXADC channel data
 * @req_idx:       Request register number
 * @req_mask:      Bitmask to activate a channel
 * @rdy_idx:       Readiness register number
 * @rdy_mask:      Bitmask to determine channel readiness
 * @ext_sel_idx:   PMIC GPIO channel register number
 * @ext_sel_ch:    PMIC GPIO number
 * @ext_sel_pu:    PMIC GPIO channel pullup resistor selector
 * @num_samples:   Number of AUXADC samples for averaging
 * @r_ratio:       Resistance ratio fractional
 */
struct mtk_pmic_auxadc_chan {
	u8 req_idx;
	u16 req_mask;
	u8 rdy_idx;
	u16 rdy_mask;
	s8 ext_sel_idx;
	u8 ext_sel_ch;
	u8 ext_sel_pu;
	u16 num_samples;
	struct u8_fract r_ratio;
};

/**
 * struct mtk_pmic_auxadc_info - PMIC specific chip info
 * @model_name:     PMIC model name
 * @channels:       IIO specification of ADC channels
 * @num_channels:   Number of ADC channels
 * @desc:           PMIC AUXADC channel data
 * @regs:           List of PMIC specific registers
 * @sec_unlock_key: Security unlock key for HK_TOP writes
 * @vref_mV:        AUXADC Reference Voltage (VREF) in millivolts
 * @imp_adc_num:    ADC channel for battery impedance readings
 * @is_spmi:        Defines whether this PMIC communicates over SPMI
 * @no_reset:       If true, this PMIC does not support ADC reset
 * @read_imp:       Callback to read impedance channels
 */
struct mtk_pmic_auxadc_info {
	const char *model_name;
	const struct iio_chan_spec *channels;
	u8 num_channels;
	const struct mtk_pmic_auxadc_chan *desc;
	const u16 *regs;
	u16 sec_unlock_key;
	u32 vref_mV;
	u8 imp_adc_num;
	bool is_spmi;
	bool no_reset;
	int (*read_imp)(struct mt6359_auxadc *adc_dev,
			const struct iio_chan_spec *chan, int *vbat, int *ibat);
};

#define MTK_PMIC_ADC_EXT_CHAN(_ch_idx, _req_idx, _req_bit, _rdy_idx, _rdy_bit,	\
			      _ext_sel_idx, _ext_sel_ch, _ext_sel_pu,		\
			      _samples, _rnum, _rdiv)				\
	[PMIC_AUXADC_CHAN_##_ch_idx] = {					\
		.req_idx = _req_idx,						\
		.req_mask = BIT(_req_bit),					\
		.rdy_idx = _rdy_idx,						\
		.rdy_mask = BIT(_rdy_bit),					\
		.ext_sel_idx = _ext_sel_idx,					\
		.ext_sel_ch = _ext_sel_ch,					\
		.ext_sel_pu = _ext_sel_pu,					\
		.num_samples = _samples,					\
		.r_ratio = { _rnum, _rdiv }					\
	}

#define MTK_PMIC_ADC_CHAN(_ch_idx, _req_idx, _req_bit, _rdy_idx, _rdy_bit,	\
			  _samples, _rnum, _rdiv)				\
	MTK_PMIC_ADC_EXT_CHAN(_ch_idx, _req_idx, _req_bit, _rdy_idx, _rdy_bit,	\
			      -1, 0, 0, _samples, _rnum, _rdiv)

#define MTK_PMIC_IIO_CHAN(_model, _name, _ch_idx, _adc_idx, _nbits, _ch_type)	\
{										\
	.type = _ch_type,							\
	.channel = _model##_AUXADC_##_ch_idx,					\
	.address = _adc_idx,							\
	.scan_index = PMIC_AUXADC_CHAN_##_ch_idx,				\
	.datasheet_name = __stringify(_name),					\
	.scan_type =  {								\
		.sign = 'u',							\
		.realbits = _nbits,						\
		.storagebits = 16,						\
		.endianness = IIO_CPU						\
	},									\
	.indexed = 1,								\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE)	\
}

static const struct iio_chan_spec mt6357_auxadc_channels[] = {
	MTK_PMIC_IIO_CHAN(MT6357, bat_adc, BATADC, 0, 15, IIO_RESISTANCE),
	MTK_PMIC_IIO_CHAN(MT6357, isense, ISENSE, 1, 12, IIO_CURRENT),
	MTK_PMIC_IIO_CHAN(MT6357, cdt_v, VCDT, 2, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6357, batt_temp, BAT_TEMP, 3, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6357, chip_temp, CHIP_TEMP, 4, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6357, acc_det, ACCDET, 5, 12, IIO_RESISTANCE),
	MTK_PMIC_IIO_CHAN(MT6357, dcxo_v, VDCXO, 6, 12, IIO_VOLTAGE),
	MTK_PMIC_IIO_CHAN(MT6357, tsx_temp, TSX_TEMP, 7, 15, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6357, hp_ofs_cal, HPOFS_CAL, 9, 15, IIO_RESISTANCE),
	MTK_PMIC_IIO_CHAN(MT6357, dcxo_temp, DCXO_TEMP, 36, 15, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6357, vcore_temp, VCORE_TEMP, 40, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6357, vproc_temp, VPROC_TEMP, 41, 12, IIO_TEMP),

	/* Battery impedance channels */
	MTK_PMIC_IIO_CHAN(MT6357, batt_v, VBAT, 0, 15, IIO_VOLTAGE),
};

static const struct mtk_pmic_auxadc_chan mt6357_auxadc_ch_desc[] = {
	MTK_PMIC_ADC_CHAN(BATADC, PMIC_AUXADC_RQST0, 0, PMIC_AUXADC_IMP0, 8, 128, 3, 1),
	MTK_PMIC_ADC_CHAN(ISENSE, PMIC_AUXADC_RQST0, 0, PMIC_AUXADC_IMP0, 8, 128, 3, 1),
	MTK_PMIC_ADC_CHAN(VCDT, PMIC_AUXADC_RQST0, 0, PMIC_AUXADC_IMP0, 8, 8, 1, 1),
	MTK_PMIC_ADC_CHAN(BAT_TEMP, PMIC_AUXADC_RQST0, 3, PMIC_AUXADC_IMP0, 8, 8, 1, 1),
	MTK_PMIC_ADC_CHAN(CHIP_TEMP, PMIC_AUXADC_RQST0, 4, PMIC_AUXADC_IMP0, 8, 8, 1, 1),
	MTK_PMIC_ADC_CHAN(ACCDET, PMIC_AUXADC_RQST0, 5, PMIC_AUXADC_IMP0, 8, 8, 1, 1),
	MTK_PMIC_ADC_CHAN(TSX_TEMP, PMIC_AUXADC_RQST0, 7, PMIC_AUXADC_IMP0, 8, 128, 1, 1),
	MTK_PMIC_ADC_CHAN(HPOFS_CAL, PMIC_AUXADC_RQST0, 9, PMIC_AUXADC_IMP0, 8, 256, 1, 1),
	MTK_PMIC_ADC_CHAN(DCXO_TEMP, PMIC_AUXADC_RQST0, 10, PMIC_AUXADC_IMP0, 8, 16, 1, 1),
	MTK_PMIC_ADC_CHAN(VBIF, PMIC_AUXADC_RQST0, 11, PMIC_AUXADC_IMP0, 8, 8, 1, 1),
	MTK_PMIC_ADC_CHAN(VCORE_TEMP, PMIC_AUXADC_RQST1, 5, PMIC_AUXADC_IMP0, 8, 8, 1, 1),
	MTK_PMIC_ADC_CHAN(VPROC_TEMP, PMIC_AUXADC_RQST1, 6, PMIC_AUXADC_IMP0, 8, 8, 1, 1),

	/* Battery impedance channels */
	MTK_PMIC_ADC_CHAN(VBAT, 0, 0, PMIC_AUXADC_IMP0, 8, 128, 3, 1),
};

static const u16 mt6357_auxadc_regs[] = {
	[PMIC_HK_TOP_RST_CON0]	= 0x0f90,
	[PMIC_AUXADC_DCM_CON]	= 0x122e,
	[PMIC_AUXADC_ADC0]	= 0x1088,
	[PMIC_AUXADC_IMP0]	= 0x119c,
	[PMIC_AUXADC_IMP1]	= 0x119e,
	[PMIC_AUXADC_RQST0]	= 0x110e,
	[PMIC_AUXADC_RQST1]	= 0x1114,
};

static const struct iio_chan_spec mt6358_auxadc_channels[] = {
	MTK_PMIC_IIO_CHAN(MT6358, bat_adc, BATADC, 0, 15, IIO_RESISTANCE),
	MTK_PMIC_IIO_CHAN(MT6358, cdt_v, VCDT, 2, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6358, batt_temp, BAT_TEMP, 3, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6358, chip_temp, CHIP_TEMP, 4, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6358, acc_det, ACCDET, 5, 12, IIO_RESISTANCE),
	MTK_PMIC_IIO_CHAN(MT6358, dcxo_v, VDCXO, 6, 12, IIO_VOLTAGE),
	MTK_PMIC_IIO_CHAN(MT6358, tsx_temp, TSX_TEMP, 7, 15, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6358, hp_ofs_cal, HPOFS_CAL, 9, 15, IIO_RESISTANCE),
	MTK_PMIC_IIO_CHAN(MT6358, dcxo_temp, DCXO_TEMP, 10, 15, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6358, bif_v, VBIF, 11, 12, IIO_VOLTAGE),
	MTK_PMIC_IIO_CHAN(MT6358, vcore_temp, VCORE_TEMP, 38, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6358, vproc_temp, VPROC_TEMP, 39, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6358, vgpu_temp, VGPU_TEMP, 40, 12, IIO_TEMP),

	/* Battery impedance channels */
	MTK_PMIC_IIO_CHAN(MT6358, batt_v, VBAT, 0, 15, IIO_VOLTAGE),
};

static const struct mtk_pmic_auxadc_chan mt6358_auxadc_ch_desc[] = {
	MTK_PMIC_ADC_CHAN(BATADC, PMIC_AUXADC_RQST0, 0, PMIC_AUXADC_IMP0, 8, 128, 3, 1),
	MTK_PMIC_ADC_CHAN(VCDT, PMIC_AUXADC_RQST0, 0, PMIC_AUXADC_IMP0, 8, 8, 1, 1),
	MTK_PMIC_ADC_CHAN(BAT_TEMP, PMIC_AUXADC_RQST0, 3, PMIC_AUXADC_IMP0, 8, 8, 2, 1),
	MTK_PMIC_ADC_CHAN(CHIP_TEMP, PMIC_AUXADC_RQST0, 4, PMIC_AUXADC_IMP0, 8, 8, 1, 1),
	MTK_PMIC_ADC_CHAN(ACCDET, PMIC_AUXADC_RQST0, 5, PMIC_AUXADC_IMP0, 8, 8, 1, 1),
	MTK_PMIC_ADC_CHAN(VDCXO, PMIC_AUXADC_RQST0, 6, PMIC_AUXADC_IMP0, 8, 8, 3, 2),
	MTK_PMIC_ADC_CHAN(TSX_TEMP, PMIC_AUXADC_RQST0, 7, PMIC_AUXADC_IMP0, 8, 128, 1, 1),
	MTK_PMIC_ADC_CHAN(HPOFS_CAL, PMIC_AUXADC_RQST0, 9, PMIC_AUXADC_IMP0, 8, 256, 1, 1),
	MTK_PMIC_ADC_CHAN(DCXO_TEMP, PMIC_AUXADC_RQST0, 10, PMIC_AUXADC_IMP0, 8, 16, 1, 1),
	MTK_PMIC_ADC_CHAN(VBIF, PMIC_AUXADC_RQST0, 11, PMIC_AUXADC_IMP0, 8, 8, 2, 1),
	MTK_PMIC_ADC_CHAN(VCORE_TEMP, PMIC_AUXADC_RQST1, 8, PMIC_AUXADC_IMP0, 8, 8, 1, 1),
	MTK_PMIC_ADC_CHAN(VPROC_TEMP, PMIC_AUXADC_RQST1, 9, PMIC_AUXADC_IMP0, 8, 8, 1, 1),
	MTK_PMIC_ADC_CHAN(VGPU_TEMP, PMIC_AUXADC_RQST1, 10, PMIC_AUXADC_IMP0, 8, 8, 1, 1),

	/* Battery impedance channels */
	MTK_PMIC_ADC_CHAN(VBAT, 0, 0, PMIC_AUXADC_IMP0, 8, 128, 7, 2),
};

static const u16 mt6358_auxadc_regs[] = {
	[PMIC_HK_TOP_RST_CON0]	= 0x0f90,
	[PMIC_AUXADC_DCM_CON]	= 0x1260,
	[PMIC_AUXADC_ADC0]	= 0x1088,
	[PMIC_AUXADC_IMP0]	= 0x1208,
	[PMIC_AUXADC_IMP1]	= 0x120a,
	[PMIC_AUXADC_RQST0]	= 0x1108,
	[PMIC_AUXADC_RQST1]	= 0x110a,
};

static const struct iio_chan_spec mt6359_auxadc_channels[] = {
	MTK_PMIC_IIO_CHAN(MT6359, bat_adc, BATADC, 0, 15, IIO_RESISTANCE),
	MTK_PMIC_IIO_CHAN(MT6359, batt_temp, BAT_TEMP, 3, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6359, chip_temp, CHIP_TEMP, 4, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6359, acc_det, ACCDET, 5, 12, IIO_RESISTANCE),
	MTK_PMIC_IIO_CHAN(MT6359, dcxo_v, VDCXO, 6, 12, IIO_VOLTAGE),
	MTK_PMIC_IIO_CHAN(MT6359, tsx_temp, TSX_TEMP, 7, 15, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6359, hp_ofs_cal, HPOFS_CAL, 9, 15, IIO_RESISTANCE),
	MTK_PMIC_IIO_CHAN(MT6359, dcxo_temp, DCXO_TEMP, 10, 15, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6359, bif_v, VBIF, 11, 12, IIO_VOLTAGE),
	MTK_PMIC_IIO_CHAN(MT6359, vcore_temp, VCORE_TEMP, 30, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6359, vproc_temp, VPROC_TEMP, 31, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6359, vgpu_temp, VGPU_TEMP, 32, 12, IIO_TEMP),

	/* Battery impedance channels */
	MTK_PMIC_IIO_CHAN(MT6359, batt_v, VBAT, 0, 15, IIO_VOLTAGE),
	MTK_PMIC_IIO_CHAN(MT6359, batt_i, IBAT, 0, 15, IIO_CURRENT),
};

static const struct mtk_pmic_auxadc_chan mt6359_auxadc_ch_desc[] = {
	MTK_PMIC_ADC_CHAN(BATADC, PMIC_AUXADC_RQST0, 0, PMIC_AUXADC_IMP1, 15, 128, 7, 2),
	MTK_PMIC_ADC_CHAN(BAT_TEMP, PMIC_AUXADC_RQST0, 3, PMIC_AUXADC_IMP1, 15, 8, 5, 2),
	MTK_PMIC_ADC_CHAN(CHIP_TEMP, PMIC_AUXADC_RQST0, 4, PMIC_AUXADC_IMP1, 15, 8, 1, 1),
	MTK_PMIC_ADC_CHAN(ACCDET, PMIC_AUXADC_RQST0, 5, PMIC_AUXADC_IMP1, 15 ,8, 1, 1),
	MTK_PMIC_ADC_CHAN(VDCXO, PMIC_AUXADC_RQST0, 6, PMIC_AUXADC_IMP1, 15, 8, 3, 2),
	MTK_PMIC_ADC_CHAN(TSX_TEMP, PMIC_AUXADC_RQST0, 7, PMIC_AUXADC_IMP1, 15, 128, 1, 1),
	MTK_PMIC_ADC_CHAN(HPOFS_CAL, PMIC_AUXADC_RQST0, 9, PMIC_AUXADC_IMP1, 15, 256, 1, 1),
	MTK_PMIC_ADC_CHAN(DCXO_TEMP, PMIC_AUXADC_RQST0, 10, PMIC_AUXADC_IMP1, 15, 16, 1, 1),
	MTK_PMIC_ADC_CHAN(VBIF, PMIC_AUXADC_RQST0, 11, PMIC_AUXADC_IMP1, 15, 8, 5, 2),
	MTK_PMIC_ADC_CHAN(VCORE_TEMP, PMIC_AUXADC_RQST1, 8, PMIC_AUXADC_IMP1, 15, 8, 1, 1),
	MTK_PMIC_ADC_CHAN(VPROC_TEMP, PMIC_AUXADC_RQST1, 9, PMIC_AUXADC_IMP1, 15, 8, 1, 1),
	MTK_PMIC_ADC_CHAN(VGPU_TEMP, PMIC_AUXADC_RQST1, 10, PMIC_AUXADC_IMP1, 15, 8, 1, 1),

	/* Battery impedance channels */
	MTK_PMIC_ADC_CHAN(VBAT, 0, 0, PMIC_AUXADC_IMP1, 15, 128, 7, 2),
	MTK_PMIC_ADC_CHAN(IBAT, 0, 0, PMIC_AUXADC_IMP1, 15, 128, 7, 2),
};

static const u16 mt6359_auxadc_regs[] = {
	[PMIC_FGADC_R_CON0]	= 0x0d88,
	[PMIC_HK_TOP_WKEY]	= 0x0fb4,
	[PMIC_HK_TOP_RST_CON0]	= 0x0f90,
	[PMIC_AUXADC_RQST0]	= 0x1108,
	[PMIC_AUXADC_RQST1]	= 0x110a,
	[PMIC_AUXADC_ADC0]	= 0x1088,
	[PMIC_AUXADC_IMP0]	= 0x1208,
	[PMIC_AUXADC_IMP1]	= 0x120a,
	[PMIC_AUXADC_IMP3]	= 0x120e,
};

static const struct iio_chan_spec mt6363_auxadc_channels[] = {
	MTK_PMIC_IIO_CHAN(MT6363, bat_adc, BATADC, 0, 15, IIO_RESISTANCE),
	MTK_PMIC_IIO_CHAN(MT6363, cdt_v, VCDT, 2, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6363, batt_temp, BAT_TEMP, 3, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6363, chip_temp, CHIP_TEMP, 4, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6363, sys_sns_v, VSYSSNS, 6, 15, IIO_VOLTAGE),
	MTK_PMIC_IIO_CHAN(MT6363, tref_v, VTREF, 11, 12, IIO_VOLTAGE),
	MTK_PMIC_IIO_CHAN(MT6363, vcore_temp, VCORE_TEMP, 38, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6363, vproc_temp, VPROC_TEMP, 39, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6363, vgpu_temp, VGPU_TEMP, 40, 12, IIO_TEMP),

	/* For VIN, ADC12 holds the result depending on which GPIO was activated */
	MTK_PMIC_IIO_CHAN(MT6363, in1_v, VIN1, 45, 15, IIO_VOLTAGE),
	MTK_PMIC_IIO_CHAN(MT6363, in2_v, VIN2, 45, 15, IIO_VOLTAGE),
	MTK_PMIC_IIO_CHAN(MT6363, in3_v, VIN3, 45, 15, IIO_VOLTAGE),
	MTK_PMIC_IIO_CHAN(MT6363, in4_v, VIN4, 45, 15, IIO_VOLTAGE),
	MTK_PMIC_IIO_CHAN(MT6363, in5_v, VIN5, 45, 15, IIO_VOLTAGE),
	MTK_PMIC_IIO_CHAN(MT6363, in6_v, VIN6, 45, 15, IIO_VOLTAGE),
	MTK_PMIC_IIO_CHAN(MT6363, in7_v, VIN7, 45, 15, IIO_VOLTAGE),
};

static const struct mtk_pmic_auxadc_chan mt6363_auxadc_ch_desc[] = {
	MTK_PMIC_ADC_CHAN(BATADC, PMIC_AUXADC_RQST0, 0, PMIC_AUXADC_ADC0, 15, 64, 4, 1),
	MTK_PMIC_ADC_CHAN(VCDT, PMIC_AUXADC_RQST0, 2, PMIC_AUXADC_ADC0, 15, 32, 1, 1),
	MTK_PMIC_ADC_CHAN(BAT_TEMP, PMIC_AUXADC_RQST0, 3, PMIC_AUXADC_ADC0, 15, 32, 3, 2),
	MTK_PMIC_ADC_CHAN(CHIP_TEMP, PMIC_AUXADC_RQST0, 4, PMIC_AUXADC_ADC0, 15, 32, 1, 1),
	MTK_PMIC_ADC_CHAN(VSYSSNS, PMIC_AUXADC_RQST1, 6, PMIC_AUXADC_ADC0, 15, 64, 3, 1),
	MTK_PMIC_ADC_CHAN(VTREF, PMIC_AUXADC_RQST1, 3, PMIC_AUXADC_ADC0, 15, 32, 3, 2),
	MTK_PMIC_ADC_CHAN(VCORE_TEMP, PMIC_AUXADC_RQST3, 0, PMIC_AUXADC_ADC0, 15, 32, 1, 1),
	MTK_PMIC_ADC_CHAN(VPROC_TEMP, PMIC_AUXADC_RQST3, 1, PMIC_AUXADC_ADC0, 15, 32, 1, 1),
	MTK_PMIC_ADC_CHAN(VGPU_TEMP, PMIC_AUXADC_RQST3, 2, PMIC_AUXADC_ADC0, 15, 32, 1, 1),

	MTK_PMIC_ADC_EXT_CHAN(VIN1,
			      PMIC_AUXADC_RQST1, 4, PMIC_AUXADC_ADC0, 15,
			      PMIC_AUXADC_SDMADC_CON0, 1, MT6363_PULLUP_RES_100K, 32, 1, 1),
	MTK_PMIC_ADC_EXT_CHAN(VIN2,
			      PMIC_AUXADC_RQST1, 4, PMIC_AUXADC_ADC0, 15,
			      PMIC_AUXADC_SDMADC_CON0, 2, MT6363_PULLUP_RES_100K, 32, 1, 1),
	MTK_PMIC_ADC_EXT_CHAN(VIN3,
			      PMIC_AUXADC_RQST1, 4, PMIC_AUXADC_ADC0, 15,
			      PMIC_AUXADC_SDMADC_CON0, 3, MT6363_PULLUP_RES_100K, 32, 1, 1),
	MTK_PMIC_ADC_EXT_CHAN(VIN4,
			      PMIC_AUXADC_RQST1, 4, PMIC_AUXADC_ADC0, 15,
			      PMIC_AUXADC_SDMADC_CON0, 4, MT6363_PULLUP_RES_100K, 32, 1, 1),
	MTK_PMIC_ADC_EXT_CHAN(VIN5,
			      PMIC_AUXADC_RQST1, 4, PMIC_AUXADC_ADC0, 15,
			      PMIC_AUXADC_SDMADC_CON0, 5, MT6363_PULLUP_RES_100K, 32, 1, 1),
	MTK_PMIC_ADC_EXT_CHAN(VIN6,
			      PMIC_AUXADC_RQST1, 4, PMIC_AUXADC_ADC0, 15,
			      PMIC_AUXADC_SDMADC_CON0, 6, MT6363_PULLUP_RES_100K, 32, 1, 1),
	MTK_PMIC_ADC_EXT_CHAN(VIN7,
			      PMIC_AUXADC_RQST1, 4, PMIC_AUXADC_ADC0, 15,
			      PMIC_AUXADC_SDMADC_CON0, 7, MT6363_PULLUP_RES_100K, 32, 1, 1),
};

static const u16 mt6363_auxadc_regs[] = {
	[PMIC_AUXADC_RQST0]	= 0x1108,
	[PMIC_AUXADC_RQST1]	= 0x1109,
	[PMIC_AUXADC_RQST3]	= 0x110c,
	[PMIC_AUXADC_ADC0]	= 0x1088,
	[PMIC_AUXADC_IMP0]	= 0x1208,
	[PMIC_AUXADC_IMP1]	= 0x1209,
};

static const struct iio_chan_spec mt6373_auxadc_channels[] = {
	MTK_PMIC_IIO_CHAN(MT6363, chip_temp, CHIP_TEMP, 4, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6363, vcore_temp, VCORE_TEMP, 38, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6363, vproc_temp, VPROC_TEMP, 39, 12, IIO_TEMP),
	MTK_PMIC_IIO_CHAN(MT6363, vgpu_temp, VGPU_TEMP, 40, 12, IIO_TEMP),

	/* For VIN, ADC12 holds the result depending on which GPIO was activated */
	MTK_PMIC_IIO_CHAN(MT6363, in1_v, VIN1, 45, 15, IIO_VOLTAGE),
	MTK_PMIC_IIO_CHAN(MT6363, in2_v, VIN2, 45, 15, IIO_VOLTAGE),
	MTK_PMIC_IIO_CHAN(MT6363, in3_v, VIN3, 45, 15, IIO_VOLTAGE),
	MTK_PMIC_IIO_CHAN(MT6363, in4_v, VIN4, 45, 15, IIO_VOLTAGE),
	MTK_PMIC_IIO_CHAN(MT6363, in5_v, VIN5, 45, 15, IIO_VOLTAGE),
};

static const struct mtk_pmic_auxadc_chan mt6373_auxadc_ch_desc[] = {
	MTK_PMIC_ADC_CHAN(CHIP_TEMP, PMIC_AUXADC_RQST0, 4, PMIC_AUXADC_ADC0, 15, 32, 1, 1),
	MTK_PMIC_ADC_CHAN(VCORE_TEMP, PMIC_AUXADC_RQST3, 0, PMIC_AUXADC_ADC0, 15, 32, 1, 1),
	MTK_PMIC_ADC_CHAN(VPROC_TEMP, PMIC_AUXADC_RQST3, 1, PMIC_AUXADC_ADC0, 15, 32, 1, 1),
	MTK_PMIC_ADC_CHAN(VGPU_TEMP, PMIC_AUXADC_RQST3, 2, PMIC_AUXADC_ADC0, 15, 32, 1, 1),

	MTK_PMIC_ADC_EXT_CHAN(VIN1,
			      PMIC_AUXADC_RQST1, 4, PMIC_AUXADC_ADC0, 15,
			      PMIC_AUXADC_SDMADC_CON0, 1, MT6363_PULLUP_RES_30K, 32, 1, 1),
	MTK_PMIC_ADC_EXT_CHAN(VIN2,
			      PMIC_AUXADC_RQST1, 4, PMIC_AUXADC_ADC0, 15,
			      PMIC_AUXADC_SDMADC_CON0, 2, MT6363_PULLUP_RES_OPEN, 32, 1, 1),
	MTK_PMIC_ADC_EXT_CHAN(VIN3,
			      PMIC_AUXADC_RQST1, 4, PMIC_AUXADC_ADC0, 15,
			      PMIC_AUXADC_SDMADC_CON0, 3, MT6363_PULLUP_RES_OPEN, 32, 1, 1),
	MTK_PMIC_ADC_EXT_CHAN(VIN4,
			      PMIC_AUXADC_RQST1, 4, PMIC_AUXADC_ADC0, 15,
			      PMIC_AUXADC_SDMADC_CON0, 4, MT6363_PULLUP_RES_OPEN, 32, 1, 1),
	MTK_PMIC_ADC_EXT_CHAN(VIN5,
			      PMIC_AUXADC_RQST1, 4, PMIC_AUXADC_ADC0, 15,
			      PMIC_AUXADC_SDMADC_CON0, 5, MT6363_PULLUP_RES_OPEN, 32, 1, 1),
};

static void mt6358_stop_imp_conv(struct mt6359_auxadc *adc_dev)
{
	const struct mtk_pmic_auxadc_info *cinfo = adc_dev->chip_info;
	struct regmap *regmap = adc_dev->regmap;

	regmap_set_bits(regmap, cinfo->regs[PMIC_AUXADC_IMP0], MT6358_IMP0_CLEAR);
	regmap_clear_bits(regmap, cinfo->regs[PMIC_AUXADC_IMP0], MT6358_IMP0_CLEAR);
	regmap_clear_bits(regmap, cinfo->regs[PMIC_AUXADC_IMP1], MT6358_IMP1_AUTOREPEAT_EN);
	regmap_clear_bits(regmap, cinfo->regs[PMIC_AUXADC_DCM_CON], MT6358_DCM_CK_SW_EN);
}

static int mt6358_start_imp_conv(struct mt6359_auxadc *adc_dev, const struct iio_chan_spec *chan)
{
	const struct mtk_pmic_auxadc_info *cinfo = adc_dev->chip_info;
	const struct mtk_pmic_auxadc_chan *desc = &cinfo->desc[chan->scan_index];
	struct regmap *regmap = adc_dev->regmap;
	u32 val;
	int ret;

	regmap_set_bits(regmap, cinfo->regs[PMIC_AUXADC_DCM_CON], MT6358_DCM_CK_SW_EN);
	regmap_set_bits(regmap, cinfo->regs[PMIC_AUXADC_IMP1], MT6358_IMP1_AUTOREPEAT_EN);

	ret = regmap_read_poll_timeout(regmap, cinfo->regs[desc->rdy_idx],
				       val, val & desc->rdy_mask,
				       IMP_POLL_DELAY_US, AUXADC_TIMEOUT_US);
	if (ret) {
		mt6358_stop_imp_conv(adc_dev);
		return ret;
	}

	return 0;
}

static int mt6358_read_imp(struct mt6359_auxadc *adc_dev,
			   const struct iio_chan_spec *chan, int *vbat, int *ibat)
{
	const struct mtk_pmic_auxadc_info *cinfo = adc_dev->chip_info;
	struct regmap *regmap = adc_dev->regmap;
	u16 reg_adc0 = cinfo->regs[PMIC_AUXADC_ADC0];
	u32 val_v;
	int ret;

	ret = mt6358_start_imp_conv(adc_dev, chan);
	if (ret)
		return ret;

	/* Read the params before stopping */
	regmap_read(regmap, reg_adc0 + (cinfo->imp_adc_num << 1), &val_v);

	mt6358_stop_imp_conv(adc_dev);

	if (vbat)
		*vbat = val_v;
	if (ibat)
		*ibat = 0;

	return 0;
}

static int mt6359_read_imp(struct mt6359_auxadc *adc_dev,
			   const struct iio_chan_spec *chan, int *vbat, int *ibat)
{
	const struct mtk_pmic_auxadc_info *cinfo = adc_dev->chip_info;
	const struct mtk_pmic_auxadc_chan *desc = &cinfo->desc[chan->scan_index];
	struct regmap *regmap = adc_dev->regmap;
	u32 val, val_v, val_i;
	int ret;

	/* Start conversion */
	regmap_write(regmap, cinfo->regs[PMIC_AUXADC_IMP0], MT6359_IMP0_CONV_EN);
	ret = regmap_read_poll_timeout(regmap, cinfo->regs[desc->rdy_idx],
				       val, val & desc->rdy_mask,
				       IMP_POLL_DELAY_US, AUXADC_TIMEOUT_US);

	/* Stop conversion regardless of the result */
	regmap_write(regmap, cinfo->regs[PMIC_AUXADC_IMP0], 0);
	if (ret)
		return ret;

	/* If it succeeded, wait for the registers to be populated */
	fsleep(IMP_STOP_DELAY_US);

	ret = regmap_read(regmap, cinfo->regs[PMIC_AUXADC_IMP3], &val_v);
	if (ret)
		return ret;

	ret = regmap_read(regmap, cinfo->regs[PMIC_FGADC_R_CON0], &val_i);
	if (ret)
		return ret;

	if (vbat)
		*vbat = val_v;
	if (ibat)
		*ibat = val_i;

	return 0;
}

static const struct mtk_pmic_auxadc_info mt6357_chip_info = {
	.model_name = "MT6357",
	.channels = mt6357_auxadc_channels,
	.num_channels = ARRAY_SIZE(mt6357_auxadc_channels),
	.desc = mt6357_auxadc_ch_desc,
	.regs = mt6357_auxadc_regs,
	.imp_adc_num = MT6357_IMP_ADC_NUM,
	.read_imp = mt6358_read_imp,
	.vref_mV = 1800,
};

static const struct mtk_pmic_auxadc_info mt6358_chip_info = {
	.model_name = "MT6358",
	.channels = mt6358_auxadc_channels,
	.num_channels = ARRAY_SIZE(mt6358_auxadc_channels),
	.desc = mt6358_auxadc_ch_desc,
	.regs = mt6358_auxadc_regs,
	.imp_adc_num = MT6358_IMP_ADC_NUM,
	.read_imp = mt6358_read_imp,
	.vref_mV = 1800,
};

static const struct mtk_pmic_auxadc_info mt6359_chip_info = {
	.model_name = "MT6359",
	.channels = mt6359_auxadc_channels,
	.num_channels = ARRAY_SIZE(mt6359_auxadc_channels),
	.desc = mt6359_auxadc_ch_desc,
	.regs = mt6359_auxadc_regs,
	.sec_unlock_key = 0x6359,
	.read_imp = mt6359_read_imp,
	.vref_mV = 1800,
};

static const struct mtk_pmic_auxadc_info mt6363_chip_info = {
	.model_name = "MT6363",
	.channels = mt6363_auxadc_channels,
	.num_channels = ARRAY_SIZE(mt6363_auxadc_channels),
	.desc = mt6363_auxadc_ch_desc,
	.regs = mt6363_auxadc_regs,
	.is_spmi = true,
	.no_reset = true,
	.vref_mV = 1840,
};

static const struct mtk_pmic_auxadc_info mt6373_chip_info = {
	.model_name = "MT6373",
	.channels = mt6373_auxadc_channels,
	.num_channels = ARRAY_SIZE(mt6373_auxadc_channels),
	.desc = mt6373_auxadc_ch_desc,
	.regs = mt6363_auxadc_regs,
	.is_spmi = true,
	.no_reset = true,
	.vref_mV = 1840,
};

static void mt6359_auxadc_reset(struct mt6359_auxadc *adc_dev)
{
	const struct mtk_pmic_auxadc_info *cinfo = adc_dev->chip_info;
	struct regmap *regmap = adc_dev->regmap;

	/* Some PMICs do not support reset */
	if (cinfo->no_reset)
		return;

	/* Unlock HK_TOP writes */
	if (cinfo->sec_unlock_key)
		regmap_write(regmap, cinfo->regs[PMIC_HK_TOP_WKEY], cinfo->sec_unlock_key);

	/* Assert ADC reset */
	regmap_set_bits(regmap, cinfo->regs[PMIC_HK_TOP_RST_CON0], PMIC_RG_RESET_VAL);

	/* De-assert ADC reset. No wait required, as pwrap takes care of that for us. */
	regmap_clear_bits(regmap, cinfo->regs[PMIC_HK_TOP_RST_CON0], PMIC_RG_RESET_VAL);

	/* Lock HK_TOP writes again */
	if (cinfo->sec_unlock_key)
		regmap_write(regmap, cinfo->regs[PMIC_HK_TOP_WKEY], 0);
}

/**
 * mt6359_auxadc_sample_adc_val() - Start ADC channel sampling and read value
 * @adc_dev: Main driver structure
 * @chan:    IIO Channel spec for requested ADC
 * @out:     Preallocated variable to store the value read from HW
 *
 * This function starts the sampling for an ADC channel, waits until all
 * of the samples are averaged and then reads the value from the HW.
 *
 * Note that the caller must stop the ADC sampling on its own, as this
 * function *never* stops it.
 *
 * Return:
 * Negative number for error;
 * Upon success returns zero and writes the read value to *out.
 */
static int mt6359_auxadc_sample_adc_val(struct mt6359_auxadc *adc_dev,
					const struct iio_chan_spec *chan, u32 *out)
{
	const struct mtk_pmic_auxadc_info *cinfo = adc_dev->chip_info;
	const struct mtk_pmic_auxadc_chan *desc = &cinfo->desc[chan->scan_index];
	struct regmap *regmap = adc_dev->regmap;
	u32 reg, rdy_mask, val, lval;
	int ret;

	/* Request to start sampling for ADC channel */
	ret = regmap_write(regmap, cinfo->regs[desc->req_idx], desc->req_mask);
	if (ret)
		return ret;

	/* Wait until all samples are averaged */
	fsleep(desc->num_samples * AUXADC_AVG_TIME_US);

	reg = cinfo->regs[PMIC_AUXADC_ADC0] + (chan->address << 1);
	rdy_mask = PMIC_AUXADC_RDY_BIT;

	/*
	 * Even though for both PWRAP and SPMI cases the ADC HW signals that
	 * the data is ready by setting AUXADC_RDY_BIT, for SPMI the register
	 * read is only 8 bits long: for this case, the check has to be done
	 * on the ADC(x)_H register (high bits) and the rdy_mask needs to be
	 * shifted to the right by the same 8 bits.
	 */
	if (cinfo->is_spmi) {
		rdy_mask >>= 8;
		reg += 1;
	}

	ret = regmap_read_poll_timeout(regmap, reg, val, val & rdy_mask,
				       AUXADC_POLL_DELAY_US, AUXADC_TIMEOUT_US);
	if (ret) {
		dev_dbg(adc_dev->dev, "ADC read timeout for chan %lu\n", chan->address);
		return ret;
	}

	if (cinfo->is_spmi) {
		ret = regmap_read(regmap, reg - 1, &lval);
		if (ret)
			return ret;

		val = (val << 8) | lval;
	}

	*out = val;
	return 0;
}

static int mt6359_auxadc_read_adc(struct mt6359_auxadc *adc_dev,
				  const struct iio_chan_spec *chan, int *out)
{
	const struct mtk_pmic_auxadc_info *cinfo = adc_dev->chip_info;
	const struct mtk_pmic_auxadc_chan *desc = &cinfo->desc[chan->scan_index];
	struct regmap *regmap = adc_dev->regmap;
	int ret, adc_stop_err;
	u8 ext_sel;
	u32 val;

	if (desc->ext_sel_idx >= 0) {
		ext_sel = FIELD_PREP(MT6363_EXT_PURES_MASK, desc->ext_sel_pu);
		ext_sel |= FIELD_PREP(MT6363_EXT_CHAN_MASK, desc->ext_sel_ch);

		ret = regmap_update_bits(regmap, cinfo->regs[desc->ext_sel_idx],
					 MT6363_EXT_PURES_MASK | MT6363_EXT_CHAN_MASK,
					 ext_sel);
		if (ret)
			return ret;
	}

	/*
	 * Get sampled value, then stop sampling unconditionally; the gathered
	 * value is good regardless of if the ADC could be stopped.
	 *
	 * Note that if the ADC cannot be stopped but sampling was ok, this
	 * function will not return any error, but will set the timed_out
	 * status: this is not critical, as the ADC may auto recover and auto
	 * stop after some time (depending on the PMIC model); if not, the next
	 * read attempt will return -ETIMEDOUT and, for models that support it,
	 * reset will be triggered.
	 */
	ret = mt6359_auxadc_sample_adc_val(adc_dev, chan, &val);

	adc_stop_err = regmap_write(regmap, cinfo->regs[desc->req_idx], 0);
	if (adc_stop_err) {
		dev_warn(adc_dev->dev, "Could not stop the ADC: %d\n,", adc_stop_err);
		adc_dev->timed_out = true;
	}

	/* If any sampling error occurred, the retrieved value is invalid */
	if (ret)
		return ret;

	/* ...and deactivate the ADC GPIO if previously done */
	if (desc->ext_sel_idx >= 0) {
		ext_sel = FIELD_PREP(MT6363_EXT_PURES_MASK, MT6363_PULLUP_RES_OPEN);

		ret = regmap_update_bits(regmap, cinfo->regs[desc->ext_sel_idx],
					 MT6363_EXT_PURES_MASK, ext_sel);
		if (ret)
			return ret;
	}

	/* Everything went fine, give back the ADC reading */
	*out = val & GENMASK(chan->scan_type.realbits - 1, 0);
	return 0;
}

static int mt6359_auxadc_read_label(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan, char *label)
{
	return sysfs_emit(label, "%s\n", chan->datasheet_name);
}

static int mt6359_auxadc_read_raw(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan,
				  int *val, int *val2, long mask)
{
	struct mt6359_auxadc *adc_dev = iio_priv(indio_dev);
	const struct mtk_pmic_auxadc_info *cinfo = adc_dev->chip_info;
	const struct mtk_pmic_auxadc_chan *desc = &cinfo->desc[chan->scan_index];
	int ret;

	if (mask == IIO_CHAN_INFO_SCALE) {
		*val = desc->r_ratio.numerator * cinfo->vref_mV;

		if (desc->r_ratio.denominator > 1) {
			*val2 = desc->r_ratio.denominator;
			return IIO_VAL_FRACTIONAL;
		}

		return IIO_VAL_INT;
	}

	scoped_guard(mutex, &adc_dev->lock) {
		switch (chan->scan_index) {
		case PMIC_AUXADC_CHAN_IBAT:
			if (!adc_dev->chip_info->read_imp)
				return -EOPNOTSUPP;

			ret = adc_dev->chip_info->read_imp(adc_dev, chan, NULL, val);
			break;
		case PMIC_AUXADC_CHAN_VBAT:
			if (!adc_dev->chip_info->read_imp)
				return -EOPNOTSUPP;

			ret = adc_dev->chip_info->read_imp(adc_dev, chan, val, NULL);
			break;
		default:
			ret = mt6359_auxadc_read_adc(adc_dev, chan, val);
			break;
		}
	}

	if (ret) {
		/*
		 * If we get more than one timeout, it's possible that the
		 * AUXADC is stuck: perform a full reset to recover it.
		 */
		if (ret == -ETIMEDOUT) {
			if (adc_dev->timed_out) {
				dev_warn(adc_dev->dev, "Resetting stuck ADC!\r\n");
				mt6359_auxadc_reset(adc_dev);
			}
			adc_dev->timed_out = true;
		}
		return ret;
	}
	adc_dev->timed_out = false;

	return IIO_VAL_INT;
}

static const struct iio_info mt6359_auxadc_iio_info = {
	.read_label = mt6359_auxadc_read_label,
	.read_raw = mt6359_auxadc_read_raw,
};

static int mt6359_auxadc_probe(struct platform_device *pdev)
{
	const struct mtk_pmic_auxadc_info *chip_info;
	struct device *dev = &pdev->dev;
	struct device *mfd_dev = dev->parent;
	struct mt6359_auxadc *adc_dev;
	struct iio_dev *indio_dev;
	struct device *regmap_dev;
	struct regmap *regmap;
	int ret;

	chip_info = device_get_match_data(dev);
	if (!chip_info)
		return -EINVAL;
	/*
	 * The regmap for this device has to be acquired differently for
	 * SoC PMIC Wrapper and SPMI PMIC cases:
	 *
	 * If this is under SPMI, the regmap comes from the direct parent of
	 * this driver: this_device->parent(mfd).
	 *                            ... or ...
	 * If this is under the SoC PMIC Wrapper, the regmap comes from the
	 * parent of the MT6397 MFD: this_device->parent(mfd)->parent(pwrap)
	 */
	if (chip_info->is_spmi)
		regmap_dev = mfd_dev;
	else
		regmap_dev = mfd_dev->parent;


	/* Regmap is from SoC PMIC Wrapper, parent of the mt6397 MFD */
	regmap = dev_get_regmap(regmap_dev, NULL);
	if (!regmap)
		return dev_err_probe(dev, -ENODEV, "Failed to get regmap\n");

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc_dev));
	if (!indio_dev)
		return -ENOMEM;

	adc_dev = iio_priv(indio_dev);
	adc_dev->regmap = regmap;
	adc_dev->dev = dev;
	adc_dev->chip_info = chip_info;

	mutex_init(&adc_dev->lock);

	mt6359_auxadc_reset(adc_dev);

	indio_dev->name = adc_dev->chip_info->model_name;
	indio_dev->info = &mt6359_auxadc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = adc_dev->chip_info->channels;
	indio_dev->num_channels = adc_dev->chip_info->num_channels;

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register iio device\n");

	return 0;
}

static const struct of_device_id mt6359_auxadc_of_match[] = {
	{ .compatible = "mediatek,mt6357-auxadc", .data = &mt6357_chip_info },
	{ .compatible = "mediatek,mt6358-auxadc", .data = &mt6358_chip_info },
	{ .compatible = "mediatek,mt6359-auxadc", .data = &mt6359_chip_info },
	{ .compatible = "mediatek,mt6363-auxadc", .data = &mt6363_chip_info },
	{ .compatible = "mediatek,mt6373-auxadc", .data = &mt6373_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, mt6359_auxadc_of_match);

static struct platform_driver mt6359_auxadc_driver = {
	.driver = {
		.name = "mt6359-auxadc",
		.of_match_table = mt6359_auxadc_of_match,
	},
	.probe	= mt6359_auxadc_probe,
};
module_platform_driver(mt6359_auxadc_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_DESCRIPTION("MediaTek MT6359 PMIC AUXADC Driver");
